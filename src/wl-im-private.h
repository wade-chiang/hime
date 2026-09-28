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

#ifndef HIME_WL_IM_PRIVATE_H
#define HIME_WL_IM_PRIVATE_H

// Between the core of the Wayland input method (wl-im.c: the engine, keys,
// focus, windows) and the protocols a compositor offers: input-method-v2
// (wl-im-v2.c: wlroots, niri, Hyprland) and input-method-v1 (wl-im-v1.c:
// KWin).

#include <stdint.h>

#include <gtk/gtk.h>
#include <wayland-client.h>

// What the core asks of the protocol
typedef struct {
    // Commit TEXT (NULL: none), then show PREEDIT with the cursor at byte
    // CURSOR.  Called when either changed.
    void (*send) (const char *text, const char *preedit, int cursor);
    // pass a key (evdev code) or the modifiers on to the application
    void (*forward_key) (uint32_t time, uint32_t key, uint32_t state);
    void (*forward_modifiers) (uint32_t depressed, uint32_t latched,
                               uint32_t locked, uint32_t group);
    // the keymap of the keys, as text, changed (NULL if not needed)
    void (*keymap) (const char *text);
    // take the keyboard, or give it back
    void (*grab) (gboolean on);
    // the state a wl_im_queue_state () call with TOKEN described applies
    // now (NULL if not needed)
    void (*state) (gpointer token);
    // Give the popup role to SURFACE, the wl_surface of WIN, which is
    // being mapped; store the role object as the "hime-popup-surface" data
    // of WIN, with its destructor (NULL: no popups; checked each time)
    void (*popup) (GtkWidget *win, struct wl_surface *surface);
    // a state with another token needs a grab of its own (v1: a context)
    gboolean regrab;
} WlImProtocol;

// The protocol is ready: the input method of the seat is PROTOCOL (the
// first one a compositor offers)
void wl_im_start (const WlImProtocol *protocol);
gboolean wl_im_started (void);
// another input method has the seat
void wl_im_queue_unavailable (void);

// Events, handled in order (see the event queue in wl-im.c)
//
// A text field is focused (ACTIVE; ACTIVATED: it is a new one, or the
// application enabled text input again and dropped its preedit), and it
// takes a password (PASSWORD)
void wl_im_queue_state (gboolean active, gboolean password, gboolean activated, gpointer token);
// a key (STATE: a wl_keyboard key_state, also 2, repeated by the
// compositor) or the modifiers of the keyboard grab
void wl_im_queue_key (uint32_t time, uint32_t key, uint32_t state);
void wl_im_queue_modifiers (uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);

// The focused window changed to the one numbered WINDOW (0: none known),
// or the window WINDOW was closed (wl-toplevel.c)
void wl_im_queue_window (guint window);
void wl_im_queue_window_closed (guint window);

// the keymap (wl_keyboard.keymap) and repeat info of the keyboard grab
void wl_im_grab_keymap (uint32_t format, int32_t fd, uint32_t size);
void wl_im_grab_repeat_info (int32_t rate, int32_t delay);

// the seat of GDK's connection
struct wl_seat *wl_im_seat (void);

// the protocols' parts of the registry
gboolean wl_im_v2_global (struct wl_registry *registry, uint32_t name, const char *interface,
                          uint32_t version);
gboolean wl_im_v1_global (struct wl_registry *registry, uint32_t name, const char *interface,
                          uint32_t version);
// the compositor's window list
gboolean wl_toplevel_global (struct wl_registry *registry, uint32_t name, const char *interface,
                             uint32_t version);

#endif /* HIME_WL_IM_PRIVATE_H */
