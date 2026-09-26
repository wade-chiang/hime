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

// The Wayland input method through input-method-unstable-v2 (wlroots
// compositors, niri, Hyprland): commits and the preedit are double-buffered
// state, applied by a commit request, and keys HIME does not handle go on
// to the application through a virtual keyboard on the same connection
// (wlroots lets only that one past the keyboard grab).  See wl-im.c.

// memfd_create
#define _GNU_SOURCE

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <wayland-client.h>

#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wl-im-private.h"

// zwp_text_input_v3.content_purpose
#define CONTENT_PURPOSE_PASSWORD 8
#define CONTENT_PURPOSE_PIN 9

static struct zwp_input_method_manager_v2 *im_manager;
static struct zwp_virtual_keyboard_manager_v1 *vk_manager;
static struct zwp_input_method_v2 *im;
static struct zwp_input_method_keyboard_grab_v2 *grab;
static struct zwp_virtual_keyboard_v1 *vk;
// the keymap the virtual keyboard has, as text
static char *vk_keymap;

// done events received, and the state applied: commits carry the serial
// of the state they were made for
static uint32_t serial, applied_serial;
// state since the last done event; activated: an activate came, also while
// active (a new field, or text input enabled again: the application dropped
// its preedit)
static gboolean pending_active, pending_activated;
static uint32_t pending_purpose;

static void commit_text (const char *text, const char *preedit, int cursor) {
    if (text) {
        zwp_input_method_v2_commit_string (im, text);
    }
    // A commit applies all pending state, and an unset preedit is empty
    if (preedit[0]) {
        zwp_input_method_v2_set_preedit_string (im, preedit, cursor, cursor);
    }
    zwp_input_method_v2_commit (im, applied_serial);
}

static void forward_key (uint32_t time, uint32_t key, uint32_t state) {
    zwp_virtual_keyboard_v1_key (vk, time, key, state);
}

static void forward_modifiers (uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    zwp_virtual_keyboard_v1_modifiers (vk, depressed, latched, locked, group);
}

static void keymap (const char *text) {
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

static void grab_keymap (void *data, struct zwp_input_method_keyboard_grab_v2 *g,
                         uint32_t format, int32_t fd, uint32_t size) {
    wl_im_grab_keymap (format, fd, size);
}

static void grab_key (void *data, struct zwp_input_method_keyboard_grab_v2 *g,
                      uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    wl_im_queue_key (time, key, state);
}

static void grab_modifiers (void *data, struct zwp_input_method_keyboard_grab_v2 *g,
                            uint32_t serial, uint32_t depressed, uint32_t latched,
                            uint32_t locked, uint32_t group) {
    wl_im_queue_modifiers (depressed, latched, locked, group);
}

static void grab_repeat_info (void *data, struct zwp_input_method_keyboard_grab_v2 *g,
                              int32_t rate, int32_t delay) {
    wl_im_grab_repeat_info (rate, delay);
}

static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
    grab_keymap,
    grab_key,
    grab_modifiers,
    grab_repeat_info,
};

static void grab_keyboard (gboolean on) {
    if (on && !grab) {
        grab = zwp_input_method_v2_grab_keyboard (im);
        zwp_input_method_keyboard_grab_v2_add_listener (grab, &grab_listener, NULL);
    } else if (!on && grab) {
        zwp_input_method_keyboard_grab_v2_release (grab);
        grab = NULL;
    }
}

static void state (gpointer token) {
    applied_serial = GPOINTER_TO_UINT (token);
}

static void popup_text_input_rectangle (void *data, struct zwp_input_popup_surface_v2 *popup,
                                        int32_t x, int32_t y, int32_t width, int32_t height) {
}

static const struct zwp_input_popup_surface_v2_listener popup_listener = {
    popup_text_input_rectangle,
};

static void popup (GtkWidget *win, struct wl_surface *surface) {
    struct zwp_input_popup_surface_v2 *role = zwp_input_method_v2_get_input_popup_surface (im, surface);
    zwp_input_popup_surface_v2_add_listener (role, &popup_listener, NULL);
    g_object_set_data_full (G_OBJECT (win), "hime-popup-surface", role,
                            (GDestroyNotify) zwp_input_popup_surface_v2_destroy);
}

static const WlImProtocol protocol = {
    commit_text,
    forward_key,
    forward_modifiers,
    keymap,
    grab_keyboard,
    state,
    popup,
    FALSE,
};

static void im_activate (void *data, struct zwp_input_method_v2 *m) {
    pending_active = TRUE;
    pending_activated = TRUE;
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
    wl_im_queue_state (pending_active,
                       pending_purpose == CONTENT_PURPOSE_PASSWORD || pending_purpose == CONTENT_PURPOSE_PIN,
                       pending_activated, GUINT_TO_POINTER (serial));
    pending_activated = FALSE;
}

static void im_unavailable (void *data, struct zwp_input_method_v2 *m) {
    // the core stops using it first (queued)
    wl_im_queue_unavailable ();
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

gboolean wl_im_v2_global (struct wl_registry *registry, uint32_t name, const char *interface,
                          uint32_t version) {
    if (!strcmp (interface, zwp_input_method_manager_v2_interface.name)) {
        im_manager = wl_registry_bind (registry, name, &zwp_input_method_manager_v2_interface, 1);
    } else if (!strcmp (interface, zwp_virtual_keyboard_manager_v1_interface.name)) {
        vk_manager = wl_registry_bind (registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    } else {
        return FALSE;
    }

    struct wl_seat *seat = wl_im_seat ();
    if (im_manager && vk_manager && !im && seat && !wl_im_started ()) {
        im = zwp_input_method_manager_v2_get_input_method (im_manager, seat);
        zwp_input_method_v2_add_listener (im, &im_listener, NULL);
        vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard (vk_manager, seat);
        wl_im_start (&protocol);
    }
    return TRUE;
}
