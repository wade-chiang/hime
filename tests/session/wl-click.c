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

/*
 * Clicks the left mouse button at points of the screen, as a mouse would
 * (e.g. on HIME's windows): through a virtual pointer on wlroots
 * compositors, through KWin's fake input on KWin (see wl-type.c), and on
 * GNOME through rd-type.py.  The screen is the 1280x800 output of the
 * session tests.
 *
 * Usage: wl-click X,Y...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <wayland-client.h>

#include "fake-input-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#define BTN_LEFT 0x110
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 800

static struct wl_display *display;
static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *manager;
static struct org_kde_kwin_fake_input *fake_input;

static void registry_global (void *data, struct wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t version) {
    if (!strcmp (interface, wl_seat_interface.name) && !seat) {
        seat = wl_registry_bind (registry, name, &wl_seat_interface, 1);
    } else if (!strcmp (interface, zwlr_virtual_pointer_manager_v1_interface.name)) {
        manager = wl_registry_bind (registry, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
    } else if (!strcmp (interface, org_kde_kwin_fake_input_interface.name) && version >= 4) {
        fake_input = wl_registry_bind (registry, name, &org_kde_kwin_fake_input_interface, 4);
    }
}

static void registry_global_remove (void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

static uint32_t now_ms (void) {
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main (int argc, char **argv) {
    display = wl_display_connect (NULL);
    if (!display) {
        fprintf (stderr, "wl-click: no Wayland display\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry (display);
    wl_registry_add_listener (registry, &registry_listener, NULL);
    wl_display_roundtrip (display);
    if (!seat || (!manager && !fake_input)) {
        // GNOME: through Mutter's RemoteDesktop D-Bus interface
        char *dir = g_path_get_dirname (argv[0]);
        char **rd_argv = g_new0 (char *, argc + 1);
        rd_argv[0] = g_build_filename (dir, "rd-type.py", NULL);
        for (int i = 1; i < argc; i++) {
            rd_argv[i] = g_strdup_printf ("@click:%s", argv[i]);
        }
        execv (rd_argv[0], rd_argv);
        fprintf (stderr, "wl-click: no virtual pointer support\n");
        return 1;
    }

    struct zwlr_virtual_pointer_v1 *pointer = NULL;
    if (manager) {
        pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer (manager, seat);
    } else {
        org_kde_kwin_fake_input_authenticate (fake_input, "wl-click", "HIME session tests");
    }
    wl_display_roundtrip (display);

    for (int i = 1; i < argc; i++) {
        int x, y;
        if (sscanf (argv[i], "%d,%d", &x, &y) != 2) {
            fprintf (stderr, "wl-click: bad point: %s\n", argv[i]);
            return 2;
        }
        if (pointer) {
            zwlr_virtual_pointer_v1_motion_absolute (pointer, now_ms (), x, y, SCREEN_WIDTH, SCREEN_HEIGHT);
            zwlr_virtual_pointer_v1_frame (pointer);
            wl_display_roundtrip (display);
            usleep (50000);
            zwlr_virtual_pointer_v1_button (pointer, now_ms (), BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
            zwlr_virtual_pointer_v1_frame (pointer);
            zwlr_virtual_pointer_v1_button (pointer, now_ms (), BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
            zwlr_virtual_pointer_v1_frame (pointer);
        } else {
            org_kde_kwin_fake_input_pointer_motion_absolute (fake_input, wl_fixed_from_int (x),
                                                             wl_fixed_from_int (y));
            wl_display_roundtrip (display);
            usleep (50000);
            org_kde_kwin_fake_input_button (fake_input, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
            org_kde_kwin_fake_input_button (fake_input, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        }
        wl_display_roundtrip (display);
        usleep (300000);
    }

    if (pointer) {
        zwlr_virtual_pointer_v1_destroy (pointer);
    }
    wl_display_roundtrip (display);
    wl_display_disconnect (display);
    return 0;
}
