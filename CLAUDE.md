# HIME

HIME is a Chinese input method framework for Linux/Unix, written in C with
GTK. The daemon (`src/hime`) runs the input method engines and draws its
own windows; applications reach it through XIM or the GTK/Qt IM modules.

## Build

```bash
autoreconf -fi            # the repo lacks install-sh/compile/missing
./configure --with-gtk=3.0 --prefix=/usr --disable-system-tray
make -j$(nproc)
make check                # gtab characterization tests
make check-session        # end-to-end tests in a headless mutter session
```

- Build the daemon against GTK 3. The configure default is GTK 2, which has
  no Wayland backend.
- `--disable-system-tray`: under GTK 3 the legacy `GtkStatusIcon` tray does
  not compile (`hime-gtk-compatible.h` aliases it to `GObject`). The
  appindicator tray is the one that matters for Wayland.
- Add `--disable-qt5-immodule` when Qt 5 is not installed; Qt 6 is
  auto-detected.
- `./configure` and `autoreconf` rewrite the tracked `configure`; leave it
  out of commits unless `configure.ac` changed.
- The Makefiles are automake, but `all`/`install`/`clean` are hand-written
  rules in each `Makefile.am`.

## Tests

`tests/gtab/` locks down the observable behavior of the table engine
(gtab: Boshiamy, Dayi, Array, Cangjie, ...):

- `harness.c` links the real key dispatch (`src/eve.c`) and engine
  (`src/gtab*.c`), replacing `src/win-gtab.c` and `src/hime.c` with
  recorders. It prints, after every key, the input area, candidates,
  committed text, phrase buffer, pre-select row and bells.
- `cases/*.keys` are key scripts; `cases/*.expected` are the golden
  transcripts. Script and directive syntax are documented at the top of
  `harness.c` and `run-tests.sh`.
- `tables/test-liu.cin` is a made-up table in Boshiamy's shape
  (space_style 1). The real Boshiamy table is copyrighted and never enters
  the repo; put it and cases using it in the git-ignored `tests/gtab/local/`
  and run `tests/gtab/run-tests.sh tests/gtab/local/*.keys`.

Session tests drive mouse actions through test hooks
(`HIME_TEST_HOOKS`, `hime_test_hook ()` in `src/eve.c`, used by
`tests/session/notify-check.sh`).

`tests/session/` runs the real daemon on the Xwayland of a private
headless mutter (`run-session.sh`, needs mutter and dbus-run-session) and
types through `libhime-im-client` from a client with no X display, as a
native Wayland application. `cases/*.keys` / `*.expected` work like the
gtab ones. Use `run-session.sh CMD` to run anything else in such a
session (e.g. a GTK app with `GTK_IM_MODULE=hime`).

Run `make check` after any change to the engine or key handling, and
`make check-session` after changes to the daemon, `src/im-client/` or the
IM modules. Only run
`run-tests.sh --update` when a behavior change is intended, and show the
diff of the `.expected` files when reporting it.

Known quirks the tests lock in (do not "fix" them silently): in space_style
1 a selection key picks the 2nd candidate onward and space always commits
the first, even with more pages; a selection key with no candidate clears
the input but leaves the candidate row shown.

## Code style

- clang-format with the repo's `.clang-format`; `make clang-format` runs
  `scripts/format_code.py` (expects `clang-format-14`).
- New files carry the LGPL-2.1 header used in `src/*.c`.
- Commit subjects follow conventional commits:
  `fix(gtab): ...`, `refactor: ...`, `build: ...`, `test: ...`, `docs: ...`.

## Architecture map

- `src/hime.c`: daemon main, X11 setup, XIM (`src/IMdkit/`, `src/IC.c`).
- `src/eve.c`: key dispatch (`ProcessKeyPress`), client state, commit
  buffer (`send_text`), input method switching (`init_in_method`).
- `src/im-srv.c`, `src/im-dispatch.c`: socket server for the IM modules.
- `src/im-client/`: client library used by the GTK/Qt modules and the
  tools. It connects to the daemon's socket at
  `$XDG_RUNTIME_DIR/hime/<name>.socket` (`get_hime_im_srv_sock_path` in
  `src/im-addr.c`), starting the daemon if needed; only the TCP remote
  mode still finds the daemon through X11.
