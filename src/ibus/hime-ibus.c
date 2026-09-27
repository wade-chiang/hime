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

// HIME as an IBus engine.  GNOME (Mutter) offers no input method protocol:
// GNOME Shell passes the keys of text-input applications to IBus, and IBus
// to the engine of the input source chosen (Settings, Keyboard, Input
// Sources).  This engine is a HIME client like the GTK and Qt modules: it
// passes each key on to the HIME daemon (which it starts if needed) and
// commits the text and shows the preedit the daemon returns.  HIME shows
// its own windows; IBus's candidate window stays hidden.
//
// IBus starts it as hime.xml says (--ibus).  Each IBus input context gets
// an engine, with a daemon connection of its own; GNOME Shell uses one for
// all text-input applications.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <glib-unix.h>
#include <ibus.h>

#include "hime-im-client.h"

// IBus modifier bits above the X ones (IBUS_RELEASE_MASK, IBUS_SUPER_MASK,
// ...): not for the daemon
#define X_STATE_MASK 0xff

// key codes (evdev) of keys whose press HIME ate
#define KEYCODES_N 0x300

// HIME_IBUS_DEBUG=FILE: what goes through, appended to FILE (IBus drops
// the output of engines)
static FILE *debug;
#define DBG(...) (debug ? (fprintf (debug, "hime-ibus %.3f: ", g_get_monotonic_time () / 1e6), \
                           fprintf (debug, __VA_ARGS__), fflush (debug))                 \
                        : 0)

typedef struct {
    IBusEngine parent;

    HIME_client_handle *hime_ch;
    // the daemon connection watched for notifications (mouse actions), and
    // the one the preedit and notifications were asked for
    guint notify_source;
    int notify_fd;
    int flags_fd;
    guint pending_source;

    // the preedit the application has
    char *preedit;
    int preedit_cursor;

    // the field takes a password: the keys go to it as they are
    gboolean bypass;
    gboolean focused;

    guint8 eaten[KEYCODES_N];
} HimeEngine;

typedef struct {
    IBusEngineClass parent;
} HimeEngineClass;

G_DEFINE_TYPE (HimeEngine, hime_engine, IBUS_TYPE_ENGINE)

static void hime_engine_open (HimeEngine *engine) {
    if (engine->hime_ch) {
        return;
    }
    // no X display: the daemon's socket in $XDG_RUNTIME_DIR, the daemon
    // started if needed
    engine->hime_ch = hime_im_client_open (NULL);
}

// Show the daemon's preedit, if it changed
static void update_preedit (HimeEngine *engine) {
    char *str = NULL;
    HIME_PREEDIT_ATTR attr[HIME_PREEDIT_ATTR_MAX_N];
    int cursor = 0, sub_comp_len = 0;
    int attrN = 0;
    if (engine->hime_ch && !engine->bypass) {
        attrN = hime_im_client_get_preedit (engine->hime_ch, &str, attr, &cursor, &sub_comp_len);
    }
    if (!str) {
        str = strdup ("");
    }

    if (!g_strcmp0 (str, engine->preedit) && cursor == engine->preedit_cursor) {
        free (str);
        return;
    }
    g_free (engine->preedit);
    engine->preedit = g_strdup (str);
    engine->preedit_cursor = cursor;

    IBusText *text = ibus_text_new_from_string (str);
    const guint chars = g_utf8_strlen (str, -1);
    if (chars) {
        // HIME's attributes (underline, reverse: the phrase being edited)
        // count characters, as IBus's do
        ibus_text_append_attribute (text, IBUS_ATTR_TYPE_UNDERLINE, IBUS_ATTR_UNDERLINE_SINGLE, 0, chars);
        for (int i = 0; i < attrN; i++) {
            if (attr[i].flag & HIME_PREEDIT_ATTR_FLAG_REVERSE) {
                ibus_text_append_attribute (text, IBUS_ATTR_TYPE_BACKGROUND, 0xc0c0c0,
                                            attr[i].ofs0, attr[i].ofs1);
            }
        }
    }
    DBG ("preedit \"%s\" cursor %d\n", str, cursor);
    // dropped, not committed, when the focus moves on (as with the modules)
    ibus_engine_update_preedit_text_with_mode (IBUS_ENGINE (engine), text, MIN ((guint) cursor, chars),
                                               chars > 0, IBUS_ENGINE_PREEDIT_CLEAR);
    free (str);
}

static void commit (HimeEngine *engine, const char *str) {
    DBG ("commit \"%s\"\n", str);
    ibus_engine_commit_text (IBUS_ENGINE (engine), ibus_text_new_from_string (str));
    // the application drops its preedit with a commit (GNOME): show it again
    g_free (engine->preedit);
    engine->preedit = NULL;
}

// Notifications: text the daemon commits without a key event (mouse
// clicks on candidates, the symbol table, the virtual keyboard), and
// preedit changes.
static void handle_notifications (HimeEngine *engine) {
    char *text = NULL;
    while (engine->hime_ch && hime_im_client_read_notify (engine->hime_ch, &text)) {
        if (text) {
            commit (engine, text);
            free (text);
        }
        update_preedit (engine);
    }
}

