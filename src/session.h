/* session.h - a shell on a pty, emulated by libghostty-vt. Knows nothing of CUI,
   so the tests drive it headless. One thread: every call from the same one. */
#ifndef CUI_REPL_SESSION_H
#define CUI_REPL_SESSION_H

#include "pty.h"

#include <ghostty/vt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    pty_proc pty;
    GhosttyTerminal term;
    GhosttyKeyEncoder enc;
    GhosttyKeyEvent ev;
    GhosttyMouseEncoder menc;
    GhosttyMouseEvent mev;
    uint16_t cols, rows;
    uint32_t cell_w, cell_h; /* pixels, as given to session_resize */

    uint8_t *out;          /* bytes for the child that the pty did not take yet */
    size_t out_len, out_cap;

    char title[256];       /* OSC 0/2; empty when unset */
    bool title_changed;    /* set when title changes; the caller clears it */
    bool held;             /* mode 2026: the program asked us not to draw */
    bool exited;           /* the child exited and its output was drained */
} session;

/* Start argv (NULL: $SHELL, else /bin/sh) in cwd (NULL: ours). Returns 0, or -1
   with errno set; s is then safe to session_close. */
int session_open(session *s, char *const argv[], const char *cwd, uint16_t cols, uint16_t rows);

/* Feed what the child wrote into the terminal, at most max bytes, and flush
   bytes queued for it. Once output flows, keep reading for up to budget_ms: a
   pty buffer holds about 1 KB on macOS, so one drain per frame would stall a
   fast writer. Returns at once when nothing is waiting. Returns bytes read; sets
   exited once the child is gone. */
size_t session_pump(session *s, size_t max, int budget_ms);

/* Queue bytes for the child and write what the pty takes now. */
void session_send(session *s, const void *data, size_t n);

/* Encode a key press for the current terminal modes and send it. utf8 is the
   key's unmodified text ("c" for Ctrl+C), or NULL. Returns false when the key
   encodes to nothing. Scrolls the viewport to the bottom, as typing does. */
bool session_key(session *s, GhosttyKey key, GhosttyMods mods, const char *utf8);

/* A program asked for mouse reports (any tracking mode). */
bool session_mouse_tracking(session *s);

/* Encode a mouse event for the program's tracking mode and format, and send it.
   button GHOSTTY_MOUSE_BUTTON_UNKNOWN: none, for motion. x, y: pixels from the
   grid's top-left, at the cell size given to session_resize. held: a button is
   down. Returns false when the event encodes to nothing. */
bool session_mouse(session *s, GhosttyMouseAction action, GhosttyMouseButton button, GhosttyMods mods, float x,
                   float y, bool held);

/* Report focus gained or lost, if the program asked (mode 1004). */
void session_focus(session *s, bool focused);

/* Send typed text as is. */
void session_text(session *s, const char *utf8, size_t n);

/* Paste: strip control bytes, and bracket it when the program asked (mode 2004).
   Text that could run commands (a line break outside a bracketed paste, or the
   end-of-paste sequence) is sent only with allow_unsafe; without it nothing is
   sent and this returns GHOSTTY_REJECTED, so the caller can ask first. */
GhosttyResult session_paste(session *s, const char *text, size_t n, bool allow_unsafe);

/* Resize the grid and tell the child. Cell sizes in pixels, for size reports. */
void session_resize(session *s, uint16_t cols, uint16_t rows, uint32_t cell_w, uint32_t cell_h);

/* Scroll the viewport by delta rows (negative: up, into history). */
void session_scroll(session *s, intptr_t delta);

/* Whether a mode is set, e.g. GHOSTTY_MODE_BRACKETED_PASTE. */
bool session_mode(session *s, GhosttyMode mode);

/* The alternate screen (full-screen programs) is showing. */
bool session_alt_screen(session *s);

/* The screen, or the selection when selection is true, as plain text, trimmed;
   malloc'd and NUL-terminated, NULL on failure or no selection. */
char *session_format(session *s, bool selection);

/* Exit code as a shell reports it, -1 while running. */
int session_exit_code(const session *s);

/* Hang up the child and free everything. Safe on a failed open, and twice. */
void session_close(session *s);

#endif