- `src/gtab*.c`: table engine; `src/win-gtab.c` is its window.
- `src/pho*.c`, `src/tsin*.c`: Zhuyin and phrase (tsin) engines.
- `src/modules/`: loadable modules (Anthy, Chewing, intcode).
- `src/gtk-im/`, `src/gtk3-im/`, `src/gtk4-im/`, `src/qt5-im/`,
  `src/qt6-im/`: IM modules. The three GTK modules share the sources in
  `src/gtk-im/` (the other directories hold symlinks); GTK 4 differences
  are `#if GTK_CHECK_VERSION(4, 0, 0)` branches. GTK 4 loads
  `libim-hime.so` as a GIO module from `$GTK_PATH/4.0.0/immodules`.

## Wayland plan

The goal is native Wayland support on all mainstream compositors while
keeping HIME's own UI and typing feel. Phases:

1. Done: daemon and IM modules work without X11: socket path under
   `$XDG_RUNTIME_DIR`, keysym-based key events, a GTK 4 IM module.
2. Mostly done: where the compositor supports wlr-layer-shell, the daemon
   runs on GDK's Wayland backend (`choose_backend ()` in `src/hime.c`,
   overridable with `HIME_BACKEND=x11|wayland`) and its windows are
   layer surfaces (`hime_window_init/move/get_position` in
   `src/win-common.c`, anchored top-left, placed by margins). Mouse
   actions (candidate clicks, symbol table, virtual keyboard) reach
   clients as notifications: unsolicited `HIME_NOTIFY_MAGIC` messages on
   UNIX connections of clients that asked for them (see
   `hime-protocol.h`, `hime_notify_send ()` in `src/im-dispatch.c`, the
   modules' fd watches). They go to the connection that last sent
   focus-in or a key (fields of one X window share a ClientState, and a
   new connection becomes current_CS while it is set up, so neither
   current_CS nor its owner is right). XTest remains for XIM and old
   clients, and for the virtual keyboard on X11 applications. Modules
   must hand out notifications taken in while waiting for other replies
   (`hime_im_client_notify_pending ()`), before the key's own text. Left:
   the input method menu; virtual keyboard modifiers and keys the engine
   does not take in Wayland applications. Modules get the
   window helpers through `HIME_module_main_functions` (modules must use
   it: the daemon exports no symbols). Session tests cover the intcode,
   chewing and anthy modules (`@method`). The intcode module logs two
   pre-existing Gtk-CRITICALs (present/show on a NULL window) on X11 too.
3. `zwp_input_method_v2` frontend (niri, sway, Hyprland, labwc/Xfce).
   3a done: `src/wl-im.c`, on GDK's own connection (the compositor only
   lets the IM's own virtual keyboard past the grab). It grabs the
   keyboard only while active (a grab gets all keys, also those of
   applications with a HIME module), feeds ProcessKeyPress/Release,
   commits `output_buffer` and the preedit with every change (text-input
   state is double-buffered: an unsent preedit is cleared), forwards
   unhandled keys through a `zwp_virtual_keyboard_v1` with the grab's
   keymap, repeats held keys it handles, bypasses password/PIN fields, and
   plugs into `hime_notify_ready/send` for mouse actions. All text-input
   applications share one static ClientState. Session tests type real
   keys with `tests/session/wl-type` into `*-text-input-test` in headless
   sway. 3b done: in OverSpot with a text-input field focused, win_gtab,
   win0 and win_pho are input popups (`hime_input_window_new ()`,
   `HimePopupWindow` in `src/wl-im.c`). A GTK window cannot drop
   gtk-layer-shell, so `refresh_input_window ()` in `src/eve.c` creates
   the focused client's window again when its kind no longer fits (on
   show_in_win, init_in_method, and the `change font size` reload that
   hime-setup sends). win1, the symbol table, the gtab same-pho window
   and module windows stay at the fixed position; win1 is placed relative
   to the fixed position when win0 is a popup. Session tests take
   screenshots through `@exec popup-shot.sh` and switch styles with
   `set-style.sh`; `@method pho|tsin` exercises those windows.
4. Done (awaiting a real Plasma test): KWin as input method through
   `zwp_input_method_v1`. `src/wl-im.c` is the protocol-independent core
   behind `WlImProtocol` (`src/wl-im-private.h`), with `src/wl-im-v2.c`
   and `src/wl-im-v1.c`. KWin starts the IM itself (kwinrc
   `[Wayland] InputMethod=`, `menu/hime-wayland.desktop` with
   `X-KDE-Wayland-VirtualKeyboard`) and hands it `WAYLAND_SOCKET`, the only
   connection offering the protocol: `hime_launched_by_compositor ()`
   skips the layer-shell probe, stays in the foreground, and replaces a
   running daemon (SO_PEERCRED + SIGTERM in `src/im-srv.c`). v1 comes as a
   context per field (grab, commits, preedit, forwarded keys and echoed
   modifiers go through it, with its commit_state serial); the OverSpot
   window is an overlay input panel (one at a time). `@compositor kwin`
   runs session tests in a headless KWin; `kwin-*` cases mirror
   `wl-im-*`.
