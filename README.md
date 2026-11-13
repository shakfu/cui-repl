# cui-repl

A terminal in a [CUI](https://github.com/yappologistic/CUI) window. [libghostty-vt](https://lib.ghostty.org/vt/) 1.0.0 parses the child's output and holds the screen; `cui-repl` owns the pty, the drawing and the input.

    cui-repl [-C DIR] [-f FONT] [-s SIZE] [--light] [COMMAND [ARG...]]

Without `COMMAND` it runs `$SHELL -l`. It exits with the child's exit code when the child exits. Closing the window hangs up the child (SIGHUP, then SIGKILL after 0.5 s).

## Build

| Dependency     | Needed for                     | macOS                    | Debian or Ubuntu  |
|----------------|--------------------------------|--------------------------|-------------------|
| Zig 0.17.x     | building libghostty-vt         | `zvm`, or ziglang.org    | ziglang.org       |
| CMake 3.19+    | building                       | `brew install cmake`     | `cmake`           |
| C11 compiler   | building                       | `xcode-select --install` | `build-essential` |
| X11 headers    | the window, on Linux           | -                        | `libx11-dev`      |
| Python 3       | the `smoke` test               | preinstalled             | `python3`         |

Zig must be exactly 0.17; other minor versions fail ([Installing](https://lib.ghostty.org/vt/installing.html)). The first configure downloads the libghostty-vt tarball (SHA-256 pinned in `CMakeLists.txt`), and the first build lets Zig fetch its packages. Offline: `CMAKE_ARGS=-DFETCHCONTENT_SOURCE_DIR_GHOSTTY=/path/to/libghostty-vt-1.0.0`, with the `-deps` tarball from the same release.

    make              # configure and build into build/
    make test         # ctest: pty, session, view, smoke
    make run ARGS='-C ~/src'
    make install      # to PREFIX/bin; PREFIX=~/.local by default

`smoke` opens real windows for a few seconds. On Linux without `DISPLAY` it is skipped.

Built and tested on macOS (arm64) only. The Linux paths (X11 backend, font fallbacks, `TIOCSCTTY`) compile from the same sources but have not been run.

## Layout

| Path                  | Contains                                                              |
|-----------------------|-----------------------------------------------------------------------|
| `src/pty.{h,c}`       | spawn on a pty, non-blocking read and write, resize, hang up          |
| `src/session.{h,c}`   | pty plus libghostty terminal and key encoder; no CUI                  |
| `src/term_view.{h,c}` | the CUI widget: draws the render state, maps keys, text, paste, wheel, selection |
| `src/boxdraw.{h,c}`   | U+2500-259F drawn as rectangles                                       |
| `src/cuix.{h,c}`      | CUI internals the widget needs; compiles `cui.c` in the same unit     |
| `src/main.c`          | options, fonts, the window loop                                       |
| `vendor/cui/`         | CUI 1.3.1 (MIT), as vendored by myra                                  |
| `tests/`              | `test_pty.c`, `test_session.c` (headless), `test_view.c` (windowless CUI context), `smoke.py` (screenshots) |
| `tests/compat.sh`     | by hand: opens vim, tmux, htop, fzf and others in `cui-repl`, prints what to check, records pass or fail in `build/compat-results.md` |

## Input

| Input                              | Sent as                                                    |
|------------------------------------|------------------------------------------------------------|
| typing                             | the text CUI delivers, unchanged                           |
| Enter, Tab, arrows, F1-F12, ...    | libghostty's key encoder, following the program's modes (DECCKM, Kitty keyboard) |
| Ctrl+key; Alt+key off macOS        | the key encoder                                            |
| Option+key on macOS                | the composed text, as Terminal.app sends it                |
| Cmd+V / Ctrl+Shift+V               | the clipboard; control bytes stripped, bracketed in mode 2004. Text that could run commands waits for a confirm dialog |
| Cmd+C / Ctrl+Shift+C               | copies the selection (left-drag)                           |
| Shift+PgUp, Shift+PgDn, wheel      | scrolls history; on the alternate screen the wheel sends arrow keys |
| mouse, when the program tracks it  | libghostty's mouse encoder: buttons, wheel and motion, in the program's mode and format (X10, SGR, ...). Shift bypasses it, so Shift+drag selects |
| window focus                       | `CSI I` / `CSI O`, in mode 1004                            |

## Design notes

- **`cuix.c` includes `cui.c`.** `cui.h` has no API for a custom widget to read typed text, own keyboard focus, read window focus, keep Tab from moving focus, or reach the clipboard. `cuix.c` adds 10 accessors over `struct cui_ctx`. A CUI upgrade that renames those fields breaks the build in that file only. Patching CUI upstream is the alternative; these accessors define what such a patch needs.
- **The UI thread polls the pty.** CUI has no call that wakes its event loop from another thread. The widget reads the pty without blocking each frame and asks for the next frame in 1/60 s after recent input or output, else in 0.1 s. Idle cost measured on an M-series Mac: 2.7% of one core. Once output flows, a frame keeps reading for up to 8 ms, since the macOS pty buffer holds about 1 KB: `seq 1 500000` (3.4 MB) shows in 0.9 s, against 66 s with one drain per frame. A reader thread would need a cross-thread wake (an `NSEvent` post on macOS, an X11 `ClientMessage`), which CUI does not provide.
- **Box drawing is procedural.** At a 1.35x line height, a font's `│` leaves a gap between rows. U+2500-257F (light, heavy, double, rounded) and U+2580-259F (blocks, shades, quadrants) are drawn as rectangles snapped to whole pixels. Diagonals U+2571-2573 come from the font.
- **Fonts.** Geist Mono, built into CUI, has no box drawing or CJK. The first faces found among Menlo, Apple Symbols and Arial Unicode (macOS), or DejaVu Sans Mono and Noto Sans CJK (Linux), become fallbacks. CUI allows 4.
- **Pointer shapes.** CUI has 7 pointers and OSC 22 names 34 shapes. Shapes without a CUI equivalent show the arrow.
- **`TERM=xterm-256color`**, not `xterm-ghostty`: the ghostty terminfo entry is often not installed.

## Not done

- Bold and italic faces. Bold is overstruck; italic draws upright.
- Keys CUI has no code for: `[ ] ; ' \ \``, Insert and the keypad. They type as text, but Ctrl+[ and the like cannot be sent.
- IME composition, OSC 52 clipboard, hyperlinks, images, a visual bell.
