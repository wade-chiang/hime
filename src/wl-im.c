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

// The daemon as the seat's Wayland input method.  Applications using
// text-input (GTK 4 and Qt 6 without an IM module setting, GTK 3 with
// GTK_IM_MODULE=wayland, Firefox, Chromium with --enable-wayland-ime, ...)
// then need no HIME module: while a text field is focused, the compositor
// sends the keys here, and HIME commits text and sets the preedit through
// the protocol.  Keys HIME does not handle go on to the application.
//
// This is the core; the protocols are input-method-v2 (wl-im-v2.c:
// wlroots compositors, niri, Hyprland) and input-method-v1 (wl-im-v1.c:
// KWin).  Each window of the text-input applications has a ClientState of
// its own, as each window of an X client has, when the compositor tells
// which one has the focus (wl-toplevel.c).  The input method runs on
// GDK's own Wayland connection: its windows must be on the one of the input
// method (popups), and wlroots only lets the virtual keyboard of that
// connection past the keyboard grab.

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "hime.h"

#include "hime-im-client.h"
#include "win-sym.h"
#include "wl-im-private.h"
#include "wl-im.h"

// from eve.c
extern char *output_buffer;
extern uint32_t output_bufferN;

// evdev key codes are below 256 (KEY_MAX is 0x2ff, but keyboards do not
// send those)
#define KEYS_N 0x300

// wl_keyboard key_state repeated (since version 10), sent by KWin
#define KEY_STATE_REPEATED 2

// the protocol, once the compositor offers one
static const WlImProtocol *protocol;

static struct xkb_context *xkb_context;
static struct xkb_keymap *keymap;
static struct xkb_state *xkb_state;

// the state applied: a text field is focused, and takes a password (pass
// all keys on); the protocol's token of that state
static gboolean active, bypass;
static gpointer applied_token;

// The ClientState of each window, by the number wl-toplevel.c gave it (0:
// windows it did not tell of, or all of them without a window list), and
// that of the focused field's window (NULL until a field is focused)
static GHashTable *window_states;
static ClientState *wl_cs;
// the focused window, as wl-toplevel.c told
static guint focused_window;

// the preedit the application has
static char *shown_preedit;
static int shown_cursor;

// keys pressed during the grab, and those passed on to the application
static guint8 pressed[KEYS_N];
static guint8 forwarded[KEYS_N];

// Repeat of a held key that HIME handles (the application repeats the
// keys it gets).  Each grab sends its own rate: 0 when the compositor
// repeats keys itself (KWin, for applications that let it repeat theirs).
#define REPEAT_RATE 25
#define REPEAT_DELAY 600
static int32_t repeat_rate = REPEAT_RATE, repeat_delay = REPEAT_DELAY;
static guint repeat_source;
static uint32_t repeat_key;

// the modifiers held, as passed on to the application
static uint32_t forwarded_depressed, forwarded_latched;

// Events of the grab and the input method, handled in order: showing a
// window while handling a key can make GDK dispatch the next events (e.g.
// the key's release) before that is done.
enum {
    EVENT_KEY,
    EVENT_MODIFIERS,
    EVENT_STATE,
    EVENT_UNAVAILABLE,
    EVENT_WINDOW,
    EVENT_WINDOW_CLOSED,
};
typedef struct {
    int type;
    uint32_t arg[4];
    gpointer token;
} Event;
static GQueue events = G_QUEUE_INIT;
static gboolean handling;

static void handle_events (void);

static void queue_event (int type, uint32_t a, uint32_t b, uint32_t c, uint32_t d, gpointer token) {
    Event *event = g_new (Event, 1);
    event->type = type;
    event->arg[0] = a;
    event->arg[1] = b;
    event->arg[2] = c;
    event->arg[3] = d;
    event->token = token;
    g_queue_push_tail (&events, event);
    handle_events ();
}

void wl_im_queue_state (gboolean new_active, gboolean password, gboolean activated, gpointer token) {
    queue_event (EVENT_STATE, new_active, password, activated, 0, token);
}

void wl_im_queue_key (uint32_t time, uint32_t key, uint32_t state) {
    queue_event (EVENT_KEY, time, key, state, 0, NULL);
}

void wl_im_queue_modifiers (uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    queue_event (EVENT_MODIFIERS, depressed, latched, locked, group, NULL);
}

void wl_im_queue_unavailable (void) {
    queue_event (EVENT_UNAVAILABLE, 0, 0, 0, 0, NULL);
}

void wl_im_queue_window (guint window) {
    queue_event (EVENT_WINDOW, window, 0, 0, 0, NULL);
}