5. Done (GNOME/Mutter, which offers no input method protocol: GNOME
   Shell passes text-input keys to IBus): `src/ibus/hime-ibus`, an IBus
   engine that is a HIME client like the modules (component
   `src/ibus/hime.xml`, `--enable-ibus-engine`). It asks for the preedit,
   notifications and `FLAG_HIME_client_handle_screen_spot` on each new
   connection (IBus may start it before the daemon); screen-spot clients
   get their window at the cursor in screen coordinates
   (`move_IC_in_win`), right on Xwayland, where the daemon runs on GNOME.
   It uses has-focus-id (a custom create-engine) to treat IBus's "fake"
   context as no focus, sends one focus out per focus, ignores the
   (0,0,0,0) cursor GNOME Shell resets on focus out, and passes on
   repeats of keys whose press it ate (GNOME drops them otherwise).
   `@compositor gnome` runs session tests in a headless GNOME Shell;
   `gnome-*` cases mirror `wl-im-*`.

6. fcitx5 parity.  Done: each window of the text-input applications
   has its own ClientState (Chinese/English, input method), as X
   clients' windows do: `src/wl-toplevel.c` follows the focused window
   (wlr-foreign-toplevel-management, or plasma-window-management on
   KWin, bound at version 17) and queues window events into the
   `src/wl-im.c` queue; states are per window number, 0 for unknown
   windows, freed on close. On GNOME only per application:
   `src/ibus/gnome-app-monitor.c` reads GNOME Shell's replies to
   xdg-desktop-portal-gnome's GetRunningApplications on a D-Bus monitor
   connection (as fcitx5 5.1.23), and hime-ibus keeps a daemon
   connection per application (`use_app ()`). `hime-single-state`
   still shares one state. Tests: `*-windows*` cases open a second
   window with `@exec gtk3-text-input-test:KEYS`. Done: XIM while the
   daemon runs on the Wayland backend (`src/xim-wayland.c`): its own
   Xlib connection to DISPLAY (`xim_dpy`, which is `dpy` on the X11
   backend), opened in a thread (xwayland-satellite starts Xwayland on
   the first client), read by a GSource; libX11 >= 1.7
   (`HIME_XIM_WAYLAND`); ICs use the fixed position; mouse
   actions commit with IMCommitString (`xim_wayland_ready/send`, part
   of `hime_notify_ready/send`); virtual keyboard keys go through the
   engine; when the X server goes away the ICs are dropped and it
   reconnects with backoff. `HIME_NO_XIM=1` turns it off. Tests:
   `@xwayland @x11` with `xim-test` (an Xlib XIM client) on sway and
   KWin, `xim-restart.sh` kills the session's Xwayland.

Both input window styles must work on Wayland and stay switchable at
runtime from hime-setup, as on X11 (`hime-input-style`, applied by
`move_in_win()` in `src/eve.c`):

- Root (fixed position, `hime-root-x/y`): anchor the window with
  `wlr-layer-shell` where available (niri, KWin, wlroots); build this
  first, it needs no cursor coordinates.
- OverSpot (follows the text cursor, the default): let the compositor
  place the window relative to the cursor rectangle (input-method-v2
  popup surface, input-method-v1 input panel, IBus on GNOME).

Every phase must keep `make check` green without `--update`.

Known gaps after phase 1 (from review, not fixed yet):

- GTK 4 has no Mod2-Mod5 modifier masks, so AltGr (Mod5) and NumLock
  (Mod2) never reach the daemon from GTK 4 applications.
- The daemon auto-start path (`start_hime_server` in
  `src/im-client/hime-im-client.c`) is not covered by tests: the session
  tests disable it and the daemon path is fixed at build time.
- Socket paths longer than `sun_path` (108 bytes) are silently
  truncated, the same way on both sides.
- The GTK modules assume GDK was built with the X11 backend.
- Qt 5 on Wayland is untested (qt5-wayland is not installed here).

