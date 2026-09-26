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

// The Wayland input method through input-method-unstable-v1 (KWin, which
// starts the daemon itself and offers the protocol only on the connection
// it hands over; see hime_launched_by_compositor ()).  Each focused text
// field comes as a context: the keyboard grab, commits, the preedit and
// keys HIME does not handle go through it, with the serial of its last
// commit_state.  While the grab is taken, KWin passes neither keys nor
// modifiers on: the modifiers are passed back too.  See wl-im.c.

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <wayland-client.h>

#include "input-method-unstable-v1-client-protocol.h"
#include "wl-im-private.h"

// zwp_text_input_v1.content_purpose (KWin maps v2 and v3 purposes to it)
#define CONTENT_PURPOSE_PASSWORD 8

static struct zwp_input_method_v1 *im;
static struct zwp_input_panel_v1 *panel;

typedef struct {
    struct zwp_input_method_context_v1 *context;
    uint32_t serial;
} Context;

// the context activated last, as the events come, and the one the core
// works with (the state it applied)
static Context *latest, *current;
static struct wl_keyboard *grab;

static void destroy_context (Context *c) {
    zwp_input_method_context_v1_destroy (c->context);
    g_free (c);
}

static void commit_text (const char *text, const char *preedit, int cursor) {
    if (!current) {
        return;
    }
    if (text) {
        // also clears the preedit
        zwp_input_method_context_v1_commit_string (current->context, current->serial, text);
    }
    zwp_input_method_context_v1_preedit_cursor (current->context, cursor);
    zwp_input_method_context_v1_preedit_string (current->context, current->serial, preedit, "");
}

static void forward_key (uint32_t time, uint32_t key, uint32_t state) {
    if (current) {
        zwp_input_method_context_v1_key (current->context, current->serial, time, key, state);
    }
}

static void forward_modifiers (uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    if (current) {
        zwp_input_method_context_v1_modifiers (current->context, current->serial, depressed,
                                               latched, locked, group);
    }
}

static void keyboard_keymap (void *data, struct wl_keyboard *keyboard, uint32_t format,
                             int32_t fd, uint32_t size) {
    wl_im_grab_keymap (format, fd, size);
}

static void keyboard_enter (void *data, struct wl_keyboard *keyboard, uint32_t serial,
                            struct wl_surface *surface, struct wl_array *keys) {
}

static void keyboard_leave (void *data, struct wl_keyboard *keyboard, uint32_t serial,
                            struct wl_surface *surface) {
}

static void keyboard_key (void *data, struct wl_keyboard *keyboard, uint32_t serial,
                          uint32_t time, uint32_t key, uint32_t state) {
    wl_im_queue_key (time, key, state);
}

static void keyboard_modifiers (void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                uint32_t depressed, uint32_t latched, uint32_t locked,
                                uint32_t group) {
    wl_im_queue_modifiers (depressed, latched, locked, group);
}

static void keyboard_repeat_info (void *data, struct wl_keyboard *keyboard, int32_t rate,
                                  int32_t delay) {
    wl_im_grab_repeat_info (rate, delay);
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_keymap,
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifiers,
    keyboard_repeat_info,
};

static void grab_keyboard (gboolean on) {
    if (on && !grab && current) {
        grab = zwp_input_method_context_v1_grab_keyboard (current->context);
        wl_keyboard_add_listener (grab, &keyboard_listener, NULL);
    } else if (!on && grab) {
        // ends with its context
        wl_keyboard_destroy (grab);
        grab = NULL;
    }
}

static void state (gpointer token) {
    Context *c = token;
    if (c == current) {
        return;
    }
    // the grab of the old one is released (the core does that first)
    if (current) {
        destroy_context (current);
    }
    current = c;
}

// An overlay panel, which KWin shows below the text cursor (only one at a
// time: the main input window)
static void popup (GtkWidget *win, struct wl_surface *surface) {
    if (!panel) {
        return;
    }
    struct zwp_input_panel_surface_v1 *role = zwp_input_panel_v1_get_input_panel_surface (panel, surface);
    zwp_input_panel_surface_v1_set_overlay_panel (role);
    g_object_set_data_full (G_OBJECT (win), "hime-popup-surface", role,
                            (GDestroyNotify) zwp_input_panel_surface_v1_destroy);
}

static const WlImProtocol protocol = {
    commit_text,
    forward_key,
    forward_modifiers,
    NULL,
    grab_keyboard,
    state,
    popup,
};

static void context_surrounding_text (void *data, struct zwp_input_method_context_v1 *context,
                                      const char *text, uint32_t cursor, uint32_t anchor) {
}

static void context_reset (void *data, struct zwp_input_method_context_v1 *context) {
}

static void context_content_type (void *data, struct zwp_input_method_context_v1 *context,
                                  uint32_t hint, uint32_t purpose) {
    Context *c = data;
    if (c == latest) {
        wl_im_queue_state (TRUE, purpose == CONTENT_PURPOSE_PASSWORD, FALSE, c);
    }
}

static void context_invoke_action (void *data, struct zwp_input_method_context_v1 *context,
                                   uint32_t button, uint32_t index) {
}

static void context_commit_state (void *data, struct zwp_input_method_context_v1 *context,
                                  uint32_t serial) {
    Context *c = data;
    c->serial = serial;
}

static void context_preferred_language (void *data, struct zwp_input_method_context_v1 *context,
                                        const char *language) {
}

static const struct zwp_input_method_context_v1_listener context_listener = {
    context_surrounding_text,
    context_reset,
    context_content_type,
    context_invoke_action,
    context_commit_state,
    context_preferred_language,
};

static void im_activate (void *data, struct zwp_input_method_v1 *m,
                         struct zwp_input_method_context_v1 *context) {
    Context *c = g_new0 (Context, 1);
    c->context = context;
    zwp_input_method_context_v1_add_listener (context, &context_listener, c);
    latest = c;
    wl_im_queue_state (TRUE, FALSE, TRUE, c);
}

static void im_deactivate (void *data, struct zwp_input_method_v1 *m,
                           struct zwp_input_method_context_v1 *context) {
    // Another one may have been activated since: then this one is
    // replaced (and destroyed) when that one's state applies
    if (latest && latest->context == context) {
        latest = NULL;
        wl_im_queue_state (FALSE, FALSE, FALSE, NULL);
    }
}

static const struct zwp_input_method_v1_listener im_listener = {
    im_activate,
    im_deactivate,
};

gboolean wl_im_v1_global (struct wl_registry *registry, uint32_t name, const char *interface,
                          uint32_t version) {
    if (!strcmp (interface, zwp_input_method_v1_interface.name) && !im) {
        im = wl_registry_bind (registry, name, &zwp_input_method_v1_interface, 1);
        zwp_input_method_v1_add_listener (im, &im_listener, NULL);
        wl_im_start (&protocol);
    } else if (!strcmp (interface, zwp_input_panel_v1_interface.name) && !panel) {
        panel = wl_registry_bind (registry, name, &zwp_input_panel_v1_interface, 1);
    } else {
        return FALSE;
    }
    return TRUE;
}
