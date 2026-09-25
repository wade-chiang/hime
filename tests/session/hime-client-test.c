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
 * A HIME client without an X display, as a native Wayland application
 * would be.  Connects through libhime-im-client, types the keys given on
 * the command line and prints what the daemon commits.
 *
 * Usage: hime-client-test [-m MESSAGE] KEY...
 *   KEY is a single printable character or one of <space> <enter> <bs>
 *   <esc>, optionally prefixed by S- (Shift) and/or C- (Control).  @1 and @2 move the focus to the first or a second client
 *   connection (two text fields, both without an X window); @new closes
 *   the focused connection and focuses a new one in its place (an
 *   application quits, another starts).  -m sends a daemon message (as
 *   hime-setup does) first.
 * Exit status: 0 if connected, 1 if no daemon could be reached.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "hime-im-client.h"

static KeySym parse_key (const char *tok, uint32_t *state) {
    static const struct {
        const char *name;
        KeySym sym;
    } named_keys[] = {
        {"<space>", XK_space},
        {"<enter>", XK_Return},
        {"<bs>", XK_BackSpace},
        {"<esc>", XK_Escape},
    };

    *state = 0;
    for (;;) {
        if (!strncmp (tok, "S-", 2) && tok[2])
            *state |= ShiftMask;
        else if (!strncmp (tok, "C-", 2) && tok[2])
            *state |= ControlMask;
        else
            break;
        tok += 2;
    }

    size_t i;
    for (i = 0; i < sizeof (named_keys) / sizeof (named_keys[0]); i++)
        if (!strcmp (tok, named_keys[i].name))
            return named_keys[i].sym;

    if (strlen (tok) == 1 && tok[0] > ' ' && tok[0] < 127) {
        // X reports Shift+a as XK_A
        return (*state & ShiftMask) && tok[0] >= 'a' && tok[0] <= 'z' ? (KeySym) (tok[0] - 'a' + 'A') : (KeySym) tok[0];
    }

    return NoSymbol;
}

static HIME_client_handle *open_client (void) {
    HIME_client_handle *handle = hime_im_client_open (NULL);
    if (!handle || handle->fd <= 0) {
        fprintf (stderr, "cannot connect to hime\n");
        exit (1);
    }
    return handle;
}

int main (int argc, char **argv) {
    HIME_client_handle *clients[2] = {open_client (), NULL};
    HIME_client_handle *handle = clients[0];

    int argi = 1;
    if (argc > 2 && !strcmp (argv[1], "-m")) {
        hime_im_client_send_message (handle, argv[2]);
        argi = 3;
    }

    hime_im_client_focus_in (handle);

    for (; argi < argc; argi++) {
        if (!strcmp (argv[argi], "@new")) {
            const int i = handle == clients[0] ? 0 : 1;
            hime_im_client_close (handle);
            clients[i] = handle = open_client ();
            hime_im_client_focus_in (handle);
            printf ("%s\n", argv[argi]);
            continue;
        }

        if (!strcmp (argv[argi], "@1") || !strcmp (argv[argi], "@2")) {
            const int i = argv[argi][1] - '1';
            if (!clients[i])
                clients[i] = open_client ();
            hime_im_client_focus_out (handle);
            handle = clients[i];
            hime_im_client_focus_in (handle);
            printf ("%s\n", argv[argi]);
            continue;
        }

        uint32_t state;
        KeySym key = parse_key (argv[argi], &state);
        if (key == NoSymbol) {
            fprintf (stderr, "bad key: %s\n", argv[argi]);
            return 2;
        }

        char *commit = NULL;
        int eaten = hime_im_client_forward_key_press (handle, key, state, &commit);
        char *release = NULL;
        hime_im_client_forward_key_release (handle, key, state, &release);

        printf ("%-8s %s", argv[argi], eaten ? "eat " : "pass");
        if (commit && commit[0])
            printf (" commit=\"%s\"", commit);
        printf ("\n");

        free (commit);
        free (release);
    }

    hime_im_client_focus_out (handle);
    hime_im_client_close (clients[0]);
    hime_im_client_close (clients[1]);
    return 0;
}
