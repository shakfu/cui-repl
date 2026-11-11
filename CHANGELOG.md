# Changelog

## Unreleased

### Added

- `cui-repl`: a terminal in a CUI 1.3.1 window. libghostty-vt 1.0.0 emulates it, fetched by CMake with a pinned SHA-256 and built with Zig 0.17 (README, "Build"). Runs `$SHELL -l` or a given command and exits with its exit code. Options: `-C DIR`, `-f FONT`, `-s SIZE`, `--light`.

- Input goes through libghostty's key encoder for special keys and Ctrl (and, off macOS, Alt) combinations, so cursor-key and Kitty keyboard modes are followed. Plain typing goes through as CUI's text, since CUI reports a printable key both as a key and as text, and encoding both sent it twice. Paste strips control bytes and brackets in mode 2004. Drag selects, Cmd+C / Ctrl+Shift+C copies. The wheel and Shift+PgUp scroll history; on the alternate screen the wheel sends arrow keys.

- Box drawing (U+2500-257F) and block elements (U+2580-259F) are drawn as rectangles. Font glyphs left gaps between rows at a 1.35x line height. Missing glyphs fall back to Menlo, Apple Symbols and Arial Unicode on macOS, DejaVu Sans Mono and Noto Sans CJK on Linux.

- `src/cuix.c` compiles `cui.c` with 10 accessors over `struct cui_ctx`, for typed text, keyboard and window focus, Tab, wheel, cursor and clipboard, which `cui.h` does not expose. A CUI upgrade that renames those fields breaks this file only.

- The UI thread polls the pty: CUI cannot be woken from another thread. Idle cost is 2.7% of one core. Once output flows, a frame reads for up to 8 ms: the macOS pty buffer holds about 1 KB, and one drain per frame took 66 s for 3.4 MB of `seq` output, against 0.9 s now.

- Mouse reporting through libghostty's mouse encoder, for programs that enable tracking (vim `mouse=a`, htop, tmux): left, right and middle buttons, the wheel as buttons 4 and 5, and motion once per cell. Shift bypasses tracking, so Shift+drag still selects. A release is always sent for a reported press, even after Shift goes down mid-drag, so the program never sees a stuck button. Positions are scaled to the integer cell size libghostty gets at resize. Otherwise a fractional cell width shifts the reported column across a wide row.

- Focus reports (`CSI I` / `CSI O`, mode 1004) when window or keyboard focus changes. OSC 22 pointer shapes map to CUI's 7 pointers; the others show the arrow.

- A paste that could run commands opens a dialog showing the text, with Cancel and Paste; Esc cancels. Such a paste is a line break outside a bracketed paste, or the end-of-paste sequence inside one. libghostty's `ghostty_terminal_paste` applies that rule; `ghostty_paste_is_safe` was the alternative, but it also flags line breaks inside a bracketed paste, which the shell does not run. The terminal takes no keys while the dialog is open, so Enter cannot reach the shell.

- Answers size queries (`CSI 14/16/18/19 t`) and color scheme queries (`CSI ? 996 n`). Mode 2048 resize reports needed no code; libghostty sends them on resize. The scheme comes from the terminal's background luminance, not `--light`, since `--light` themes only the window. Mode 2031 change reports are not sent: the scheme changes only when the program sets OSC 11 itself.

- The default colors (white on black) are now set on the terminal, not left to the renderer's fallback. Unset, `OSC 10/11 ?` got no reply, so programs that detect the background (vim, fish, delta) fell back to guessing.

- Tests (`make test`): `pty` and `session` run real children headless; `view` drives the widget through a windowless CUI context; `smoke` checks screenshot pixels and exit codes. Built and tested on macOS only.
