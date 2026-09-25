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
- `src/gtk-im/`, `src/gtk3-im/`, `src/qt5-im/`, `src/qt6-im/`: IM modules.

## Wayland plan

The goal is native Wayland support on all mainstream compositors while
keeping HIME's own UI and typing feel. Phases:

1. Daemon and IM modules work without X11: socket path under
   `$XDG_RUNTIME_DIR`, keysym-based key events, a GTK 4 IM module.
2. Separate the candidate UI model from its windows; draw with cairo so
   the same code renders into X11 windows and Wayland surfaces. The
   harness's stub list is the seam: everything `harness.c` replaces from
   `win-gtab.c` is the UI interface to extract.
3. `zwp_input_method_v2` + popup surface frontend (niri, sway, Hyprland,
   labwc/Xfce).
4. `zwp_input_method_v1` frontend (KDE/KWin).
5. IBus-compatible frontend plus a GNOME Shell extension (GNOME/Mutter).

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

## Gotchas

- Do not add `-DGTK_DISABLE_DEPRECATED` to GTK 3 builds: HIME still calls
  deprecated GTK 3 APIs, and hiding them turns the calls into implicit
  declarations (pointer truncation at runtime).
- `hime-cin2gtab` truncates paths at 64 characters; run it from the
  table's directory with a relative name.
- `hime-tsin2gtab-phrase` reads `tsin32` only from `~/.config/hime`, and
  the daemon runs helper tools from the hard-coded `HIME_BIN_DIR`.
- `src/im-client/` compiles its own copies of `src/*.c` files; the src
  Makefile always recurses into it so they get rebuilt.
- The daemon reads config from `$HOME/.config/hime`; tests isolate it by
  pointing `HOME` at a temp dir and `HIME_TABLE_DIR` at `data/`.