Open items after phase 6:

- A real Plasma test must check that KWin offers
  plasma-window-management to the daemon it starts
  (`X-KDE-Wayland-Interfaces` in `menu/hime-wayland.desktop`): the
  session tests disable KWin's permission checks
  (`KWIN_WAYLAND_NO_PERMISSION_CHECKS`), so they cannot tell. Without
  it, all windows share one input state there, silently.
- `module-symbols-gnome` failed once in a full `make check-session` run
  and passed six times alone; the cause is unknown (it uses the IM
  module and mouse clicks, not hime-ibus).

## Gotchas

- Do not add `-DGTK_DISABLE_DEPRECATED` to GTK 3 builds: HIME still calls
  deprecated GTK 3 APIs, and hiding them turns the calls into implicit
  declarations (pointer truncation at runtime).
- `hime-cin2gtab` truncates paths at 64 characters; run it from the
  table's directory with a relative name.
- `hime-tsin2gtab-phrase` reads `tsin32` only from `~/.config/hime`, and
  the daemon runs helper tools from the hard-coded `HIME_BIN_DIR`.
- `src/im-client/` compiles its own copies of `src/*.c` files; the src
  Makefile always recurses into it so they get rebuilt. Header
  dependencies are tracked with `-MMD` (`*.d` files); a build from
  before that change needs one `make clean`.
- Packages are linked with `-z now` (Arch makepkg default): every
  undefined symbol of a module and of `libhime-im-client` must resolve
  in the host application. The library is loaded into GTK 4 and Qt apps,
  so it must not use GTK 3; session tests run with `LD_BIND_NOW=1` to
  catch this. Check with `ldd -r` on the modules.
- The daemon must daemonize before `gtk_init()`: forking after GLib
  started its GDBus worker threads hangs the child.
- GNOME sets `XMODIFIERS=@im=ibus` and `QT_IM_MODULE=ibus` in the
  session; HIME only takes an `@im=` name starting with `hime`.
- gnome-session also sets `QT_IM_MODULES=wayland;ibus` in the systemd
  user manager; Qt 6 prefers it over `QT_IM_MODULE`, and it survives into
  other sessions (niri) while the user manager lives. Users need
  `QT_IM_MODULES=hime` too.
- A daemon on X11 under niri (`HIME_BACKEND=x11`, or a build without
  gtk-layer-shell) shows its windows through xwayland-satellite as
  ordinary windows (app-id `Hime`) that take the focus; a niri window rule
  works around it: `match app-id="(?i)^hime$"`, `open-focused false`,
  `open-floating true`, `default-floating-position ...`.
- gtk-layer-shell must init a window before it is realized and cannot
  undo it: call `hime_window_init ()` right after `gtk_window_new ()`.
  Layer surfaces report no position; `hime_window_get_position ()`
  returns what `hime_window_move ()` set.
- Headless sway for tests (`@compositor sway`): it names its socket
  itself (`wayland-N`, ignoring `WAYLAND_DISPLAY`), its IPC socket path
  must fit in `sun_path` (keep `XDG_RUNTIME_DIR` short, e.g. mktemp), and
  there is no swaybg, so layer checks compare against a far-away pixel.
- Headless KWin for tests (`@compositor kwin`): `kwin_wayland --virtual
  --inputmethod ...` starts the daemon; KWin has no virtual keyboard or
  screencopy protocol, so `wl-type` uses `org_kde_kwin_fake_input` and
  `kwin-shot.py` the ScreenShot2 D-Bus interface, both allowed by
  `KWIN_WAYLAND_NO_PERMISSION_CHECKS=1` and
  `KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1`. Screenshots need OpenGL
  compositing (not `KWIN_COMPOSE=Q`); a render node or llvmpipe does.
  KWin places new windows centered: the text-input tests maximize theirs.
- KWin 6.7 removed text-input-v1: Chromium/Electron need
  `--enable-wayland-ime --wayland-text-input-version=3`. Qt 6 uses
  text-input-v2 on KWin.
