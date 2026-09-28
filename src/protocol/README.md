# Wayland protocols

Protocols the daemon uses, copied unchanged.  From wlroots
(https://gitlab.freedesktop.org/wlroots/wlroots, `protocol/`), which
`wayland-protocols` does not ship, under the MIT license in each file:

- `input-method-unstable-v2.xml`: the daemon as the seat's input method.
- `virtual-keyboard-unstable-v1.xml`: passing keys the input method does
  not handle on to the application.

From wayland-protocols (https://gitlab.freedesktop.org/wayland/wayland-protocols,
`unstable/input-method/`, MIT license in the file):

- `input-method-unstable-v1.xml`: the daemon as KWin's input method.

The window that has the focus, so that each window has an input state of
its own (`src/wl-toplevel.c`):

- `wlr-foreign-toplevel-management-unstable-v1.xml`: wlroots compositors,
  niri, Hyprland, labwc; from wlr-protocols
  (https://gitlab.freedesktop.org/wlroots/wlr-protocols, `unstable/`, MIT
  license in the file).
- `plasma-window-management.xml`: KWin; from plasma-wayland-protocols
  (https://invent.kde.org/libraries/plasma-wayland-protocols,
  `src/protocols/`, LGPL-2.1-or-later).

The build generates their client code with `wayland-scanner`.

For the session tests only (`tests/session/wl-type`):

- `fake-input.xml`: KWin's key injection, from plasma-wayland-protocols
  (https://invent.kde.org/libraries/plasma-wayland-protocols, `src/protocols/`,
  LGPL-2.1-or-later): KWin offers no virtual keyboard protocol.
- `wlr-virtual-pointer-unstable-v1.xml`: mouse clicks on wlroots
  compositors (`tests/session/wl-click`), from wlr-protocols
  (https://gitlab.freedesktop.org/wlroots/wlr-protocols, `unstable/`,
  MIT license in the file).
