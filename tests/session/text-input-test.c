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
 * A GTK entry using the Wayland text-input protocol (GTK's own "wayland"
 * IM module, no HIME module), typed into with real key events from
 * wl-type: HIME gets them as the seat's input method.  Built against GTK 3
 * and GTK 4.
 *
 * Usage: gtk{3,4}-text-input-test [--password] [--im MODULE] [--two] TOKEN...
 *   --im MODULE   use that GTK IM module instead, e.g. hime
 *   --two         a second entry after the first (<tab> moves there); its
 *                 text is printed too
 *   TOKEN is a key for wl-type (see wl-type.c), or:
 *   @check        print the entry's text and preedit
 *   @hold KEY     hold KEY down for a second
 *   @hook ACTION  make the daemon do what a mouse action does (see
 *                 hime_test_hook in src/eve.c): commit TEXT, preedit or
 *                 key K
 *   @exec PROGRAM[:ARG...]
 *                 run tests/session/PROGRAM with the ARGs (e.g. to take a
 *                 screenshot while the field is focused and HIME's window
 *                 shown); its exit status 77 (skipped) ends the test with it
 *   The text and preedit are printed once more at the end.  The window's
 *   background is blue, which HIME's windows are not.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <gtk/gtk.h>

#if !GTK_CHECK_VERSION(4, 0, 0)
#define gtk_editable_get_text(editable) gtk_entry_get_text (GTK_ENTRY (editable))
#endif

static char **tokens;
static int tokensN, next_token;
static char *dir;
static GtkWidget *entry, *entry2;
static char *preedit;
static gboolean started;

static void on_preedit_changed (GtkWidget *widget, const char *text, gpointer data) {
    g_free (preedit);
    preedit = g_strdup (text);
}

static void print_state (void) {
    printf ("text=\"%s\" ", gtk_editable_get_text (GTK_EDITABLE (entry)));
    if (entry2) {
        printf ("text2=\"%s\" ", gtk_editable_get_text (GTK_EDITABLE (entry2)));
    }
    printf ("preedit=\"%s\"\n", preedit ? preedit : "");
    fflush (stdout);
}

static void quit (void);
static void run_tokens (void);

static gboolean continue_cb (gpointer data) {
    run_tokens ();
    return G_SOURCE_REMOVE;
}

static void child_exited (GPid pid, gint status, gpointer data) {
    g_spawn_close_pid (pid);
    if (WIFEXITED (status) && WEXITSTATUS (status) == 77) {
        exit (77);
    }
    // let the input method's commits arrive
    g_timeout_add (300, continue_cb, NULL);
}

static void spawn (GPtrArray *argv) {
    g_ptr_array_add (argv, NULL);
    GPid pid;
    GError *error = NULL;
    if (!g_spawn_async (NULL, (char **) argv->pdata, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
                        NULL, NULL, &pid, &error)) {
        fprintf (stderr, "text-input-test: %s\n", error->message);
        exit (1);
    }
    g_child_watch_add (pid, child_exited, NULL);
}

// Type the keys up to the next @check or @hook, or do that
static void run_tokens (void) {
    if (next_token >= tokensN) {
        print_state ();
        quit ();
        return;
    }

    if (!strcmp (tokens[next_token], "@check")) {
        next_token++;
        print_state ();
        run_tokens ();
        return;
    }

    GPtrArray *argv = g_ptr_array_new_with_free_func (g_free);
    if (!strcmp (tokens[next_token], "@exec") && next_token + 1 < tokensN) {
        char **words = g_strsplit (tokens[next_token + 1], ":", -1);
        g_ptr_array_add (argv, g_build_filename (dir, words[0], NULL));
        for (int i = 1; words[i]; i++) {
            g_ptr_array_add (argv, g_strdup (words[i]));
        }
        g_strfreev (words);
        next_token += 2;
    } else if (!strcmp (tokens[next_token], "@hook") && next_token + 1 < tokensN) {
        const char *action = tokens[next_token + 1];
        const gboolean has_arg = (!strcmp (action, "commit") || !strcmp (action, "key")) &&
                                 next_token + 2 < tokensN;
        g_ptr_array_add (argv, g_build_filename (dir, "hime-client-test", NULL));
        g_ptr_array_add (argv, g_strdup ("-m"));
        g_ptr_array_add (argv, g_strdup_printf ("#hime_test %s%s%s", action, has_arg ? " " : "",
                                                has_arg ? tokens[next_token + 2] : ""));
        next_token += has_arg ? 3 : 2;
    } else {
        g_ptr_array_add (argv, g_build_filename (dir, "wl-type", NULL));
        while (next_token < tokensN && strcmp (tokens[next_token], "@check") &&
               strcmp (tokens[next_token], "@hook") && strcmp (tokens[next_token], "@exec")) {
            g_ptr_array_add (argv, g_strdup (tokens[next_token++]));
        }
    }
    spawn (argv);
    g_ptr_array_unref (argv);
}

