/* session.c - see session.h. */
#include "session.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void on_write_pty(GhosttyTerminal t, void *ud, const uint8_t *data, size_t len) {
    (void)t;
    session_send(ud, data, len);
}

static void on_title(GhosttyTerminal t, void *ud) {
    session *s = ud;
    GhosttyString str = {0};
    if (ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_TITLE, &str) != GHOSTTY_SUCCESS) return;
    size_t n = str.len < sizeof s->title - 1 ? str.len : sizeof s->title - 1;
    while (n > 0 && n < str.len && (str.ptr[n] & 0xC0) == 0x80) n--; /* whole characters */
    if (n) memcpy(s->title, str.ptr, n);
    s->title[n] = 0;
    s->title_changed = true;
}

static void on_hold(GhosttyTerminal t, void *ud, bool held) {
    (void)t;
    ((session *)ud)->held = held;
}

/* CSI 14/16/18/19 t. Mode 2048 reports need no callback: resize sends them. */
static bool on_size(GhosttyTerminal t, void *ud, GhosttySizeReportSize *out) {
    (void)t;
    const session *s = ud;
    if (!s->cell_w || !s->cell_h) return false; /* no pixel geometry until the first resize */
    *out = (GhosttySizeReportSize){.rows = s->rows, .columns = s->cols, .cell_width = s->cell_w, .cell_height = s->cell_h};
    return true;
}

/* CSI ? 996 n, from the background the program sees (OSC 11 included), not the window theme. */
static bool on_color_scheme(GhosttyTerminal t, void *ud, GhosttyColorScheme *out) {
    (void)ud;
    GhosttyColorRgb bg;
    if (ghostty_terminal_get(t, GHOSTTY_TERMINAL_DATA_COLOR_BACKGROUND, &bg) != GHOSTTY_SUCCESS) return false;
    *out = 299 * bg.r + 587 * bg.g + 114 * bg.b < 128000 ? GHOSTTY_COLOR_SCHEME_DARK : GHOSTTY_COLOR_SCHEME_LIGHT;
    return true;
}

int session_open(session *s, char *const argv[], const char *cwd, uint16_t cols, uint16_t rows) {
    memset(s, 0, sizeof *s);
    s->pty.fd = -1;
    s->cols = cols, s->rows = rows;
    if (ghostty_terminal_new(NULL, &s->term, cols, rows) != GHOSTTY_SUCCESS ||
        ghostty_key_encoder_new(NULL, &s->enc) != GHOSTTY_SUCCESS ||
        ghostty_key_event_new(NULL, &s->ev) != GHOSTTY_SUCCESS ||
        ghostty_mouse_encoder_new(NULL, &s->menc) != GHOSTTY_SUCCESS ||
        ghostty_mouse_event_new(NULL, &s->mev) != GHOSTTY_SUCCESS) {
        errno = ENOMEM;
        return -1;
    }
    ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_USERDATA, s);
    ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_WRITE_PTY, (const void *)on_write_pty);
    ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED, (const void *)on_title);
    ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_RENDER_HOLD, (const void *)on_hold);
    ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_SIZE, (const void *)on_size);
    ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_COLOR_SCHEME, (const void *)on_color_scheme);
    /* Unset defaults draw as white on black, but leave OSC 10/11 queries unanswered. */
    ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &(GhosttyColorRgb){0xff, 0xff, 0xff});
    ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &(GhosttyColorRgb){0, 0, 0});

    const char *shell = getenv("SHELL");
    if (!shell || !*shell) shell = "/bin/sh";
    char *login[] = {(char *)shell, "-l", NULL}; /* a login shell, as macOS terminals start */
    /* xterm-256color, not xterm-ghostty: the ghostty terminfo entry is often not installed */
    char *env[] = {"TERM=xterm-256color", "COLORTERM=truecolor", "TERM_PROGRAM=cui-repl", NULL};
    return pty_spawn(&s->pty, argv ? argv : login, cwd, env, cols, rows);
}

static void flush(session *s) {
    if (!s->out_len) return;
    ssize_t w = pty_write(&s->pty, s->out, s->out_len);
    if (w < 0) { s->out_len = 0; return; } /* the child is gone; nobody will read it */
    memmove(s->out, s->out + w, s->out_len - (size_t)w);
    s->out_len -= (size_t)w;
}

