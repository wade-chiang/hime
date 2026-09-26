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
 * Types keys into the IM module GTK picks (GTK_IM_MODULE), the way a text
 * widget would, and prints the module in use and what it commits.  Built
 * against GTK 3 and GTK 4; run it in run-session.sh to test the HIME
 * module on a native Wayland display.
 *
 * Usage: gtk{3,4}-im-test KEY...
 *   KEY is a single printable character or one of <space> <enter> <bs>
 *   <esc>, optionally prefixed by S- (Shift) and/or C- (Control).
 *   @wait MS runs the main loop for MS, then prints what the module
 *   committed meanwhile (notifications) and its preedit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

static GString *commits;

static void on_commit (GtkIMContext *context, const char *str, gpointer data) {
    g_string_append (commits, str);
}

static guint parse_key (const char *tok, GdkModifierType *state) {
    static const struct {
        const char *name;
        guint keyval;
    } named_keys[] = {
        {"<space>", GDK_KEY_space},
        {"<enter>", GDK_KEY_Return},
        {"<bs>", GDK_KEY_BackSpace},
        {"<esc>", GDK_KEY_Escape},
    };

    *state = 0;
    for (;;) {
        if (!strncmp (tok, "S-", 2) && tok[2])
            *state |= GDK_SHIFT_MASK;
        else if (!strncmp (tok, "C-", 2) && tok[2])
            *state |= GDK_CONTROL_MASK;
        else
            break;
        tok += 2;
    }

    size_t i;
    for (i = 0; i < G_N_ELEMENTS (named_keys); i++)
        if (!strcmp (tok, named_keys[i].name))
            return named_keys[i].keyval;

    if (strlen (tok) == 1 && tok[0] > ' ' && tok[0] < 127) {
        // the key value is translated for the modifiers, as GDK reports it
        const char c = (*state & GDK_SHIFT_MASK) && tok[0] >= 'a' && tok[0] <= 'z' ? tok[0] - 'a' + 'A' : tok[0];
        return gdk_unicode_to_keyval (c);
    }

    return GDK_KEY_VoidSymbol;
}

#if GTK_CHECK_VERSION(4, 0, 0)

static GtkWidget *window, *text;

static void setup (GtkIMContext *context) {
    window = gtk_window_new ();
    text = gtk_text_new ();
    gtk_window_set_child (GTK_WINDOW (window), text);
    gtk_widget_realize (window);
    gtk_im_context_set_client_widget (context, text);
}

static gboolean send_key (GtkIMContext *context, guint keyval, GdkModifierType state, gboolean press) {
    GdkDisplay *display = gtk_widget_get_display (window);
    GdkKeymapKey *keys = NULL;
    int n_keys = 0;
    guint keycode = 0, group = 0;
    if (gdk_display_map_keyval (display, keyval, &keys, &n_keys) && n_keys) {
        keycode = keys[0].keycode;
        group = keys[0].group;
    }
    g_free (keys);

    GdkDevice *keyboard = gdk_seat_get_keyboard (gdk_display_get_default_seat (display));
    GdkSurface *surface = gtk_native_get_surface (GTK_NATIVE (window));

    return gtk_im_context_filter_key (context, press, surface, keyboard,
                                      GDK_CURRENT_TIME, keycode, state, group);
}

#else

static GtkWidget *window;

static void setup (GtkIMContext *context) {
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_realize (window);
    gtk_im_context_set_client_window (context, gtk_widget_get_window (window));
}

static gboolean send_key (GtkIMContext *context, guint keyval, GdkModifierType state, gboolean press) {
    GdkWindow *gdk_window = gtk_widget_get_window (window);
    GdkEvent *event = gdk_event_new (press ? GDK_KEY_PRESS : GDK_KEY_RELEASE);
    event->key.window = g_object_ref (gdk_window);
    event->key.time = GDK_CURRENT_TIME;
    event->key.keyval = keyval;
    event->key.state = state;

    GdkKeymapKey *keys = NULL;
    int n_keys = 0;
    GdkKeymap *keymap = gdk_keymap_get_for_display (gdk_window_get_display (gdk_window));
    if (gdk_keymap_get_entries_for_keyval (keymap, keyval, &keys, &n_keys) && n_keys) {
        event->key.hardware_keycode = keys[0].keycode;
        event->key.group = keys[0].group;
    }
    g_free (keys);

    GdkSeat *seat = gdk_display_get_default_seat (gdk_window_get_display (gdk_window));
    gdk_event_set_device (event, gdk_seat_get_keyboard (seat));

    const gboolean eaten = gtk_im_context_filter_keypress (context, &event->key);
    gdk_event_free (event);
    return eaten;
}

#endif

int main (int argc, char **argv) {
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_init ();
#else
    gtk_init (&argc, &argv);
#endif

    commits = g_string_new (NULL);

    GtkIMContext *context = gtk_im_multicontext_new ();
    g_signal_connect (context, "commit", G_CALLBACK (on_commit), NULL);
    setup (context);
    gtk_im_context_focus_in (context);

    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp (argv[i], "@wait") && i + 1 < argc) {
            g_string_truncate (commits, 0);
            const gint64 end = g_get_monotonic_time () + atoi (argv[++i]) * 1000;
            while (g_get_monotonic_time () < end) {
                while (g_main_context_iteration (NULL, FALSE))
                    ;
                g_usleep (10000);
            }

            char *preedit = NULL;
            gtk_im_context_get_preedit_string (context, &preedit, NULL, NULL);
            printf ("@wait    commit=\"%s\" preedit=\"%s\"\n", commits->str, preedit);
            g_free (preedit);
            continue;
        }

        GdkModifierType state;
        guint keyval = parse_key (argv[i], &state);
        if (keyval == GDK_KEY_VoidSymbol) {
            fprintf (stderr, "bad key: %s\n", argv[i]);
            return 2;
        }

        g_string_truncate (commits, 0);
        const gboolean eaten = send_key (context, keyval, state, TRUE);
        send_key (context, keyval, state, FALSE);

        printf ("%-8s %s", argv[i], eaten ? "eat " : "pass");
        if (commits->len)
            printf (" commit=\"%s\"", commits->str);
        printf ("\n");
    }

    printf ("module=%s\n", gtk_im_multicontext_get_context_id (GTK_IM_MULTICONTEXT (context)));
    printf ("backend=%s\n", G_OBJECT_TYPE_NAME (gdk_display_get_default ()));

    gtk_im_context_focus_out (context);
    return 0;
}
