/*
 * Copyright (C) 2020 The HIME team, Taiwan
 * Copyright (C) 2011 Edward Der-Hua Liu, Hsin-Chu, Taiwan
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

#include "hime.h"

#include "win-common.h"
#include "wl-im.h"

#if HIME_LAYER_SHELL
#include <gtk-layer-shell.h>
#endif

// On the Wayland backend (no X display) with a compositor supporting
// wlr-layer-shell, the daemon's windows are layer surfaces: they never take
// the keyboard focus and are placed by anchoring them to the top-left corner
// of one output with margins, as windows cannot position themselves there.
// That is the output at the origin of the layout, Wayland having no primary
// one (get_primary_monitor ()).
gboolean hime_use_layer_shell (void) {
#if HIME_LAYER_SHELL
    static int use = -1;
    if (use < 0) {
        use = !dpy && gtk_layer_is_supported ();
    }
    return use;
#else
    return FALSE;
#endif
}

// Call right after gtk_window_new, before the window is realized.  A
// positioned window is moved with hime_window_move; other windows are
// centered.
void hime_window_init (GtkWidget *win, gboolean positioned) {
#if HIME_LAYER_SHELL
    if (!hime_use_layer_shell ()) {
        return;
    }

    GtkWindow *window = GTK_WINDOW (win);
    gtk_layer_init_for_window (window);
    gtk_layer_set_namespace (window, "hime");
    gtk_layer_set_layer (window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode (window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    // margins count from the output edges, not from other surfaces' zones
    gtk_layer_set_exclusive_zone (window, -1);
    // all on one output, whose size get_display_size () uses: positions
    // are relative to it
    gtk_layer_set_monitor (window, get_primary_monitor ());

    if (positioned) {
        gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor (window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    }
#endif
}

static gboolean is_popup (GtkWidget *win) {
    return g_object_get_data (G_OBJECT (win), "hime-popup") != NULL;
}

gboolean hime_window_is_popup (GtkWidget *win) {
    return win && is_popup (win);
}

// A new window for the main input windows (the ones following the text
// cursor in OverSpot): an input popup surface while a Wayland text-input
// field is focused, placed by the compositor; otherwise as with
// hime_window_init (win, TRUE).
GtkWidget *hime_input_window_new (void) {
    if (wl_im_popup_wanted ()) {
        return wl_im_popup_window_new ();
    }
    GtkWidget *win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    hime_window_init (win, TRUE);
    return win;
}

// Is WIN, from hime_input_window_new (), the wrong kind of window now (the
// focus moved between a text-input field and a HIME client, or the input
// style changed)?  It has to be created again: a window cannot change.
gboolean hime_input_window_stale (GtkWidget *win) {
    return win && is_popup (win) != wl_im_popup_wanted ();
}

void hime_window_move (GtkWidget *win, int x, int y) {
    if (is_popup (win)) {
        // the compositor places it; shrink it to what it shows now
        gtk_window_resize (GTK_WINDOW (win), 1, 1);
        return;
    }
#if HIME_LAYER_SHELL
    if (hime_use_layer_shell ()) {
        GtkWindow *window = GTK_WINDOW (win);
        gtk_layer_set_margin (window, GTK_LAYER_SHELL_EDGE_LEFT, x);
        gtk_layer_set_margin (window, GTK_LAYER_SHELL_EDGE_TOP, y);
        // the compositor does not tell where a surface is: remember it
        g_object_set_data (G_OBJECT (win), "hime-x", GINT_TO_POINTER (x));
        g_object_set_data (G_OBJECT (win), "hime-y", GINT_TO_POINTER (y));
        return;
    }
#endif
    gtk_window_move (GTK_WINDOW (win), x, y);
}

void hime_window_get_position (GtkWidget *win, int *x, int *y) {
    if (is_popup (win)) {
        // unknown: windows placed next to it go to the fixed position
        *x = hime_root_x;
        *y = hime_root_y;
        return;
    }
#if HIME_LAYER_SHELL
    if (hime_use_layer_shell ()) {
        *x = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (win), "hime-x"));
        *y = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (win), "hime-y"));
        return;
    }
#endif
    gtk_window_get_position (GTK_WINDOW (win), x, y);
}

char *get_full_str () {
    if (!chinese_mode ()) {
        if (hime_use_custom_theme)
            return eng_color_half_str;
        else
            return _ (eng_half_str);
    }

    if (current_CS->b_im_enabled) {
        if (current_fullwidth_mode ()) {
            if (hime_use_custom_theme)
                return cht_color_full_str;
            else
                return _ (cht_full_str);
        }
    } else if (current_fullwidth_mode ()) {
        if (hime_use_custom_theme)
            return eng_color_full_str;
        else
            return _ (eng_full_str);
    }
    return ("");
}

void get_win_geom (GtkWidget *win) {
    if (!win)
        return;
    hime_window_get_position (win, &win_x, &win_y);
    get_win_size (win, &input_window_width, &input_window_height);
}

void move_win (GtkWidget *win, int x, int y) {
    if (!win)
        return;

    int best_win_x = x;
    int best_win_y = y;

    get_win_size (win, &input_window_width, &input_window_height);

    if (x + input_window_width > display_width)
        best_win_x = display_width - input_window_width;
    if (x < 0)
        best_win_x = 0;

    if (y + input_window_height > display_height)
        best_win_y = display_height - input_window_height;
    if (y < 0)
        best_win_y = 0;

    hime_window_move (win, best_win_x, best_win_y);

    win_x = best_win_x;
    win_y = best_win_y;
}

void get_win_size (GtkWidget *win, int *width, int *height) {
    GtkRequisition sz;
    sz.width = sz.height = 0;
    gtk_widget_get_preferred_size (GTK_WIDGET (win), NULL, &sz);
    *width = sz.width;
    *height = sz.height;
}
