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

// XIM while the daemon runs on GDK's Wayland backend (niri, sway, KDE
// Plasma): X11 applications without a HIME module (xterm, old Motif or Tk
// programs, ...) on Xwayland reach HIME through XIM, as on X11.  The XIM
// server runs on a connection of its own to DISPLAY, whose events a GLib
// source reads.  HIME's windows stay Wayland windows; XIM clients' windows
// are X windows whose place on the screen is unknown, so the input window
// is at the fixed position for them (move_IC_in_win).  Mouse actions commit
// to the focused XIM client directly; virtual keyboard keys go through the
// engine, as for Wayland applications (Xwayland's XTest may need a
// permission from the compositor: KWin's does).  Connecting may wait for
// Xwayland to start (xwayland-satellite starts it on the first client), so
// it is done in a thread.  When the X server goes away (e.g. Xwayland
// restarted), the XIM server goes with it, and comes back on a new
// connection.

#include <sys/socket.h>

#include "hime.h"

#include "xim-wayland.h"

#if HIME_XIM_WAYLAND

#include <X11/Xlibint.h>

#include "IMdkit.h"
#include "Xi18n.h"

// from hime.c, eve.c, IC.c and IMdkit
extern Display *xim_dpy;
XIMS open_xim (Display *display, Window win);
extern char *output_buffer;
extern uint32_t output_bufferN;
IC *find_IC_of_client (ClientState *cs);
void forget_all_IC (void);
Xi18nClient *_Xi18nFindClient (Xi18n i18n_core, CARD16 connect_id);

typedef struct {
    GSource source;
    gpointer fd_tag;
} XSource;

static XIMS ims;
static GSource *source;
// the connection broke: only XCloseDisplay may use it now
static gboolean broken;
static guint retry_delay;
static gboolean told_open_failed;

#define RETRY_DELAY_MIN 2
#define RETRY_DELAY_MAX 60

static void xim_wayland_connect (void);
static void io_error_exit (Display *display, void *data);

// As GDK's X11 source: XPending flushes Xlib's buffer and reads what came
static gboolean xsource_prepare (GSource *s, gint *timeout) {
    *timeout = -1;
    return !broken && XPending (xim_dpy) > 0;
}

static gboolean xsource_check (GSource *s) {
    XSource *xs = (XSource *) s;
    return !broken && (g_source_query_unix_fd (s, xs->fd_tag) || XPending (xim_dpy) > 0);
}

