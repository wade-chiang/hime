# Wayland protocols

Protocols the daemon uses that `wayland-protocols` does not ship, copied
unchanged from wlroots (https://gitlab.freedesktop.org/wlroots/wlroots,
`protocol/`), under the MIT license in each file:

- `input-method-unstable-v2.xml`: the daemon as the seat's input method.
- `virtual-keyboard-unstable-v1.xml`: passing keys the input method does
  not handle on to the application.

The build generates their client code with `wayland-scanner`.
