/* Copyright (C) 2009 Edward Der-Hua Liu, Hsin-Chu, Taiwan
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

#include <string.h>

#include "hime.h"

#include "im-srv.h"

Atom get_hime_addr_atom (Display *display) {
    return get_atom_by_name (display, "HIME_ADDR_ATOM_%s");
}

Atom get_hime_sockpath_atom (Display *display) {
    return get_atom_by_name (display, "HIME_SOCKPATH_ATOM_%s");
}

// The daemon's sockets live in $XDG_RUNTIME_DIR/hime, or /tmp/.hime-$USER
// when XDG_RUNTIME_DIR is unset.  dir is empty on failure.
static void get_sock_dir (char *dir, const int dirN) {
    dir[0] = '\0';

    const int uid = getuid ();

    const char *runtime_dir = getenv ("XDG_RUNTIME_DIR");
    if (runtime_dir && runtime_dir[0]) {
        snprintf (dir, dirN, "%s/hime", runtime_dir);
    } else {
        struct passwd *pw = getpwuid (uid);
        if (!pw) {
            return;
        }
        snprintf (dir, dirN, "%s/.hime-%s", g_get_tmp_dir (), pw->pw_name);
    }

    struct stat st;

    // dir doesn't exist, create one
    if (stat (dir, &st) == -1) {
        mkdir (dir, 0700);
    } else {
        if (st.st_uid != uid) {
            fprintf (stderr, "please check the permission of dir %s\n", dir);
            dir[0] = '\0';
        }
    }
}

// Socket of the daemon serving $DISPLAY: <dir>/<xim name>-<display>.socket,
// e.g. /run/user/1000/hime/hime-:0.socket.  Each X display has its own
// daemon, as its windows and XIM live there.  Without DISPLAY (a native
// Wayland client without Xwayland) this is the session's default socket,
// <dir>/<xim name>.socket, which the first daemon links to its own.
// The XIM name comes from XMODIFIERS=@im=...  outstr is empty on failure.
void get_hime_im_srv_sock_path (char *outstr, const int outstrN) {
    outstr[0] = '\0';

    const int DIR_NAME_SIZE = 128;
    char dir[DIR_NAME_SIZE];
    get_sock_dir (dir, sizeof (dir));
    if (!dir[0]) {
        return;
    }

    const char *display = getenv ("DISPLAY");
    if (!display || !display[0]) {
        snprintf (outstr, outstrN, "%s/%s.socket", dir, get_hime_xim_name ());
        return;
    }

    // host:display[.screen]; the screen does not matter
    char tdisplay[64];
    snprintf (tdisplay, sizeof (tdisplay), "%s", display);
    char *colon = strrchr (tdisplay, ':');
    char *dot = colon ? strchr (colon, '.') : NULL;
    if (dot) {
        *dot = '\0';
    }
    char *p;
    for (p = tdisplay; *p; p++) {
        if (*p == '/') {
            *p = '_';
        }
    }

    snprintf (outstr, outstrN, "%s/%s-%s.socket", dir, get_hime_xim_name (), tdisplay);
}

// the session's default socket, see get_hime_im_srv_sock_path
void get_hime_im_srv_default_sock_path (char *outstr, const int outstrN) {
    outstr[0] = '\0';

    const int DIR_NAME_SIZE = 128;
    char dir[DIR_NAME_SIZE];
    get_sock_dir (dir, sizeof (dir));
    if (!dir[0]) {
        return;
    }

    snprintf (outstr, outstrN, "%s/%s.socket", dir, get_hime_xim_name ());
}