static gboolean cb_pending (gpointer data) {
    HimeEngine *engine = data;
    engine->pending_source = 0;
    handle_notifications (engine);
    return G_SOURCE_REMOVE;
}

// After a request: notifications it took in before its reply are no longer
// on the connection, so the watch would not report them.
static void queue_pending_notifications (HimeEngine *engine) {
    if (engine->hime_ch && !engine->pending_source && hime_im_client_notify_pending (engine->hime_ch)) {
        engine->pending_source = g_idle_add (cb_pending, engine);
    }
}

static void watch_notifications (HimeEngine *engine);

static gboolean cb_notification (gint fd, GIOCondition condition, gpointer data) {
    HimeEngine *engine = data;
    DBG ("notification on fd %d (condition 0x%x)\n", fd, condition);
    handle_notifications (engine);
    // the connection closed or was reopened on another fd
    if (condition & (G_IO_HUP | G_IO_ERR) || !engine->hime_ch ||
        hime_im_client_get_fd (engine->hime_ch) != fd) {
        engine->notify_source = 0;
        engine->notify_fd = 0;
        watch_notifications (engine);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

// Watch the daemon connection, following reconnections.
static void watch_notifications (HimeEngine *engine) {
    if (!engine->hime_ch) {
        return;
    }
    const int fd = hime_im_client_get_fd (engine->hime_ch);
    // Ask for the preedit and notifications on each new connection: the
    // daemon may have been not up yet (IBus starts the engine with the
    // input source), or restarted
    if (fd > 0 && fd != engine->flags_fd) {
        engine->flags_fd = fd;
        int ret = 0;
        hime_im_client_set_flags (engine->hime_ch,
                                  FLAG_HIME_client_handle_use_preedit | FLAG_HIME_client_handle_screen_spot,
                                  &ret);
        hime_im_client_enable_notify (engine->hime_ch);
        DBG ("connected on fd %d, notify_ok %d\n", fd, hime_im_client_notify_ok (engine->hime_ch));
    }
    if (engine->notify_source && fd == engine->notify_fd) {
        return;
    }
    if (engine->notify_source) {
        g_source_remove (engine->notify_source);
        engine->notify_source = 0;
    }
    if (fd > 0 && hime_im_client_notify_ok (engine->hime_ch)) {
        engine->notify_source = g_unix_fd_add (fd, G_IO_IN | G_IO_HUP | G_IO_ERR, cb_notification, engine);
        engine->notify_fd = fd;
    }
}

// after each request to the daemon
static void after_request (HimeEngine *engine) {
    watch_notifications (engine);
    handle_notifications (engine);
    queue_pending_notifications (engine);
}

static gboolean hime_engine_process_key_event (IBusEngine *ibus_engine, guint keyval,
                                               guint keycode, guint modifiers) {
    HimeEngine *engine = (HimeEngine *) ibus_engine;
    if (engine->bypass) {
        return FALSE;
    }
    hime_engine_open (engine);
    if (!engine->hime_ch) {
        return FALSE;
    }

    // IBus key values are X keysyms, translated for the modifier state
    const uint32_t state = modifiers & X_STATE_MASK;
    char *text = NULL;
    gboolean eaten;
    if (modifiers & IBUS_RELEASE_MASK) {
        eaten = hime_im_client_forward_key_release (engine->hime_ch, keyval, state, &text);
    } else {
        eaten = hime_im_client_forward_key_press (engine->hime_ch, keyval, state, &text);
    }

    DBG ("key 0x%x %s state 0x%x: %s\n", keyval, modifiers & IBUS_RELEASE_MASK ? "release" : "press",
         state, eaten ? "eaten" : "passed on");
    // notifications that came before this reply are older than its text
    handle_notifications (engine);
    if (text) {
        commit (engine, text);
        free (text);
    }
    update_preedit (engine);
    after_request (engine);
    const gboolean release = modifiers & IBUS_RELEASE_MASK;
    if (keycode < KEYCODES_N) {
        if (release) {
            engine->eaten[keycode] = FALSE;
        } else if (eaten) {
            engine->eaten[keycode] = TRUE;
        } else if (engine->eaten[keycode]) {
            // A repeat of a held key HIME ate (e.g. BackSpace once the keys
            // typed are gone): GNOME does not pass repeats of a key on to
            // the application, which saw no press.  Pass it on as a key of
            // its own.
            ibus_engine_forward_key_event (ibus_engine, keyval, keycode, modifiers);
            ibus_engine_forward_key_event (ibus_engine, keyval, keycode, modifiers | IBUS_RELEASE_MASK);
            return TRUE;
        }
    }
    // not eaten: IBus passes it on to the application (GNOME Shell
    // forwards it)
    return eaten;
}

static void hime_engine_focus_in (IBusEngine *ibus_engine) {
    HimeEngine *engine = (HimeEngine *) ibus_engine;
    DBG ("focus in\n");
    engine->focused = TRUE;
    if (engine->bypass) {
        return;
    }
    hime_engine_open (engine);
    if (engine->hime_ch) {
        hime_im_client_focus_in (engine->hime_ch);
    }
    after_request (engine);
}

// What was typed but not committed is dropped: the focus may already be in
// another application, which a commit now would reach
static void hime_engine_focus_out (IBusEngine *ibus_engine) {
    HimeEngine *engine = (HimeEngine *) ibus_engine;
    DBG ("focus out\n");
    engine->focused = FALSE;
    if (engine->hime_ch) {
        hime_im_client_reset (engine->hime_ch);
        hime_im_client_focus_out (engine->hime_ch);
    }
    g_free (engine->preedit);
    engine->preedit = NULL;
    after_request (engine);
}

// Where the text cursor is, in screen coordinates (GNOME's stage): HIME's
// window goes below it in OverSpot
static void hime_engine_set_cursor_location (IBusEngine *ibus_engine, gint x, gint y, gint w, gint h) {
    HimeEngine *engine = (HimeEngine *) ibus_engine;
    DBG ("cursor %d,%d %dx%d\n", x, y, w, h);
    if (!engine->hime_ch || engine->bypass) {
        return;
    }
    hime_im_client_set_cursor_location (engine->hime_ch, x, y + h);
    after_request (engine);
}

static void hime_engine_reset (IBusEngine *ibus_engine) {
    HimeEngine *engine = (HimeEngine *) ibus_engine;
    if (engine->hime_ch) {
        hime_im_client_reset (engine->hime_ch);
    }
    update_preedit (engine);
    after_request (engine);
}

// the input source switched away: as a focus out
static void hime_engine_disable (IBusEngine *ibus_engine) {
    hime_engine_focus_out (ibus_engine);
}

static void hime_engine_set_content_type (IBusEngine *ibus_engine, guint purpose, guint hints) {
    HimeEngine *engine = (HimeEngine *) ibus_engine;
    const gboolean bypass = purpose == IBUS_INPUT_PURPOSE_PASSWORD || purpose == IBUS_INPUT_PURPOSE_PIN;
    if (bypass == engine->bypass) {
        return;
    }
    // HIME's window goes away for a password, and comes back after it
    if (engine->focused && bypass && engine->hime_ch) {
        hime_im_client_reset (engine->hime_ch);
        hime_im_client_focus_out (engine->hime_ch);
    }
    engine->bypass = bypass;
    if (engine->focused) {
        if (!bypass) {
            hime_engine_focus_in (ibus_engine);
        } else {
            update_preedit (engine);
        }
    }
}

static void hime_engine_destroy (IBusObject *object) {
    HimeEngine *engine = (HimeEngine *) object;
    if (engine->notify_source) {
        g_source_remove (engine->notify_source);
        engine->notify_source = 0;
    }
    if (engine->pending_source) {
        g_source_remove (engine->pending_source);
        engine->pending_source = 0;
    }
    if (engine->hime_ch) {
        hime_im_client_close (engine->hime_ch);
        engine->hime_ch = NULL;
    }
    g_free (engine->preedit);
    engine->preedit = NULL;
    IBUS_OBJECT_CLASS (hime_engine_parent_class)->destroy (object);
}

static void hime_engine_class_init (HimeEngineClass *klass) {
    IBusEngineClass *engine_class = IBUS_ENGINE_CLASS (klass);
    engine_class->process_key_event = hime_engine_process_key_event;
    engine_class->focus_in = hime_engine_focus_in;
    engine_class->focus_out = hime_engine_focus_out;
    engine_class->reset = hime_engine_reset;
    engine_class->disable = hime_engine_disable;
    engine_class->set_content_type = hime_engine_set_content_type;
    engine_class->set_cursor_location = hime_engine_set_cursor_location;
    IBUS_OBJECT_CLASS (klass)->destroy = hime_engine_destroy;
}

static void hime_engine_init (HimeEngine *engine) {
}

static void bus_disconnected (IBusBus *bus, gpointer data) {
    ibus_quit ();
}

int main (int argc, char **argv) {
    if (argc < 2 || strcmp (argv[1], "--ibus")) {
        fprintf (stderr, "hime-ibus: IBus starts it (see %s)\n", "hime.xml");
        return 1;
    }

    if (getenv ("HIME_IBUS_DEBUG")) {
        debug = fopen (getenv ("HIME_IBUS_DEBUG"), "a");
    }
    ibus_init ();
    IBusBus *bus = ibus_bus_new ();
    if (!ibus_bus_is_connected (bus)) {
        fprintf (stderr, "hime-ibus: cannot connect to IBus\n");
        return 1;
    }
    g_signal_connect (bus, "disconnected", G_CALLBACK (bus_disconnected), NULL);

    IBusFactory *factory = ibus_factory_new (ibus_bus_get_connection (bus));
    ibus_factory_add_engine (factory, "hime", hime_engine_get_type ());
    ibus_bus_request_name (bus, "org.freedesktop.IBus.Hime", 0);

    ibus_main ();
    return 0;
}
