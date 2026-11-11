# TODO

## Next

- [ ] **Word and line selection.** Double- and triple-click through `ghostty_selection_gesture_*`. It may replace part of the drag code in `term_view.c`.

- [ ] **Cross-thread wake.** Replace the 10 Hz idle poll (2.7% of one core) with a reader thread that wakes the event loop.
  - Preferred: a CUI patch adding a thread-safe `cui_app_wake`.
  - Fallback: post an `NSEvent` on macOS and an X11 `ClientMessage` on Linux from `cuix.c`.
  - Keep the 8 ms per-frame read budget; keep `fast_output_is_not_paced_by_frames` passing.

## Later

- [ ] **Embed in myra-ui.** One `session` plus one `term_view` as a dock panel, started in myra-ui's `s.cwd`.
  - Choose the build wiring: `FetchContent` of this repo, or a submodule.
  - Make Zig optional in myra: a `MYRA_UI_TERMINAL` option, off by default, so the CLI keeps libc + libcurl only.
  - Settle the clash between `cuix.c` compiling `cui.c` and myra's own `cui` target. Only one unit may compile `cui.c`.
  - Panel lifecycle: hang up the child when the panel closes; restart it on reopen.
- [ ] Build and run on Linux (X11, font fallbacks, `TIOCSCTTY`).
- [ ] Real bold and italic faces instead of overstrike and upright text.
- [ ] Keys CUI has no code for (`[ ] ; ' \ \``, Insert, keypad), so Ctrl+[ and the like can be sent.
- [ ] IME composition, OSC 52 clipboard, hyperlinks.