// Has the X server closed the connection?  Xlib does not tell when the
// connection polls ready with nothing to read (and a round trip loops).
static gboolean hung_up (GSource *s) {
    const GIOCondition condition = g_source_query_unix_fd (s, ((XSource *) s)->fd_tag);
    if (condition & (G_IO_HUP | G_IO_ERR)) {
        return TRUE;
    }
    char c;
    return (condition & G_IO_IN) &&
           recv (ConnectionNumber (xim_dpy), &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0;
}

static gboolean xsource_dispatch (GSource *s, GSourceFunc callback, gpointer data) {
    while (!broken && XPending (xim_dpy)) {
        XEvent event;
        XNextEvent (xim_dpy, &event);
        if (event.type == MappingNotify) {
            // e.g. another keyboard layout, for the keys of XIM clients
            XRefreshKeyboardMapping (&event.xmapping);
        }
        // IMdkit's filters
        XFilterEvent (&event, None);
    }
    if (!broken && hung_up (s)) {
        // as Xlib does when it finds out, so that XCloseDisplay does not
        // talk to the server
        xim_dpy->flags |= XlibDisplayIOError;
        io_error_exit (xim_dpy, NULL);
    }
    // GLib dispatches the source as long as the hung up connection polls
    // ready: stop, and let disconnect_cb run
    return broken ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

static GSourceFuncs xsource_funcs = {
    xsource_prepare,
    xsource_check,
    xsource_dispatch,
    NULL,
};

static gboolean retry_cb (gpointer data) {
    xim_wayland_connect ();
    return G_SOURCE_REMOVE;
}

static void retry_later (void) {
    retry_delay = retry_delay ? MIN (retry_delay * 2, RETRY_DELAY_MAX) : RETRY_DELAY_MIN;
    g_timeout_add_seconds (retry_delay, retry_cb, NULL);
}

// Drop the broken connection, out of the Xlib call that found it broken
static gboolean disconnect_cb (gpointer data) {
    fprintf (stderr, "hime: the X server of XIM went away\n");
    if (source) {
        g_source_destroy (source);
        g_source_unref (source);
        source = NULL;
    }
    forget_all_IC ();
    // IMdkit's data stays: IMCloseIM would talk to the X server
    ims = NULL;
    Display *const display = xim_dpy;
    xim_dpy = NULL;
    broken = FALSE;
    XCloseDisplay (display);
    retry_delay = 0;
    retry_later ();
    return G_SOURCE_REMOVE;
}

// Xlib calls it when the connection breaks, instead of exiting
static void io_error_exit (Display *display, void *data) {
    if (!broken) {
        broken = TRUE;
        g_idle_add (disconnect_cb, NULL);
    }
}

static int io_error (Display *display) {
    return 0;
}

// e.g. BadWindow for the window of a client that went away
static int x_error (Display *display, XErrorEvent *event) {
    return 0;
}

// Serve XIM on DISPLAY, now open
static void start (Display *display) {
    // no other X connection in this process (GDK is on Wayland)
    XSetErrorHandler (x_error);
    XSetIOErrorHandler (io_error);
    XSetIOErrorExitHandler (display, io_error_exit, NULL);

    // the XIM server's window, as start_inmd_window's on X11
    const Window window = XCreateSimpleWindow (display, DefaultRootWindow (display), 0, 0, 1, 1, 0, 0, 0);
    xim_dpy = display;
    ims = open_xim (display, window);
    if (broken) {
        // the X server went away meanwhile: disconnect_cb tries again
        return;
    }
    if (!ims) {
        // e.g. a HIME being replaced has the name still
        if (!told_open_failed) {
            told_open_failed = TRUE;
            fprintf (stderr, "hime: trying again later\n");
        }
        xim_dpy = NULL;
        XCloseDisplay (display);
        retry_later ();
        return;
    }
    retry_delay = 0;
    told_open_failed = FALSE;

    source = g_source_new (&xsource_funcs, sizeof (XSource));
    ((XSource *) source)->fd_tag = g_source_add_unix_fd (source, ConnectionNumber (display), G_IO_IN | G_IO_HUP | G_IO_ERR);
    g_source_set_name (source, "hime XIM");
    g_source_attach (source, NULL);
    dbg ("xim-wayland: XIM on %s\n", DisplayString (display));
}

static gboolean opened_cb (gpointer data) {
    Display *display = data;
    if (!display) {
        retry_later ();
        return G_SOURCE_REMOVE;
    }
    start (display);
    return G_SOURCE_REMOVE;
}

// The display is used by the main thread only once it is open
static gpointer open_thread (gpointer data) {
    g_idle_add (opened_cb, XOpenDisplay (NULL));
    return NULL;
}

static void xim_wayland_connect (void) {
    g_thread_unref (g_thread_new ("hime-xim-open", open_thread, NULL));
}

void xim_wayland_init (void) {
    if (!getenv ("DISPLAY") || getenv ("HIME_NO_XIM")) {
        return;
    }
    xim_wayland_connect ();
}

static IC *focused_ic (void) {
    ClientState *const cs = hime_focused_client ();
    // not on the X11 backend (dpy), where it all goes as before
    if (dpy || !xim_dpy || broken || !ims || !cs || cs->b_hime_protocol) {
        return NULL;
    }
    IC *const ic = find_IC_of_client (cs);
    // it has the focus still (an unfocused one stays the last focused
    // client, e.g. when a window without text input gets the focus), and
    // its connection is there
    if (!ic || !ic->xim_focused || !_Xi18nFindClient (ims->protocol, ic->connect_id)) {
        return NULL;
    }
    return ic;
}

gboolean xim_wayland_ready (void) {
    return focused_ic () != NULL;
}

void xim_wayland_send (void) {
    IC *const ic = focused_ic ();
    if (!ic || !output_bufferN) {
        return;
    }
    char *text = output_buffer;
    XTextProperty tp;
    // a positive count: characters COMPOUND_TEXT has not, in some form
    if (Xutf8TextListToTextProperty (xim_dpy, &text, 1, XCompoundTextStyle, &tp) >= Success) {
        IMCommitStruct commit;
        memset (&commit, 0, sizeof (commit));
        commit.major_code = XIM_COMMIT;
        commit.connect_id = ic->connect_id;
        commit.icid = ic->id;
        commit.flag = XimLookupChars;
        commit.commit_string = (char *) tp.value;
        IMCommitString (ims, (XPointer) &commit);
        XFree (tp.value);
        XFlush (xim_dpy);
    }
    clear_output_buffer ();
}

#endif /* HIME_XIM_WAYLAND */
