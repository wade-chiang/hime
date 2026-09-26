/*
 * Copyright (C) 2026 The HIME team, Taiwan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation version 2.1
 * of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// The daemon as the seat's Wayland input method (input-method-unstable-v2,
// as wlroots compositors, niri and Hyprland offer it).  Applications using
// text-input-v3 (GTK 4 and Qt 6 without an IM module setting, GTK 3 with
// GTK_IM_MODULE=wayland, Firefox, Chromium with --enable-wayland-ime, ...)
// then need no HIME module: while a text field is focused, the compositor
// sends the keys here, and HIME commits text and sets the preedit through
// the protocol.  Keys HIME does not handle go on to the application through
// a virtual keyboard.
//
// All such applications share one ClientState, like one X client.  The
// input method runs on GDK's own Wayland connection: the compositor only
// lets the virtual keyboard of the input method's own connection past the
// keyboard grab.

// memfd_create
#define _GNU_SOURCE

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "hime.h"

#include "hime-im-client.h"
#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wl-im.h"

// from eve.c
extern char *output_buffer;
extern uint32_t output_bufferN;

// zwp_text_input_v3.content_purpose
#define CONTENT_PURPOSE_PASSWORD 8
#define CONTENT_PURPOSE_PIN 9

// evdev key codes are below 256 (KEY_MAX is 0x2ff, but keyboards do not
// send those)
#define KEYS_N 0x300

static struct zwp_input_method_manager_v2 *im_manager;
static struct zwp_virtual_keyboard_manager_v1 *vk_manager;
static struct zwp_input_method_v2 *im;
static struct zwp_input_method_keyboard_grab_v2 *grab;
static struct zwp_virtual_keyboard_v1 *vk;

static struct xkb_context *xkb_context;
static struct xkb_keymap *keymap;
static struct xkb_state *xkb_state;
// the keymap the virtual keyboard has, as text
static char *vk_keymap;

// done events received: the serial of commit requests
static uint32_t serial;
// state since the last done event, and the state it applied
static gboolean pending_active, active;
static uint32_t pending_purpose;
// the field takes a password: pass all keys on
static gboolean bypass;

// the one ClientState of all text-input applications
static ClientState wl_cs;
static gboolean wl_cs_inited;

// the preedit the application has
static char *shown_preedit;
static int shown_cursor;

// keys pressed during the grab, and those passed on to the application
static guint8 pressed[KEYS_N];
static guint8 forwarded[KEYS_N];

// repeat of a held key that HIME handles (the application repeats the
// keys it gets)
static int32_t repeat_rate = 25, repeat_delay = 600;
static guint repeat_source;
static uint32_t repeat_key;

// Events of the grab and the input method, handled in order: showing a
// window while handling a key can make GDK dispatch the next events (e.g.
// the key's release) before that is done.
enum {
    EVENT_KEY,
    EVENT_MODIFIERS,
    EVENT_DONE,
};
typedef struct {
    int type;
    uint32_t arg[4];
} Event;
static GQueue events = G_QUEUE_INIT;
static gboolean handling;

static void handle_events (void);

static void queue_event (int type, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    Event *event = g_new (Event, 1);
    event->type = type;
    event->arg[0] = a;
    event->arg[1] = b;
    event->arg[2] = c;
    event->arg[3] = d;
    g_queue_push_tail (&events, event);
    handle_events ();
}

static uint32_t now_ms (void) {
    return (uint32_t) (g_get_monotonic_time () / 1000);
}

// X modifier masks: ShiftMask, LockMask, ControlMask, Mod1Mask ... Mod5Mask
static const char *const x_mod_names[8] = {
    XKB_MOD_NAME_SHIFT, XKB_MOD_NAME_CAPS, XKB_MOD_NAME_CTRL,
    "Mod1", "Mod2", "Mod3", "Mod4", "Mod5"};

static uint32_t x_state (void) {
    uint32_t mask = 0;
    for (int i = 0; i < 8; i++) {
        if (xkb_state_mod_name_is_active (xkb_state, x_mod_names[i], XKB_STATE_MODS_EFFECTIVE) > 0) {
            mask |= 1u << i;
        }
    }
    return mask;
}

static void set_vk_keymap (const char *text) {
    if (vk_keymap && !strcmp (vk_keymap, text)) {
        return;
    }
    const size_t size = strlen (text) + 1;
    const int fd = memfd_create ("hime-keymap", MFD_CLOEXEC);
    if (fd < 0) {
        return;
    }
    if (write (fd, text, size) != (ssize_t) size) {
        close (fd);
        return;
    }
    zwp_virtual_keyboard_v1_keymap (vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
    close (fd);
    g_free (vk_keymap);
    vk_keymap = g_strdup (text);
}

static void use_keymap (struct xkb_keymap *new_keymap) {
    if (xkb_state) {
        xkb_state_unref (xkb_state);
    }
    if (keymap) {
        xkb_keymap_unref (keymap);
    }
    keymap = new_keymap;
    xkb_state = xkb_state_new (keymap);

    // the virtual keyboard sends key codes of the same keymap
    char *text = xkb_keymap_get_as_string (keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (text) {
        set_vk_keymap (text);
        free (text);
    }
}

// Some compositors send no keymap (size 0): use the default one
static gboolean have_keymap (void) {
    if (!keymap) {
        struct xkb_keymap *default_keymap = xkb_keymap_new_from_names (xkb_context, NULL, 0);
        if (!default_keymap) {
            return FALSE;
        }
        use_keymap (default_keymap);
    }
    return TRUE;
}

// Send the text HIME committed and the preedit to the application.
static void flush (void) {
    if (!im || !active) {
        return;
    }

    gboolean changed = FALSE;
    if (output_bufferN) {
        zwp_input_method_v2_commit_string (im, output_buffer);
        changed = TRUE;
    }
    clear_output_buffer ();

    char str[HIME_PREEDIT_MAX_STR];
    HIME_PREEDIT_ATTR attr[HIME_PREEDIT_ATTR_MAX_N];
    int cursor = 0, sub_comp_len = 0;
    str[0] = '\0';
    if (!bypass) {
        // e.g. after a mouse action, current_CS may be the connection of a
        // HIME tool; the preedit is ours
        ClientState *const cs = current_CS;
        current_CS = &wl_cs;
        hime_get_preedit (&wl_cs, str, attr, &cursor, &sub_comp_len);
        current_CS = cs;
    }
    // as for HIME clients (do_get_preedit)
    if (hime_edit_display & (HIME_EDIT_DISPLAY_BOTH | HIME_EDIT_DISPLAY_OVER_THE_SPOT)) {
        cursor = 0;
    }
    if (hime_edit_display & HIME_EDIT_DISPLAY_OVER_THE_SPOT) {
        str[0] = '\0';
    }
    // the cursor counts characters; the protocol wants bytes
    if (!g_utf8_validate (str, -1, NULL)) {
        str[0] = '\0';
    }
    const glong chars = g_utf8_strlen (str, -1);
    if (cursor < 0 || cursor > chars) {
        cursor = chars;
    }
    const int cursor_bytes = g_utf8_offset_to_pointer (str, cursor) - str;

    if (g_strcmp0 (shown_preedit, str) || shown_cursor != cursor_bytes) {
        g_free (shown_preedit);
        shown_preedit = g_strdup (str);
        shown_cursor = cursor_bytes;
        changed = TRUE;
    }

    if (!changed) {
        return;
    }
    // A commit applies all pending state, and an unset preedit is empty
    if (shown_preedit[0]) {
        zwp_input_method_v2_set_preedit_string (im, shown_preedit, shown_cursor, shown_cursor);
    }
    zwp_input_method_v2_commit (im, serial);
}

gboolean wl_im_ready (void) {
    return im && active && hime_focused_client () == &wl_cs;
}

void wl_im_send (void) {
    if (wl_im_ready ()) {
        flush ();
    }
}

static void forward_key (uint32_t time, uint32_t key, uint32_t state) {
    zwp_virtual_keyboard_v1_key (vk, time, key, state);
    if (key < KEYS_N) {
        forwarded[key] = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    }
}

static void stop_repeat (void) {
    if (repeat_source) {
        g_source_remove (repeat_source);
        repeat_source = 0;
    }
}

static gboolean process_press (uint32_t key) {
    const KeySym keysym = xkb_state_key_get_one_sym (xkb_state, key + 8);
    current_CS = &wl_cs;
    save_CS_temp_to_current ();
    return ProcessKeyPress (keysym, x_state ());
}

static gboolean repeat_cb (gpointer data) {
    const gboolean first = GPOINTER_TO_INT (data);
    if (handling) {
        return G_SOURCE_CONTINUE;
    }
    handling = TRUE;
    gboolean again = TRUE;
    if (!process_press (repeat_key)) {
        // e.g. BackSpace held until the preedit is empty: the application
        // takes over, and repeats it itself
        flush ();
        forward_key (now_ms (), repeat_key, WL_KEYBOARD_KEY_STATE_PRESSED);
        again = FALSE;
    } else {
        flush ();
    }
    handling = FALSE;

    if (!again) {
        repeat_source = 0;
    } else if (first) {
        repeat_source = g_timeout_add (1000 / repeat_rate, repeat_cb, GINT_TO_POINTER (FALSE));
    }
    // events that came meanwhile, e.g. the key's release
    handle_events ();
    return again && !first ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void key_press (uint32_t time, uint32_t key) {
    stop_repeat ();
    if (key < KEYS_N) {
        pressed[key] = TRUE;
    }

    if (bypass) {
        forward_key (time, key, WL_KEYBOARD_KEY_STATE_PRESSED);
        return;
    }

    const gboolean eaten = process_press (key);
    // text committed before the key, e.g. by Enter, comes first
    flush ();
    if (!eaten) {
        forward_key (time, key, WL_KEYBOARD_KEY_STATE_PRESSED);
        return;
    }

    if (repeat_rate > 0 && xkb_keymap_key_repeats (keymap, key + 8)) {
        repeat_key = key;
        repeat_source = g_timeout_add (repeat_delay, repeat_cb, GINT_TO_POINTER (TRUE));
    }
}

static void key_release (uint32_t time, uint32_t key) {
    if (key == repeat_key) {
        stop_repeat ();
    }

    if (!bypass) {
        const KeySym keysym = xkb_state_key_get_one_sym (xkb_state, key + 8);
        current_CS = &wl_cs;
        save_CS_temp_to_current ();
        ProcessKeyRelease (keysym, x_state ());
        flush ();
    }

    // the application gets the release of each press it got, also of keys
    // pressed before the grab
    if (key >= KEYS_N || forwarded[key] || !pressed[key]) {
        forward_key (time, key, WL_KEYBOARD_KEY_STATE_RELEASED);
    }
    if (key < KEYS_N) {
        pressed[key] = FALSE;
    }
}

static void grab_keymap (void *data, struct zwp_input_method_keyboard_grab_v2 *g,
                         uint32_t format, int32_t fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !size) {
        close (fd);
        return;
    }
    char *text = mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close (fd);
    if (text == MAP_FAILED) {
        return;
    }
    struct xkb_keymap *new_keymap = xkb_keymap_new_from_buffer (
        xkb_context, text, strnlen (text, size), XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap (text, size);
    if (new_keymap) {
        use_keymap (new_keymap);
    }
}

static void grab_key (void *data, struct zwp_input_method_keyboard_grab_v2 *g,
                      uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    queue_event (EVENT_KEY, time, key, state, 0);
}

static void grab_modifiers (void *data, struct zwp_input_method_keyboard_grab_v2 *g,
                            uint32_t serial, uint32_t depressed, uint32_t latched,
                            uint32_t locked, uint32_t group) {
    queue_event (EVENT_MODIFIERS, depressed, latched, locked, group);
}

static void grab_repeat_info (void *data, struct zwp_input_method_keyboard_grab_v2 *g,
                              int32_t rate, int32_t delay) {
    repeat_rate = rate;
    repeat_delay = delay;
}

static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
    grab_keymap,
    grab_key,
    grab_modifiers,
    grab_repeat_info,
};

// The compositor sends all keys to a grab, also those of applications with
// a HIME module: grab only while a text-input field is focused.
static void start_grab (void) {
    memset (pressed, 0, sizeof (pressed));
    grab = zwp_input_method_v2_grab_keyboard (im);
    zwp_input_method_keyboard_grab_v2_add_listener (grab, &grab_listener, NULL);
}

static void stop_grab (void) {
    stop_repeat ();
    if (grab) {
        zwp_input_method_keyboard_grab_v2_release (grab);
        grab = NULL;
    }
    // do not leave keys held down in the virtual keyboard
    const uint32_t time = now_ms ();
    for (uint32_t key = 0; key < KEYS_N; key++) {
        if (forwarded[key]) {
            forward_key (time, key, WL_KEYBOARD_KEY_STATE_RELEASED);
        }
    }
    if (xkb_state) {
        zwp_virtual_keyboard_v1_modifiers (
            vk, 0, 0,
            xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout (xkb_state, XKB_STATE_LAYOUT_EFFECTIVE));
    }
}

static void focus_in (void) {
    if (!wl_cs_inited) {
        wl_cs_inited = TRUE;
        wl_cs.b_hime_protocol = TRUE;
        wl_cs.input_style = InputStyleOverSpot;
        wl_cs.use_preedit = TRUE;
        if (hime_init_im_enabled) {
            current_CS = &wl_cs;
            save_CS_temp_to_current ();
            init_state_chinese (&wl_cs);
        }
    }

    // the application has dropped its preedit
    g_free (shown_preedit);
    shown_preedit = g_strdup ("");
    shown_cursor = 0;

    // another field: drop what was typed into the last one
    hime_reset ();
    hime_FocusIn (&wl_cs);
    if (bypass) {
        hide_in_win (&wl_cs);
    }
}

static void focus_out (void) {
    hime_reset ();
    clear_output_buffer ();
    if (current_CS == &wl_cs) {
        hime_FocusOut (&wl_cs);
        hide_in_win (&wl_cs);
    }
}

static void im_activate (void *data, struct zwp_input_method_v2 *m) {
    pending_active = TRUE;
    // activate resets the state
    pending_purpose = 0;
}

static void im_deactivate (void *data, struct zwp_input_method_v2 *m) {
    pending_active = FALSE;
}

static void im_surrounding_text (void *data, struct zwp_input_method_v2 *m,
                                 const char *text, uint32_t cursor, uint32_t anchor) {
}

static void im_text_change_cause (void *data, struct zwp_input_method_v2 *m, uint32_t cause) {
}

static void im_content_type (void *data, struct zwp_input_method_v2 *m,
                             uint32_t hint, uint32_t purpose) {
    pending_purpose = purpose;
}

static void im_done (void *data, struct zwp_input_method_v2 *m) {
    serial++;
    queue_event (EVENT_DONE, pending_active, pending_purpose, 0, 0);
}

static void apply_state (gboolean new_active, uint32_t purpose) {
    const gboolean was_active = active;
    const gboolean was_bypass = bypass;
    active = new_active;
    bypass = purpose == CONTENT_PURPOSE_PASSWORD || purpose == CONTENT_PURPOSE_PIN;

    dbg ("wl-im: done %u, active %d, purpose %u\n", serial, active, purpose);

    if (active && !was_active) {
        start_grab ();
        focus_in ();
    } else if (!active && was_active) {
        stop_grab ();
        focus_out ();
    } else if (active && bypass != was_bypass) {
        focus_in ();
    }
}

static void handle_events (void) {
    if (handling) {
        return;
    }
    handling = TRUE;
    Event *event;
    while ((event = g_queue_pop_head (&events))) {
        const uint32_t *arg = event->arg;
        switch (event->type) {
        case EVENT_KEY:
            if (!have_keymap ()) {
                break;
            }
            if (arg[2] == WL_KEYBOARD_KEY_STATE_PRESSED) {
                key_press (arg[0], arg[1]);
            } else {
                key_release (arg[0], arg[1]);
            }
            break;
        case EVENT_MODIFIERS:
            if (!have_keymap ()) {
                break;
            }
            xkb_state_update_mask (xkb_state, arg[0], arg[1], arg[2], 0, 0, arg[3]);
            zwp_virtual_keyboard_v1_modifiers (vk, arg[0], arg[1], arg[2], arg[3]);
            break;
        case EVENT_DONE:
            apply_state (arg[0], arg[1]);
            break;
        }
        g_free (event);
    }
    handling = FALSE;
}

static void im_unavailable (void *data, struct zwp_input_method_v2 *m) {
    fprintf (stderr, "hime: another Wayland input method is running\n");
    zwp_input_method_v2_destroy (im);
    im = NULL;
    active = FALSE;
}

static const struct zwp_input_method_v2_listener im_listener = {
    im_activate,
    im_deactivate,
    im_surrounding_text,
    im_text_change_cause,
    im_content_type,
    im_done,
    im_unavailable,
};

static void create_input_method (void) {
    GdkDisplay *display = gdk_display_get_default ();
    struct wl_seat *seat = gdk_wayland_seat_get_wl_seat (gdk_display_get_default_seat (display));
    if (!seat) {
        return;
    }
    xkb_context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
    im = zwp_input_method_manager_v2_get_input_method (im_manager, seat);
    zwp_input_method_v2_add_listener (im, &im_listener, NULL);
    vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard (vk_manager, seat);
    dbg ("wl-im: input method created\n");
}

static void registry_global (void *data, struct wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t version) {
    if (!strcmp (interface, zwp_input_method_manager_v2_interface.name)) {
        im_manager = wl_registry_bind (registry, name, &zwp_input_method_manager_v2_interface, 1);
    } else if (!strcmp (interface, zwp_virtual_keyboard_manager_v1_interface.name)) {
        vk_manager = wl_registry_bind (registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
    if (im_manager && vk_manager && !im && !xkb_context) {
        create_input_method ();
    }
}

static void registry_global_remove (void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

void wl_im_init (void) {
    GdkDisplay *display = gdk_display_get_default ();
    if (!GDK_IS_WAYLAND_DISPLAY (display) || getenv ("HIME_NO_WAYLAND_IM")) {
        return;
    }
    // GDK dispatches the events of its connection, also these
    struct wl_display *wl_display = gdk_wayland_display_get_wl_display (display);
    struct wl_registry *registry = wl_display_get_registry (wl_display);
    wl_registry_add_listener (registry, &registry_listener, NULL);
    wl_display_flush (wl_display);
}
