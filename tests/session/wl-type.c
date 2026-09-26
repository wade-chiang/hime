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
 * Types keys on the Wayland seat through a virtual keyboard, as a physical
 * keyboard would (the compositor sends them to the input method's grab).
 *
 * Usage: wl-type KEY...
 *   KEY is a single character of the "us" layout or one of <space>
 *   <enter> <bs> <esc> <shift> (Shift pressed and released alone),
 *   optionally prefixed by S- (Shift) and/or C- (Control).  @hold KEY
 *   holds KEY down for a second (key repeat).
 */

// memfd_create
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"

// evdev key codes
#define KEY_ESC 1
#define KEY_BACKSPACE 14
#define KEY_ENTER 28
#define KEY_LEFTCTRL 29
#define KEY_LEFTSHIFT 42
#define KEY_SPACE 57

static struct wl_display *display;
static struct wl_seat *seat;
static struct zwp_virtual_keyboard_manager_v1 *manager;
static struct zwp_virtual_keyboard_v1 *keyboard;
static struct xkb_keymap *keymap;
static uint32_t mods;

static void registry_global (void *data, struct wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t version) {
    if (!strcmp (interface, wl_seat_interface.name) && !seat) {
        seat = wl_registry_bind (registry, name, &wl_seat_interface, 1);
    } else if (!strcmp (interface, zwp_virtual_keyboard_manager_v1_interface.name)) {
        manager = wl_registry_bind (registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
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

static void sleep_ms (int ms) {
    wl_display_roundtrip (display);
    usleep (ms * 1000);
}

static void key (uint32_t code, int press) {
    zwp_virtual_keyboard_v1_key (keyboard, now_ms (), code,
                                 press ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    uint32_t mod = 0;
    if (code == KEY_LEFTSHIFT) {
        mod = 1 << xkb_keymap_mod_get_index (keymap, XKB_MOD_NAME_SHIFT);
    } else if (code == KEY_LEFTCTRL) {
        mod = 1 << xkb_keymap_mod_get_index (keymap, XKB_MOD_NAME_CTRL);
    }
    if (mod) {
        mods = press ? mods | mod : mods & ~mod;
        zwp_virtual_keyboard_v1_modifiers (keyboard, mods, 0, 0, 0);
    }
}

// the key code and Shift level typing the character c
static int find_char (char c, uint32_t *code, int *shift) {
    const xkb_keysym_t sym = xkb_utf32_to_keysym ((unsigned char) c);
    for (xkb_keycode_t kc = xkb_keymap_min_keycode (keymap); kc <= xkb_keymap_max_keycode (keymap); kc++) {
        for (int level = 0; level < 2; level++) {
            const xkb_keysym_t *syms;
            if (xkb_keymap_key_get_syms_by_level (keymap, kc, 0, level, &syms) == 1 && syms[0] == sym) {
                *code = kc - 8;
                *shift = level;
                return 1;
            }
        }
    }
    return 0;
}

static int parse_key (const char *tok, uint32_t *code, int *shift, int *ctrl) {
    static const struct {
        const char *name;
        uint32_t code;
    } named_keys[] = {
        {"<space>", KEY_SPACE},
        {"<enter>", KEY_ENTER},
        {"<bs>", KEY_BACKSPACE},
        {"<esc>", KEY_ESC},
        {"<shift>", KEY_LEFTSHIFT},
    };

    *shift = *ctrl = 0;
    for (;;) {
        if (!strncmp (tok, "S-", 2) && tok[2]) {
            *shift = 1;
        } else if (!strncmp (tok, "C-", 2) && tok[2]) {
            *ctrl = 1;
        } else {
            break;
        }
        tok += 2;
    }
    for (size_t i = 0; i < sizeof (named_keys) / sizeof (named_keys[0]); i++) {
        if (!strcmp (tok, named_keys[i].name)) {
            *code = named_keys[i].code;
            return 1;
        }
    }
    int level = 0;
    if (strlen (tok) != 1 || !find_char (tok[0], code, &level)) {
        return 0;
    }
    *shift |= level;
    return 1;
}

static void type (const char *tok, int hold_ms) {
    uint32_t code;
    int shift, ctrl;
    if (!parse_key (tok, &code, &shift, &ctrl)) {
        fprintf (stderr, "wl-type: bad key: %s\n", tok);
        exit (2);
    }
    if (ctrl) {
        key (KEY_LEFTCTRL, 1);
    }
    if (shift && code != KEY_LEFTSHIFT) {
        key (KEY_LEFTSHIFT, 1);
    }
    key (code, 1);
    if (hold_ms) {
        sleep_ms (hold_ms);
    }
    key (code, 0);
    if (shift && code != KEY_LEFTSHIFT) {
        key (KEY_LEFTSHIFT, 0);
    }
    if (ctrl) {
        key (KEY_LEFTCTRL, 0);
    }
    // one key at a time, as a person types
    sleep_ms (50);
}

int main (int argc, char **argv) {
    display = wl_display_connect (NULL);
    if (!display) {
        fprintf (stderr, "wl-type: no Wayland display\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry (display);
    wl_registry_add_listener (registry, &registry_listener, NULL);
    wl_display_roundtrip (display);
    if (!seat || !manager) {
        fprintf (stderr, "wl-type: no virtual keyboard support\n");
        return 1;
    }
    keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard (manager, seat);

    struct xkb_context *context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
    const struct xkb_rule_names names = {.layout = "us"};
    keymap = xkb_keymap_new_from_names (context, &names, 0);
    char *text = keymap ? xkb_keymap_get_as_string (keymap, XKB_KEYMAP_FORMAT_TEXT_V1) : NULL;
    if (!text) {
        fprintf (stderr, "wl-type: no keymap\n");
        return 1;
    }
    const size_t size = strlen (text) + 1;
    const int fd = memfd_create ("wl-type-keymap", MFD_CLOEXEC);
    if (fd < 0 || write (fd, text, size) != (ssize_t) size) {
        perror ("wl-type: keymap");
        return 1;
    }
    zwp_virtual_keyboard_v1_keymap (keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
    close (fd);
    free (text);
    wl_display_roundtrip (display);

    for (int i = 1; i < argc; i++) {
        if (!strcmp (argv[i], "@hold") && i + 1 < argc) {
            type (argv[++i], 1000);
        } else {
            type (argv[i], 0);
        }
    }

    zwp_virtual_keyboard_v1_destroy (keyboard);
    wl_display_roundtrip (display);
    wl_display_disconnect (display);
    return 0;
}
