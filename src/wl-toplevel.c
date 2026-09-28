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

// The window that has the keyboard focus, from the compositor's window
// list, so that each window of the text-input applications has an input
// state of its own (as fcitx5 does): wlr-foreign-toplevel-management
// (wlroots compositors, niri, Hyprland, labwc) or KDE's
// plasma-window-management (KWin, which offers it to the input method it
// started).  Windows are told apart by a number of their own (the
// objects' addresses come back).  Without either protocol, all windows
// share one state.

#include <string.h>

#include <wayland-client.h>

#include "hime.h"

#include "plasma-window-management-client-protocol.h"
#include "wl-im-private.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

typedef struct {
    guint id;
    // activated in the last complete state
    gboolean activated;
    // wlr: activated in the state being sent
    gboolean pending_activated;
} Toplevel;

static guint last_id;
// the focused window; 0: none known
static guint focused;
static guint lost_focus_source;

static Toplevel *toplevel_new (void) {
    Toplevel *toplevel = g_new0 (Toplevel, 1);
    toplevel->id = ++last_id;
    return toplevel;
}

static gboolean lost_focus_cb (gpointer data) {
    lost_focus_source = 0;
    if (focused) {
        focused = 0;
        wl_im_queue_window (0);
    }
    return G_SOURCE_REMOVE;
}

// The window's state is complete
static void update (Toplevel *toplevel, gboolean activated) {
    if (activated == toplevel->activated) {
        return;
    }
    toplevel->activated = activated;
    if (activated) {
        if (lost_focus_source) {
            g_source_remove (lost_focus_source);
            lost_focus_source = 0;
        }
        if (focused != toplevel->id) {
            focused = toplevel->id;
            wl_im_queue_window (focused);
        }
    } else if (focused == toplevel->id && !lost_focus_source) {
        // the other window usually says it is activated right after
        lost_focus_source = g_idle_add (lost_focus_cb, NULL);
    }
}

static void toplevel_gone (Toplevel *toplevel) {
    update (toplevel, FALSE);
    wl_im_queue_window_closed (toplevel->id);
    g_free (toplevel);
}

// wlr-foreign-toplevel-management

static void toplevel_title (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *title) {
}

static void toplevel_app_id (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *app_id) {
}

static void toplevel_output_enter (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
                                   struct wl_output *output) {
}

static void toplevel_output_leave (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
                                   struct wl_output *output) {
}

static void toplevel_state (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *state) {
    Toplevel *toplevel = data;
    toplevel->pending_activated = FALSE;
    uint32_t *s;
    wl_array_for_each (s, state) {
        if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
            toplevel->pending_activated = TRUE;
        }
    }
}

static void toplevel_done (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    Toplevel *toplevel = data;
    update (toplevel, toplevel->pending_activated);
}

static void toplevel_closed (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    zwlr_foreign_toplevel_handle_v1_destroy (handle);
    toplevel_gone (data);
}

static void toplevel_parent (void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
                             struct zwlr_foreign_toplevel_handle_v1 *parent) {
}

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_listener = {
    toplevel_title,
    toplevel_app_id,
    toplevel_output_enter,
    toplevel_output_leave,
    toplevel_state,
    toplevel_done,
    toplevel_closed,
    toplevel_parent,
};

static void manager_toplevel (void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
                              struct zwlr_foreign_toplevel_handle_v1 *handle) {
    zwlr_foreign_toplevel_handle_v1_add_listener (handle, &toplevel_listener, toplevel_new ());
}

static void manager_finished (void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) {
    zwlr_foreign_toplevel_manager_v1_destroy (manager);
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    manager_toplevel,
    manager_finished,
};

// plasma-window-management (bound at version 17)

static void plasma_title_changed (void *data, struct org_kde_plasma_window *pw, const char *title) {
}

static void plasma_app_id_changed (void *data, struct org_kde_plasma_window *pw, const char *app_id) {
}

static void plasma_state_changed (void *data, struct org_kde_plasma_window *pw, uint32_t flags) {
    update (data, (flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE) != 0);
}

static void plasma_virtual_desktop_changed (void *data, struct org_kde_plasma_window *pw, int32_t number) {
}

static void plasma_themed_icon_name_changed (void *data, struct org_kde_plasma_window *pw, const char *name) {
}