void wl_im_queue_window_closed (guint window) {
    queue_event (EVENT_WINDOW_CLOSED, window, 0, 0, 0, NULL);
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

static void use_keymap (struct xkb_keymap *new_keymap) {
    if (xkb_state) {
        xkb_state_unref (xkb_state);
    }
    if (keymap) {
        xkb_keymap_unref (keymap);
    }
    keymap = new_keymap;
    xkb_state = xkb_state_new (keymap);

    // e.g. the virtual keyboard sends key codes of the same keymap
    if (protocol && protocol->keymap) {
        char *text = xkb_keymap_get_as_string (keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
        if (text) {
            protocol->keymap (text);
            free (text);
        }
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

void wl_im_grab_keymap (uint32_t format, int32_t fd, uint32_t size) {
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

void wl_im_grab_repeat_info (int32_t rate, int32_t delay) {
    // KWin sends rate 0 when it repeats keys itself
    repeat_rate = rate;
    repeat_delay = delay;
}

// Send the text HIME committed and the preedit to the application.
static void flush (void) {
    if (!protocol || !active) {
        // nowhere to commit it: do not leave it for a HIME client's reply
        clear_output_buffer ();
        return;
    }

    char *text = NULL;
    if (output_bufferN) {
        text = g_strdup (output_buffer);
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
        current_CS = wl_cs;
        hime_get_preedit (wl_cs, str, attr, &cursor, &sub_comp_len);
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

    gboolean changed = text != NULL;
    if (g_strcmp0 (shown_preedit, str) || shown_cursor != cursor_bytes) {
        g_free (shown_preedit);
        shown_preedit = g_strdup (str);
        shown_cursor = cursor_bytes;
        changed = TRUE;
    }

    if (changed) {
        protocol->send (text, shown_preedit, shown_cursor);
    }
    g_free (text);
}

gboolean wl_im_ready (void) {
    return protocol && active && wl_cs && hime_focused_client () == wl_cs;
}

void wl_im_send (void) {
    if (wl_im_ready ()) {
        flush ();
    }
}

static void forward_key (uint32_t time, uint32_t key, uint32_t state) {
    protocol->forward_key (time, key, state);
    if (key < KEYS_N) {
        forwarded[key] = state != WL_KEYBOARD_KEY_STATE_RELEASED;
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
    current_CS = wl_cs;
    save_CS_temp_to_current ();
    return ProcessKeyPress (keysym, x_state ());
}

static gboolean repeat_cb (gpointer data) {
    const gboolean first = GPOINTER_TO_INT (data);
    if (handling) {
        return G_SOURCE_CONTINUE;
    }
    if (!protocol || !active || !wl_cs || bypass || repeat_rate <= 0) {
        repeat_source = 0;
        return G_SOURCE_REMOVE;
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

// The compositor repeats a held key (KWin): as our own repeat, which it
// replaces (older KWin sends no rate)
static void key_repeated (uint32_t time, uint32_t key) {
    stop_repeat ();
    if (key < KEYS_N && forwarded[key]) {
        forward_key (time, key, KEY_STATE_REPEATED);
        return;
    }
    if (bypass || (key < KEYS_N && !pressed[key])) {
        return;
    }
    const gboolean eaten = process_press (key);
    flush ();
    if (!eaten) {
        // the application takes over
        forward_key (time, key, WL_KEYBOARD_KEY_STATE_PRESSED);
    }
}

static void key_release (uint32_t time, uint32_t key) {
    if (key == repeat_key) {
        stop_repeat ();
    }

    if (!bypass) {
        const KeySym keysym = xkb_state_key_get_one_sym (xkb_state, key + 8);
        current_CS = wl_cs;
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

// The compositor sends all keys to a grab, also those of applications with
// a HIME module: grab only while a text-input field is focused.
static void start_grab (void) {
    memset (pressed, 0, sizeof (pressed));
    // until the grab sends its own
    repeat_rate = REPEAT_RATE;
    repeat_delay = REPEAT_DELAY;
    protocol->grab (TRUE);
}

static void stop_grab (void) {
    stop_repeat ();
    protocol->grab (FALSE);
    // do not leave keys held down
    const uint32_t time = now_ms ();
    for (uint32_t key = 0; key < KEYS_N; key++) {
        if (forwarded[key]) {
            forward_key (time, key, WL_KEYBOARD_KEY_STATE_RELEASED);
        }
    }
    // Only if some are held: the focus may have moved on already, and the
    // new window would see them released
    if (xkb_state && (forwarded_depressed || forwarded_latched)) {
        protocol->forward_modifiers (
            0, 0,
            xkb_state_serialize_mods (xkb_state, XKB_STATE_MODS_LOCKED),
            xkb_state_serialize_layout (xkb_state, XKB_STATE_LAYOUT_EFFECTIVE));
        forwarded_depressed = forwarded_latched = 0;
    }
}

static ClientState *window_state (guint window) {
    if (!window_states) {
        window_states = g_hash_table_new_full (NULL, NULL, NULL, g_free);
    }
    ClientState *cs = g_hash_table_lookup (window_states, GUINT_TO_POINTER (window));
    if (!cs) {
        cs = g_new0 (ClientState, 1);
        cs->b_hime_protocol = TRUE;
        cs->input_style = InputStyleOverSpot;
        cs->use_preedit = TRUE;
        hime_init_client_state (cs, TRUE);
        g_hash_table_insert (window_states, GUINT_TO_POINTER (window), cs);
    }
    return cs;
}

static void focus_in (void) {
    wl_cs = window_state (focused_window);

    // the application has dropped its preedit
    g_free (shown_preedit);
    shown_preedit = g_strdup ("");
    shown_cursor = 0;

    // another field: drop what was typed into the last one
    hime_reset ();
    hime_FocusIn (wl_cs);
    if (bypass) {
        hide_in_win (wl_cs);
    }
}

// Text typed but not committed is dropped: the protocol takes no commits
// after deactivate (the HIME modules commit it on focus out).
static void focus_out (void) {
    // a HIME client got the focus in the meantime: it is its engine and
    // window now
    if (!wl_cs || hime_focused_client () != wl_cs) {
        return;
    }
    // current_CS may be a HIME tool's connection, or none
    ClientState *const cs = current_CS;
    current_CS = wl_cs;
    hime_reset ();
    clear_output_buffer ();
    hime_FocusOut (wl_cs);
    // also when hime_FocusOut skips it, as for a quick second focus out
    hide_in_win (wl_cs);
    hide_win_sym ();
    current_CS = cs ? cs : wl_cs;
}

// The focused field is in another window than the state it uses, e.g. the
// compositor told of the window after the field: use that window's state.
// The application does not know, and keeps the preedit: clear it.
static void switch_window (void) {
    dbg ("wl-im: window %u\n", focused_window);
    stop_repeat ();
    focus_out ();
    flush ();
    focus_in ();
}

static void window_focused (guint window) {
    focused_window = window;
    // Keep the state when no window has the focus (as the field is still
    // focused, e.g. a window of the compositor's shell is).  Only focus_in
    // creates a state: that switches the input method.
    if (window && active &&
        (!window_states || wl_cs != g_hash_table_lookup (window_states, GUINT_TO_POINTER (window)))) {
        switch_window ();
    }
}

static void window_closed (guint window) {
    // the next field is in another window, which the compositor may tell of
    // after the field
    if (window == focused_window) {
        focused_window = 0;
    }
    ClientState *const cs = window_states ? g_hash_table_lookup (window_states, GUINT_TO_POINTER (window)) : NULL;
    if (!cs) {
        return;
    }
    if (cs == wl_cs) {
        // the field goes away with the window: its deactivate, or the next
        // field's activate, comes next
        stop_repeat ();
        focus_out ();
        wl_cs = NULL;
    }
    if (current_CS == cs) {
        current_CS = NULL;
    }
    hime_forget_client (cs);
    g_hash_table_remove (window_states, GUINT_TO_POINTER (window));
}

static void apply_state (gboolean new_active, gboolean password, gboolean activated, gpointer token) {
    if (!protocol) {
        return;
    }
    const gboolean was_active = active;
    const gboolean was_bypass = bypass;

    dbg ("wl-im: active %d, password %d, activated %d\n", new_active, password, activated);

    if (new_active != was_active || password != was_bypass || activated) {
        stop_repeat ();
    }
    // A grab belongs to the state it was taken for (v1: to a field's
    // context), released with it
    const gboolean new_state = protocol->regrab && token != applied_token;
    if (was_active && (!new_active || new_state)) {
        stop_grab ();
    }
    active = new_active;
    bypass = password;
    applied_token = token;
    if (protocol->state) {
        protocol->state (token);
    }

    if (active && (!was_active || new_state)) {
        start_grab ();
    }
    if (active && (!was_active || activated)) {
        focus_in ();
    } else if (!active && was_active) {
        focus_out ();
    } else if (active && bypass != was_bypass) {
        focus_in ();
    }
}

// another input method has the seat
static void become_unavailable (void) {
    fprintf (stderr, "hime: another Wayland input method is running\n");
    if (active) {
        stop_grab ();
        focus_out ();
        active = FALSE;
    }
    protocol = NULL;
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
            // keys queued before the grab was released, or after the
            // field's window closed
            if (!protocol || !active || !wl_cs || !have_keymap ()) {
                break;
            }
            if (arg[2] == WL_KEYBOARD_KEY_STATE_PRESSED) {
                key_press (arg[0], arg[1]);
            } else if (arg[2] == KEY_STATE_REPEATED) {
                key_repeated (arg[0], arg[1]);
            } else {
                key_release (arg[0], arg[1]);
            }
            break;
        case EVENT_MODIFIERS:
            if (!protocol || !have_keymap ()) {
                break;
            }
            xkb_state_update_mask (xkb_state, arg[0], arg[1], arg[2], 0, 0, arg[3]);
            protocol->forward_modifiers (arg[0], arg[1], arg[2], arg[3]);
            forwarded_depressed = arg[0];
            forwarded_latched = arg[1];
            break;
        case EVENT_STATE:
            apply_state (arg[0], arg[1], arg[2], event->token);
            break;
        case EVENT_UNAVAILABLE:
            if (protocol) {
                become_unavailable ();
            }
            break;
        case EVENT_WINDOW:
            window_focused (arg[0]);
            break;
        case EVENT_WINDOW_CLOSED:
            window_closed (arg[0]);
            break;
        }
        g_free (event);
    }
    handling = FALSE;
}

gboolean wl_im_started (void) {
    return protocol != NULL;
}

void wl_im_start (const WlImProtocol *new_protocol) {
    protocol = new_protocol;
    dbg ("wl-im: input method created\n");
}

struct wl_seat *wl_im_seat (void) {
    GdkDisplay *display = gdk_display_get_default ();
    return gdk_wayland_seat_get_wl_seat (gdk_display_get_default_seat (display));
}

// OverSpot: the input window of a text-input field is a popup surface of
// the input method, which the compositor places next to the text cursor.

gboolean wl_im_popup_wanted (void) {
    return hime_input_style == InputStyleOverSpot && wl_im_ready () && protocol->popup;
}

// Each time the window is shown, GDK creates its wl_surface again: give it
// the role then, after GtkWindow's map, before the surface is committed.
static void popup_map (GtkWidget *win, gpointer data) {
    struct wl_surface *surface = gdk_wayland_window_get_wl_surface (gtk_widget_get_window (win));
    if (!surface || !protocol || !protocol->popup) {
        return;
    }
    protocol->popup (win, surface);
}

// A window with the popup role.  GtkWindow's unmap destroys the
// wl_surface, and destroying it before its role is a protocol error: drop
// the role first.  (Handlers and emission hooks of the signal run too late,
// after the class handler, which gtk-layer-shell also overrides.)
typedef struct {
    GtkWindow parent;
} HimePopupWindow;

typedef struct {
    GtkWindowClass parent_class;
} HimePopupWindowClass;

G_DEFINE_TYPE (HimePopupWindow, hime_popup_window, GTK_TYPE_WINDOW)

static void hime_popup_window_unmap (GtkWidget *win) {
    g_object_set_data (G_OBJECT (win), "hime-popup-surface", NULL);
    GTK_WIDGET_CLASS (hime_popup_window_parent_class)->unmap (win);
}

static void hime_popup_window_class_init (HimePopupWindowClass *klass) {
    GTK_WIDGET_CLASS (klass)->unmap = hime_popup_window_unmap;
}

static void hime_popup_window_init (HimePopupWindow *win) {
}

GtkWidget *wl_im_popup_window_new (void) {
    GtkWidget *win = g_object_new (hime_popup_window_get_type (), "type", GTK_WINDOW_TOPLEVEL, NULL);
    // GTK draws a title bar (and a close button) on a toplevel when the
    // compositor offers no server-side decorations (KDE's protocol, which
    // niri has not); gtk-layer-shell drops it for the other windows
    gtk_window_set_decorated (GTK_WINDOW (win), FALSE);
    gtk_widget_realize (win);
    // no xdg role: the window gets the popup role when it is mapped
    gdk_wayland_window_set_use_custom_surface (gtk_widget_get_window (win));
    g_signal_connect_after (win, "map", G_CALLBACK (popup_map), NULL);
    g_object_set_data (G_OBJECT (win), "hime-popup", GINT_TO_POINTER (TRUE));
    return win;
}

static void registry_global (void *data, struct wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t version) {
    if (!xkb_context) {
        xkb_context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
    }
    if (!wl_im_v2_global (registry, name, interface, version) &&
        !wl_im_v1_global (registry, name, interface, version)) {
        wl_toplevel_global (registry, name, interface, version);
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