- Headless GNOME Shell for tests (`@compositor gnome`): `--unsafe-mode`
  for its Eval and Screenshot D-Bus methods; it hangs starting Xwayland
  when `SSH_CONNECTION` is set; it has no keyboard until a Mutter
  RemoteDesktop session gives it one, and a keyboard going away resets
  the input method focus, so `rd-type.py --keep` holds one session that
  types all keys (FIFO); it starts in the overview (hidden by Eval); it
  restarts ibus-daemon with `--xim` once Xwayland is up; IBus finds the
  engine through `IBUS_COMPONENT_PATH` (keep `/usr/share/ibus/component`
  in it) and launches `<exec>` without a PATH search. `hime-ibus` must
  not auto-start the installed daemon there
  (`HIME_IM_CLIENT_NO_AUTO_EXEC`). IBus drops engines' output:
  `HIME_IBUS_DEBUG=FILE` logs what hime-ibus sees.
- The daemon ignores a focus out within 100 ms of another
  (`hime_FocusOut`); a focus in in between now resets that.
- A popup surface's wl_surface must not be destroyed before its role:
  GtkWindow's unmap destroys it, and gtk-layer-shell overrides that class
  handler for every window, so signal handlers and emission hooks run too
  late; `HimePopupWindow` drops the role in its unmap vfunc.
- A HIME tool's connection (hime-setup, hime-message) is current_CS while
  it is set up and switches the input method for its own new state; the
  focused client's window can end up hidden or recreated, so anything
  acting on "the" input window should use `hime_focused_client ()`.
- `change_win0_style ()` recreates win0 on the first settings reload
  (its `current_hime_inner_frame` starts at 0).
- Ghostty is single-instance: launching it again with another
  environment (e.g. `env -u GTK_IM_MODULE`) opens a window of the running
  process, which keeps its IM module; use `--gtk-single-instance=false`.
  In headless sway Ghostty never sends text-input `enable` (on niri it
  does), so it is not used in session tests.
- GDK dispatches Wayland events re-entrantly while a window is shown
  (e.g. HIME's window popping up on a key press): `src/wl-im.c` queues
  grab and input method events and handles them one at a time.
- wlroots routes the keys of any virtual keyboard except the input
  method's own through the grab, so `wl-type` can drive headless sway;
  niri sends virtual keyboard keys around the grab, so it cannot be
  tested that way.
- GTK 3's `wayland` IM module is in the system module cache: a session
  with `GTK_IM_MODULE_FILE` pointing at HIME's cache only does not find
  it (`text-input-test.c` unsets it).
- In gtab, the keys typed only show in the application's preedit with
  `hime-on-the-spot-key=1`; otherwise they stay in HIME's window, and the
  preedit only holds the phrase buffer (phrase mode).
- A GDBus monitor connection must drop every message in its filter
  (return NULL): GDBus would answer method calls meant for others, and a
  monitor that sends is disconnected. The filter runs in GDBus's worker
  thread.
- The GNOME test session starts `/usr/lib/xdg-desktop-portal-gnome`
  itself (per-application state needs it); test windows without a
  .desktop file are each an application (`window:N`) for GNOME Shell.
- A GSource for an Xlib connection: XPending does not notice a hung up
  connection (and a round trip then loops), while GLib keeps dispatching
  a source whose fd polls HUP: mark the display
  (`flags |= XlibDisplayIOError`) and return G_SOURCE_REMOVE.
- IMdkit cached the XIM_SERVERS atom in a static; atoms differ on a new
  X server, so it is looked up each time now.
- Xwayland (with libei) drops a client's first XTest event, and KWin's
  Xwayland sends XTest through KWin's input emulation, which needs a
  permission: the daemon on Wayland does not use XTest.
- XIM clients send keys only after a trigger key (Ctrl+Space) turned the
  input method on.
- On niri, `niri msg -j layers` shows the daemon's surfaces (namespace
  `hime`, Overlay, keyboard interactivity None). A test daemon can run in
  the user's live session without touching theirs: private
  `XDG_RUNTIME_DIR` and `HOME`, and `WAYLAND_DISPLAY` set to the absolute
  path of the session's socket.
- `~/.config/environment.d` is only read when the systemd user manager
  starts, which may outlive GNOME logins (e.g. a tmux/ssh session).
  `systemctl --user daemon-reload` reads it again; `unset-environment`
  cannot remove what it set.
- Test in the user's real GNOME session from a shell by taking
  `WAYLAND_DISPLAY`, `DISPLAY`, `DBUS_SESSION_BUS_ADDRESS` from a GUI
  process's `/proc/<pid>/environ`, and `GTK_PATH` / `LD_LIBRARY_PATH`
  pointing at the build tree.
- The daemon reads config from `$HOME/.config/hime`; tests isolate it by
  pointing `HOME` at a temp dir and `HIME_TABLE_DIR` at `data/`.