static gboolean timeout_cb (gpointer data) {
    fprintf (stderr, "text-input-test: timed out\n");
    print_state ();
    exit (1);
}

static gboolean start_cb (gpointer data) {
    run_tokens ();
    return G_SOURCE_REMOVE;
}

// Once the window has the keyboard focus, the entry enables text input and
// the compositor activates the input method: give that a moment.
static void on_active (GObject *window, GParamSpec *pspec, gpointer data) {
    if (started || !gtk_window_is_active (GTK_WINDOW (window))) {
        return;
    }
    started = TRUE;
    g_timeout_add (500, start_cb, NULL);
}

#if GTK_CHECK_VERSION(4, 0, 0)
static GMainLoop *loop;

static void quit (void) {
    g_main_loop_quit (loop);
}
#else
static void quit (void) {
    gtk_main_quit ();
}
#endif

int main (int argc, char **argv) {
    dir = g_path_get_dirname (argv[0]);
    gboolean password = FALSE, two = FALSE;
    const char *module = "wayland";
    for (;;) {
        if (argc > 1 && !strcmp (argv[1], "--password")) {
            password = TRUE;
            argc--;
            argv++;
        } else if (argc > 1 && !strcmp (argv[1], "--two")) {
            two = TRUE;
            argc--;
            argv++;
        } else if (argc > 2 && !strcmp (argv[1], "--im")) {
            module = argv[2];
            argc -= 2;
            argv += 2;
        } else {
            break;
        }
    }
    // GTK's text-input module, whatever the session set up
    g_setenv ("GTK_IM_MODULE", module, TRUE);
    if (!strcmp (module, "wayland")) {
        // GTK 3 lists it in the system's module cache, not the session's
        g_unsetenv ("GTK_IM_MODULE_FILE");
    }
    tokens = argv + 1;
    tokensN = argc - 1;

#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_init ();
    loop = g_main_loop_new (NULL, FALSE);
    GtkWidget *window = gtk_window_new ();
    entry = gtk_entry_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append (GTK_BOX (box), entry);
    if (two) {
        entry2 = gtk_entry_new ();
        gtk_box_append (GTK_BOX (box), entry2);
    }
    gtk_window_set_child (GTK_WINDOW (window), box);
#else
    gtk_init (&argc, &argv);
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    entry = gtk_entry_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (box), entry);
    if (two) {
        entry2 = gtk_entry_new ();
        gtk_container_add (GTK_CONTAINER (box), entry2);
    }
    gtk_container_add (GTK_CONTAINER (window), box);
#endif
    // the preedit of either entry
    GtkWidget *entries[] = {entry, entry2};
    for (int i = 0; i < 2 && entries[i]; i++) {
#if GTK_CHECK_VERSION(4, 0, 0)
        g_signal_connect (gtk_editable_get_delegate (GTK_EDITABLE (entries[i])), "preedit-changed",
                          G_CALLBACK (on_preedit_changed), NULL);
#else
        g_signal_connect (entries[i], "preedit-changed", G_CALLBACK (on_preedit_changed), NULL);
#endif
    }
    GtkCssProvider *css = gtk_css_provider_new ();
    const char *style = "window { background: #0000ff; }";
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string (css, style);
#elif GTK_CHECK_VERSION(4, 0, 0)
    gtk_css_provider_load_from_data (css, style, -1);
#else
    gtk_css_provider_load_from_data (css, style, -1, NULL);
#endif
#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_style_context_add_provider_for_display (gdk_display_get_default (), GTK_STYLE_PROVIDER (css),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#else
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#endif
    if (password) {
        gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);
        gtk_entry_set_input_purpose (GTK_ENTRY (entry), GTK_INPUT_PURPOSE_PASSWORD);
    }
    g_signal_connect (window, "notify::is-active", G_CALLBACK (on_active), NULL);
    g_timeout_add_seconds (30, timeout_cb, NULL);

#if GTK_CHECK_VERSION(4, 0, 0)
    gtk_window_present (GTK_WINDOW (window));
    g_main_loop_run (loop);
#else
    gtk_widget_show_all (window);
    gtk_main ();
#endif
    return 0;
}
