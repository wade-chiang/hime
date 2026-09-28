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
 * A plain Xlib window typing through XIM (XMODIFIERS=@im=hime), as an X11
 * application without a HIME module (xterm, ...) on Xwayland, with real key
 * events from wl-type.
 *
 * Usage: xim-test TOKEN...
 *   TOKEN is a key for wl-type (see wl-type.c), or:
 *   @check        print the text typed
 *   @hook ACTION  make the daemon do what a mouse action does (see
 *                 text-input-test.c)
 *   The text is printed once more at the end.
 */

#include <locale.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

static char **tokens;
static int tokensN, next_token;
static char *dir;
static char text[4096];
static pid_t child;
// when to go on after the last child exited (ms, 0: not waiting)
static long resume_at;

static long now_ms (void) {
    struct timespec ts;
    clock_gettime (CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void print_state (void) {
    printf ("text=\"%s\"\n", text);
    fflush (stdout);
}

static void spawn (char **argv) {
    child = fork ();
    if (!child) {
        execv (argv[0], argv);
        perror (argv[0]);
        _exit (127);
    }
}

// Type the keys up to the next @check or @hook, or do that; FALSE when done
static int run_tokens (void) {
    while (next_token < tokensN && !strcmp (tokens[next_token], "@check")) {
        next_token++;
        print_state ();
    }
    if (next_token >= tokensN) {
        return 0;
    }

    char *argv[256];
    int argc = 0;
    char path[4096], message[256];
    if (!strcmp (tokens[next_token], "@hook") && next_token + 1 < tokensN) {
        const char *action = tokens[next_token + 1];
        const int has_arg = (!strcmp (action, "commit") || !strcmp (action, "key")) && next_token + 2 < tokensN;
        snprintf (path, sizeof (path), "%s/hime-client-test", dir);
        snprintf (message, sizeof (message), "#hime_test %s%s%s", action, has_arg ? " " : "",
                  has_arg ? tokens[next_token + 2] : "");
        argv[argc++] = path;
        argv[argc++] = "-m";
        argv[argc++] = message;
        next_token += has_arg ? 3 : 2;
    } else {
        snprintf (path, sizeof (path), "%s/wl-type", dir);
        argv[argc++] = path;
        while (next_token < tokensN && argc < 250 && strcmp (tokens[next_token], "@check") &&
               strcmp (tokens[next_token], "@hook")) {
            argv[argc++] = tokens[next_token++];
        }
    }
    argv[argc] = NULL;
    spawn (argv);
    return 1;
}

int main (int argc, char **argv) {
    dir = strdup (argv[0]);
    char *slash = strrchr (dir, '/');
    if (slash) {
        *slash = '\0';
    } else {
        strcpy (dir, ".");
    }
    tokens = argv + 1;
    tokensN = argc - 1;

    // HIME's XIM server, whatever the session set up
    setenv ("XMODIFIERS", "@im=hime", 1);
    setlocale (LC_ALL, "");
    if (!XSupportsLocale ()) {
        fprintf (stderr, "xim-test: the locale is not supported by Xlib\n");
        return 1;
    }
    XSetLocaleModifiers ("");

    Display *display = XOpenDisplay (NULL);
    if (!display) {
        fprintf (stderr, "xim-test: no X display\n");
        return 77;
    }
    // the daemon may serve XIM a moment after the X server came up
    XIM im = NULL;
    for (int i = 0; i < 100 && !(im = XOpenIM (display, NULL, NULL, NULL)); i++) {
        usleep (100000);
    }
    if (!im) {
        fprintf (stderr, "xim-test: no XIM server\n");
        return 1;
    }
    // the fixed position: no preedit or status in the application
    const Window window = XCreateSimpleWindow (display, DefaultRootWindow (display), 0, 0, 1280, 800, 0, 0,
                                               0x0000ff);
    XIC ic = XCreateIC (im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window,
                        XNFocusWindow, window, NULL);
    if (!ic) {
        fprintf (stderr, "xim-test: no input context\n");
        return 1;
    }
    long im_mask = 0;
    XGetICValues (ic, XNFilterEvents, &im_mask, NULL);
    XSelectInput (display, window, KeyPressMask | KeyReleaseMask | FocusChangeMask | im_mask);
    XMapWindow (display, window);
    XFlush (display);

    const long deadline = now_ms () + 30000;
    int started = 0, done = 0;
    while (!done) {
        if (now_ms () > deadline) {
            fprintf (stderr, "xim-test: timed out\n");
            print_state ();
            return 1;
        }
        while (XPending (display)) {
            XEvent event;
            XNextEvent (display, &event);
            if (XFilterEvent (&event, None)) {
                continue;
            }
            if (event.type == FocusIn) {
                XSetICFocus (ic);
                // the input method sets up its focus: give that a moment
                if (!started) {
                    started = 1;
                    resume_at = now_ms () + 500;
                }
            } else if (event.type == FocusOut) {
                XUnsetICFocus (ic);
            } else if (event.type == KeyPress) {
                char buf[256];
                KeySym keysym;
                Status status;
                const int len = Xutf8LookupString (ic, &event.xkey, buf, sizeof (buf) - 1, &keysym, &status);
                if ((status == XLookupChars || status == XLookupBoth) && len > 0) {
                    buf[len] = '\0';
                    // what a text field does with them
                    if (buf[0] == '\b') {
                        const size_t n = strlen (text);
                        if (n) {
                            size_t i = n - 1;
                            while (i > 0 && (text[i] & 0xc0) == 0x80) {
                                i--;
                            }
                            text[i] = '\0';
                        }
                    } else if ((unsigned char) buf[0] >= ' ' && strlen (text) + len < sizeof (text)) {
                        strcat (text, buf);
                    }
                }
            }
        }

        if (child > 0) {
            int status;
            if (waitpid (child, &status, WNOHANG) == child) {
                child = 0;
                if (WIFEXITED (status) && WEXITSTATUS (status) == 77) {
                    return 77;
                }
                // let the input method's commits arrive
                resume_at = now_ms () + 300;
            }
        } else if (resume_at && now_ms () >= resume_at) {
            resume_at = 0;
            if (!run_tokens ()) {
                done = 1;
            }
        }

        struct pollfd pfd = {ConnectionNumber (display), POLLIN, 0};
        poll (&pfd, 1, 20);
    }
    print_state ();
    XDestroyIC (ic);
    XCloseIM (im);
    XCloseDisplay (display);
    return 0;
}