static void plasma_unmapped (void *data, struct org_kde_plasma_window *pw) {
    org_kde_plasma_window_destroy (pw);
    toplevel_gone (data);
}

static void plasma_initial_state (void *data, struct org_kde_plasma_window *pw) {
}

static void plasma_parent_window (void *data, struct org_kde_plasma_window *pw,
                                  struct org_kde_plasma_window *parent) {
}

static void plasma_geometry (void *data, struct org_kde_plasma_window *pw, int32_t x, int32_t y,
                             uint32_t width, uint32_t height) {
}

static void plasma_icon_changed (void *data, struct org_kde_plasma_window *pw) {
}

static void plasma_pid_changed (void *data, struct org_kde_plasma_window *pw, uint32_t pid) {
}

static void plasma_virtual_desktop_entered (void *data, struct org_kde_plasma_window *pw, const char *id) {
}

static void plasma_virtual_desktop_left (void *data, struct org_kde_plasma_window *pw, const char *id) {
}

static void plasma_application_menu (void *data, struct org_kde_plasma_window *pw,
                                     const char *service_name, const char *object_path) {
}

static void plasma_activity_entered (void *data, struct org_kde_plasma_window *pw, const char *id) {
}

static void plasma_activity_left (void *data, struct org_kde_plasma_window *pw, const char *id) {
}

static void plasma_resource_name_changed (void *data, struct org_kde_plasma_window *pw, const char *name) {
}

static const struct org_kde_plasma_window_listener plasma_window_listener = {
    plasma_title_changed,
    plasma_app_id_changed,
    plasma_state_changed,
    plasma_virtual_desktop_changed,
    plasma_themed_icon_name_changed,
    plasma_unmapped,
    plasma_initial_state,
    plasma_parent_window,
    plasma_geometry,
    plasma_icon_changed,
    plasma_pid_changed,
    plasma_virtual_desktop_entered,
    plasma_virtual_desktop_left,
    plasma_application_menu,
    plasma_activity_entered,
    plasma_activity_left,
    plasma_resource_name_changed,
};

static void plasma_show_desktop_changed (void *data, struct org_kde_plasma_window_management *pwm, uint32_t state) {
}

static void plasma_window (void *data, struct org_kde_plasma_window_management *pwm, uint32_t id) {
}

static void plasma_stacking_order_changed (void *data, struct org_kde_plasma_window_management *pwm,
                                           struct wl_array *ids) {
}

static void plasma_stacking_order_uuid_changed (void *data, struct org_kde_plasma_window_management *pwm,
                                                const char *uuids) {
}

static void plasma_window_with_uuid (void *data, struct org_kde_plasma_window_management *pwm, uint32_t id,
                                     const char *uuid) {
    struct org_kde_plasma_window *pw = org_kde_plasma_window_management_get_window_by_uuid (pwm, uuid);
    org_kde_plasma_window_add_listener (pw, &plasma_window_listener, toplevel_new ());
}

static void plasma_stacking_order_changed_2 (void *data, struct org_kde_plasma_window_management *pwm) {
}

static const struct org_kde_plasma_window_management_listener plasma_listener = {
    plasma_show_desktop_changed,
    plasma_window,
    plasma_stacking_order_changed,
    plasma_stacking_order_uuid_changed,
    plasma_window_with_uuid,
    plasma_stacking_order_changed_2,
};

static gboolean bound;

gboolean wl_toplevel_global (struct wl_registry *registry, uint32_t name, const char *interface,
                             uint32_t version) {
    if (bound) {
        return FALSE;
    }
    if (!strcmp (interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
        struct zwlr_foreign_toplevel_manager_v1 *manager = wl_registry_bind (
            registry, name, &zwlr_foreign_toplevel_manager_v1_interface, MIN (version, 3));
        zwlr_foreign_toplevel_manager_v1_add_listener (manager, &manager_listener, NULL);
    } else if (!strcmp (interface, org_kde_plasma_window_management_interface.name) && version >= 17) {
        struct org_kde_plasma_window_management *pwm = wl_registry_bind (
            registry, name, &org_kde_plasma_window_management_interface, 17);
        org_kde_plasma_window_management_add_listener (pwm, &plasma_listener, NULL);
    } else {
        return FALSE;
    }
    bound = TRUE;
    dbg ("wl-toplevel: %s\n", interface);
    return TRUE;
}