void session_send(session *s, const void *data, size_t n) {
    if (!n || s->pty.fd < 0) return;
    if (s->out_len + n > s->out_cap) {
        size_t cap = s->out_cap ? s->out_cap : 4096;
        while (cap < s->out_len + n) cap *= 2;
        uint8_t *p = realloc(s->out, cap);
        if (!p) return; /* dropped: the paste or key is lost, not the session */
        s->out = p, s->out_cap = cap;
    }
    memcpy(s->out + s->out_len, data, n);
    s->out_len += n;
    flush(s);
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

size_t session_pump(session *s, size_t max, int budget_ms) {
    flush(s);
    uint8_t buf[64 * 1024];
    size_t total = 0;
    bool eof = s->pty.fd < 0;
    double end = now_ms() + budget_ms;
    while (!eof && total < max) {
        ssize_t n = pty_read(&s->pty, buf, sizeof buf);
        if (n < 0) eof = true;
        if (n < 0 || (n == 0 && !total)) break;
        if (n == 0) { /* drained while output flows: let the child refill it */
            int left = (int)(end - now_ms());
            struct pollfd pfd = {.fd = s->pty.fd, .events = POLLIN};
            if (left <= 0 || poll(&pfd, 1, left) <= 0) break;
            continue;
        }
        ghostty_terminal_vt_write(s->term, buf, (size_t)n);
        total += (size_t)n;
    }
    /* A background job can hold the pty open after the shell exits; the shell's
       exit ends the session either way, once its output is read. */
    if ((eof || total == 0) && pty_poll_exit(&s->pty)) s->exited = true;
    return total;
}

static void to_bottom(session *s) {
    GhosttyTerminalScrollViewport b = {.tag = GHOSTTY_TERMINAL_SCROLL_VIEWPORT_BOTTOM};
    ghostty_terminal_scroll_viewport(s->term, b);
}

/* The first code point of a UTF-8 string, 0 if none or malformed. */
static uint32_t first_codepoint(const char *u) {
    const unsigned char *p = (const unsigned char *)u;
    if (!p || !p[0]) return 0;
    if (p[0] < 0x80) return p[0];
    int n = p[0] >= 0xF0 ? 3 : p[0] >= 0xE0 ? 2 : p[0] >= 0xC0 ? 1 : -1;
    if (n < 0) return 0;
    uint32_t c = p[0] & (0x3F >> n);
    for (int i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        c = (c << 6) | (p[i] & 0x3F);
    }
    return c;
}

bool session_key(session *s, GhosttyKey key, GhosttyMods mods, const char *utf8) {
    ghostty_key_encoder_set_from_terminal(s->enc, s->term);
    ghostty_key_event_set_action(s->ev, GHOSTTY_KEY_ACTION_PRESS);
    ghostty_key_event_set_key(s->ev, key);
    ghostty_key_event_set_mods(s->ev, mods);
    ghostty_key_event_set_consumed_mods(s->ev, 0);
    GhosttyString text = {(const uint8_t *)utf8, utf8 ? strlen(utf8) : 0};
    ghostty_key_event_set_utf8(s->ev, text);
    ghostty_key_event_set_unshifted_codepoint(s->ev, first_codepoint(utf8));
    uint8_t buf[128];
    size_t n = 0;
    if (ghostty_key_encoder_encode(s->enc, s->ev, buf, sizeof buf, &n) != GHOSTTY_SUCCESS || !n) return false;
    to_bottom(s);
    session_send(s, buf, n);
    return true;
}

bool session_mouse_tracking(session *s) {
    bool on = false;
    ghostty_terminal_get(s->term, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &on);
    return on;
}

bool session_mouse(session *s, GhosttyMouseAction action, GhosttyMouseButton button, GhosttyMods mods, float x,
                   float y, bool held) {
    if (!s->cell_w || !s->cell_h) return false; /* no geometry until the first resize */
    GhosttyMouseEncoderSize sz = GHOSTTY_INIT_SIZED(GhosttyMouseEncoderSize);
    sz.cell_width = s->cell_w, sz.cell_height = s->cell_h;
    sz.screen_width = s->cols * s->cell_w, sz.screen_height = s->rows * s->cell_h;
    ghostty_mouse_encoder_set_from_terminal(s->menc, s->term);
    ghostty_mouse_encoder_set(s->menc, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &sz);
    ghostty_mouse_encoder_set(s->menc, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &held);
    ghostty_mouse_event_set_action(s->mev, action);
    if (button == GHOSTTY_MOUSE_BUTTON_UNKNOWN) ghostty_mouse_event_clear_button(s->mev);
    else ghostty_mouse_event_set_button(s->mev, button);
    ghostty_mouse_event_set_mods(s->mev, mods);
    ghostty_mouse_event_set_position(s->mev, (GhosttyMousePosition){x, y});
    uint8_t buf[128];
    size_t n = 0;
    if (ghostty_mouse_encoder_encode(s->menc, s->mev, buf, sizeof buf, &n) != GHOSTTY_SUCCESS || !n) return false;
    session_send(s, buf, n);
    return true;
}

void session_focus(session *s, bool focused) {
    if (!session_mode(s, GHOSTTY_MODE_FOCUS_EVENT)) return;
    uint8_t buf[8];
    size_t n = 0;
    if (ghostty_focus_encode(focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST, buf, sizeof buf, &n) == GHOSTTY_SUCCESS)
        session_send(s, buf, n);
}

void session_text(session *s, const char *utf8, size_t n) {
    if (!n) return;
    to_bottom(s);
    session_send(s, utf8, n);
}

typedef struct {
    const char *text;
    size_t n;
} paste_src;

static bool read_paste(void *ud, GhosttyString mime, GhosttyWriter w) {
    (void)mime;
    const paste_src *p = ud;
    return w.write(w.userdata, (const uint8_t *)p->text, p->n);
}

GhosttyResult session_paste(session *s, const char *text, size_t n, bool allow_unsafe) {
    if (!n) return GHOSTTY_SUCCESS;
    paste_src src = {text, n};
    GhosttyString mime = {(const uint8_t *)"text/plain", 10};
    GhosttyPaste p = GHOSTTY_INIT_SIZED(GhosttyPaste);
    p.source = GHOSTTY_PASTE_SOURCE_CLIPBOARD;
    p.mimes = &mime, p.mimes_len = 1;
    p.reader = (GhosttyMimeReader){read_paste, &src};
    p.allow_unsafe = allow_unsafe;
    GhosttyResult r = ghostty_terminal_paste(s->term, &p, NULL);
    if (r == GHOSTTY_SUCCESS) to_bottom(s);
    return r;
}

void session_resize(session *s, uint16_t cols, uint16_t rows, uint32_t cell_w, uint32_t cell_h) {
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    s->cell_w = cell_w, s->cell_h = cell_h;
    if (cols == s->cols && rows == s->rows) return;
    s->cols = cols, s->rows = rows;
    ghostty_terminal_resize(s->term, cols, rows, cell_w, cell_h);
    uint32_t w = cols * cell_w, h = rows * cell_h;
    pty_resize(&s->pty, cols, rows, w > 0xFFFF ? 0xFFFF : (uint16_t)w, h > 0xFFFF ? 0xFFFF : (uint16_t)h);
}

void session_scroll(session *s, intptr_t delta) {
    GhosttyTerminalScrollViewport b = {.tag = GHOSTTY_TERMINAL_SCROLL_VIEWPORT_DELTA, .value = {.delta = delta}};
    ghostty_terminal_scroll_viewport(s->term, b);
}

bool session_mode(session *s, GhosttyMode mode) {
    GhosttyTerminalModeConfig c = {.mode = mode};
    return ghostty_terminal_get(s->term, GHOSTTY_TERMINAL_DATA_MODE, &c) == GHOSTTY_SUCCESS && c.value;
}

bool session_alt_screen(session *s) {
    GhosttyTerminalScreen scr = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    ghostty_terminal_get(s->term, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &scr);
    return scr == GHOSTTY_TERMINAL_SCREEN_ALTERNATE;
}

char *session_format(session *s, bool selection) {
    GhosttySelection sel;
    if (selection && ghostty_terminal_get(s->term, GHOSTTY_TERMINAL_DATA_SELECTION, &sel) != GHOSTTY_SUCCESS)
        return NULL;
    GhosttyFormatter f = NULL;
    if (ghostty_formatter_terminal_new(NULL, &f, s->term) != GHOSTTY_SUCCESS) return NULL;
    GhosttyFormatterFormat emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    bool yes = true;
    ghostty_formatter_set(f, GHOSTTY_FORMATTER_OPT_EMIT, &emit);
    ghostty_formatter_set(f, GHOSTTY_FORMATTER_OPT_TRIM, &yes);
    if (selection) {
        ghostty_formatter_set(f, GHOSTTY_FORMATTER_OPT_UNWRAP, &yes); /* soft-wrapped lines copy as one */
        ghostty_formatter_set(f, GHOSTTY_FORMATTER_OPT_SELECTION, &sel);
    }
    uint8_t *buf = NULL;
    size_t len = 0;
    char *out = NULL;
    if (ghostty_formatter_format_alloc(f, NULL, &buf, &len) == GHOSTTY_SUCCESS && (out = malloc(len + 1))) {
        memcpy(out, buf, len);
        out[len] = 0;
    }
    if (buf) ghostty_free(NULL, buf, len);
    ghostty_formatter_free(f);
    return out;
}

int session_exit_code(const session *s) { return pty_exit_code(&s->pty); }

void session_close(session *s) {
    pty_close(&s->pty, 500);
    if (s->mev) ghostty_mouse_event_free(s->mev);
    if (s->menc) ghostty_mouse_encoder_free(s->menc);
    if (s->ev) ghostty_key_event_free(s->ev);
    if (s->enc) ghostty_key_encoder_free(s->enc);
    if (s->term) ghostty_terminal_free(s->term);
    free(s->out);
    s->mev = NULL, s->menc = NULL, s->ev = NULL, s->enc = NULL, s->term = NULL, s->out = NULL;
    s->out_len = s->out_cap = 0;
}
