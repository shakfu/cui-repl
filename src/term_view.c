/* term_view.c - see term_view.h. Input: special keys and Ctrl (and, off macOS,
   Alt) combinations go through libghostty's key encoder; plain typing arrives as
   CUI text and goes to the pty as is. */
#include "term_view.h"

#include "boxdraw.h"
#include "cuix.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAD 6.0f
#define HOLD_MAX 1.0 /* seconds a render hold may freeze the screen */

#if defined(__APPLE__)
#define COPY_MODS CUI_MOD_SUPER
#define ALT_IS_META 0 /* Option composes characters, as in Terminal.app */
#else
#define COPY_MODS (CUI_MOD_CTRL | CUI_MOD_SHIFT)
#define ALT_IS_META 1
#endif

int term_view_init(term_view *v, session *s, float font_size) {
    memset(v, 0, sizeof *v);
    v->s = s;
    v->size = font_size > 0 ? font_size : 13;
    v->mouse_col = v->mouse_row = -1;
    if (ghostty_render_state_new(NULL, &v->rs) != GHOSTTY_SUCCESS ||
        ghostty_render_state_row_iterator_new(NULL, &v->rows) != GHOSTTY_SUCCESS ||
        ghostty_render_state_row_cells_new(NULL, &v->cells) != GHOSTTY_SUCCESS) {
        term_view_free(v);
        return -1;
    }
    return 0;
}

void term_view_free(term_view *v) {
    if (v->cells) ghostty_render_state_row_cells_free(v->cells);
    if (v->rows) ghostty_render_state_row_iterator_free(v->rows);
    if (v->rs) ghostty_render_state_free(v->rs);
    free(v->line);
    free(v->paste);
    v->paste = NULL;
    v->cells = NULL, v->rows = NULL, v->rs = NULL, v->line = NULL;
    v->line_cap = 0;
}

/* ---- input ---- */

static GhosttyMods ghostty_mods(int m) {
    return (GhosttyMods)(((m & CUI_MOD_SHIFT) ? GHOSTTY_MODS_SHIFT : 0) | ((m & CUI_MOD_CTRL) ? GHOSTTY_MODS_CTRL : 0) |
                         ((m & CUI_MOD_ALT) ? GHOSTTY_MODS_ALT : 0) | ((m & CUI_MOD_SUPER) ? GHOSTTY_MODS_SUPER : 0));
}

/* The libghostty key for a CUI key, and its unshifted character for printable keys. */
static GhosttyKey key_of(cui_key k, char *ch) {
    *ch = 0;
    if (k >= CUI_KEY_A && k <= CUI_KEY_Z) return *ch = (char)('a' + (k - CUI_KEY_A)), GHOSTTY_KEY_A + (k - CUI_KEY_A);
    if (k >= CUI_KEY_0 && k <= CUI_KEY_9) return *ch = (char)('0' + (k - CUI_KEY_0)), GHOSTTY_KEY_DIGIT_0 + (k - CUI_KEY_0);
    if (k >= CUI_KEY_F1 && k <= CUI_KEY_F12) return GHOSTTY_KEY_F1 + (k - CUI_KEY_F1);
    switch (k) {
    case CUI_KEY_TAB: return GHOSTTY_KEY_TAB;
    case CUI_KEY_ENTER: return GHOSTTY_KEY_ENTER;
    case CUI_KEY_ESCAPE: return GHOSTTY_KEY_ESCAPE;
    case CUI_KEY_BACKSPACE: return GHOSTTY_KEY_BACKSPACE;
    case CUI_KEY_DELETE: return GHOSTTY_KEY_DELETE;
    case CUI_KEY_SPACE: return *ch = ' ', GHOSTTY_KEY_SPACE;
    case CUI_KEY_LEFT: return GHOSTTY_KEY_ARROW_LEFT;
    case CUI_KEY_RIGHT: return GHOSTTY_KEY_ARROW_RIGHT;
    case CUI_KEY_UP: return GHOSTTY_KEY_ARROW_UP;
    case CUI_KEY_DOWN: return GHOSTTY_KEY_ARROW_DOWN;
    case CUI_KEY_HOME: return GHOSTTY_KEY_HOME;
    case CUI_KEY_END: return GHOSTTY_KEY_END;
    case CUI_KEY_PAGE_UP: return GHOSTTY_KEY_PAGE_UP;
    case CUI_KEY_PAGE_DOWN: return GHOSTTY_KEY_PAGE_DOWN;
    case CUI_KEY_MINUS: return *ch = '-', GHOSTTY_KEY_MINUS;
    case CUI_KEY_EQUAL: return *ch = '=', GHOSTTY_KEY_EQUAL;
    case CUI_KEY_COMMA: return *ch = ',', GHOSTTY_KEY_COMMA;
    case CUI_KEY_PERIOD: return *ch = '.', GHOSTTY_KEY_PERIOD;
    case CUI_KEY_SLASH: return *ch = '/', GHOSTTY_KEY_SLASH;
    default: return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

static void copy_selection(cui_ctx *ui, term_view *v) {
    char *text = session_format(v->s, true);
    if (text && *text) cuix_set_clipboard(ui, text);
    free(text);
}

/* Paste, or hold the text for confirm_paste when it could run commands. */
static void paste_clipboard(cui_ctx *ui, term_view *v) {
    const char *clip = cuix_clipboard(ui);
    size_t n = clip ? strlen(clip) : 0;
    if (!n || session_paste(v->s, clip, n, false) != GHOSTTY_REJECTED) return;
    char *copy = malloc(n);
    if (!copy) return; /* dropped: not pasting is the safe failure */
    memcpy(copy, clip, n);
    free(v->paste);
    v->paste = copy, v->paste_len = n, v->confirming = true;
}

/* Returns true when it sent anything. */
static bool handle_keys(cui_ctx *ui, term_view *v) {
    session *s = v->s;
    int m = cui_key_mods(ui);
    bool ctrl = m & CUI_MOD_CTRL, meta = ALT_IS_META && (m & CUI_MOD_ALT), super = m & CUI_MOD_SUPER;
    bool sent = false, by_key = false;
    if ((m & COPY_MODS) == COPY_MODS && cui_key_pressed(ui, CUI_KEY_C)) {
        copy_selection(ui, v);
        cuix_eat_key(ui, CUI_KEY_C);
        return false;
    }
    if ((m & COPY_MODS) == COPY_MODS && cui_key_pressed(ui, CUI_KEY_V)) {
        paste_clipboard(ui, v);
        cuix_eat_key(ui, CUI_KEY_V);
        cuix_take_text(ui, (char[4]){0}, 4); /* a platform may also type the "v" */
        return true;
    }
    for (int k = CUI_KEY_NONE + 1; k < CUI_KEY_COUNT; k++) {
        if (!cui_key_pressed(ui, (cui_key)k)) continue;
        char ch;
        GhosttyKey g = key_of((cui_key)k, &ch);
        if (g == GHOSTTY_KEY_UNIDENTIFIED || (super && !ctrl)) continue; /* Command: the app's shortcuts */
        if (ch && !ctrl && !meta) continue; /* plain typing comes as text */
        cuix_eat_key(ui, (cui_key)k);
        if ((g == GHOSTTY_KEY_PAGE_UP || g == GHOSTTY_KEY_PAGE_DOWN) && (m & CUI_MOD_SHIFT) && !session_alt_screen(s)) {
            session_scroll(s, (g == GHOSTTY_KEY_PAGE_UP ? -1 : 1) * (intptr_t)(s->rows > 1 ? s->rows - 1 : 1));
            continue;
        }
        char utf8[2] = {ch, 0};
        if (session_key(s, g, ghostty_mods(m), ch ? utf8 : NULL)) sent = true;
        by_key |= ch != 0;
    }
    char text[256];
    int n = cuix_take_text(ui, text, sizeof text);
    if (n && !by_key && !(super && !ctrl)) { /* by_key: that key already sent this text */
        session_text(s, text, (size_t)n);
        sent = true;
    }
    return sent;
}

static void cell_at(const term_view *v, cui_vec2 m, uint16_t *x, uint16_t *y) {
    float fx = (m.x - PAD) / v->cell_w, fy = (m.y - PAD) / v->cell_h;
    float mx = v->s->cols - 1, my = v->s->rows - 1;
    *x = (uint16_t)(fx < 0 ? 0 : fx > mx ? mx : fx);
    *y = (uint16_t)(fy < 0 ? 0 : fy > my ? my : fy);
}

static void select_cells(term_view *v, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    GhosttyGridRef a = {0}, b = {0};
    GhosttyPoint pa = {.tag = GHOSTTY_POINT_TAG_VIEWPORT, .value = {.coordinate = {.x = x0, .y = y0}}};
    GhosttyPoint pb = {.tag = GHOSTTY_POINT_TAG_VIEWPORT, .value = {.coordinate = {.x = x1, .y = y1}}};
    if (ghostty_terminal_grid_ref(v->s->term, pa, &a) != GHOSTTY_SUCCESS ||
        ghostty_terminal_grid_ref(v->s->term, pb, &b) != GHOSTTY_SUCCESS)
        return;
    GhosttySelection sel = {.start = a, .end = b};
    ghostty_terminal_set(v->s->term, GHOSTTY_TERMINAL_OPT_SELECTION, &sel);
}

/* Whole wheel lines this frame (+ is up); trackpads send fractions of a line. */
static int take_wheel(cui_ctx *ui, term_view *v) {
    v->wheel += cuix_take_scroll(ui) * 3;
    int lines = (int)v->wheel;
    v->wheel -= (float)lines;
    return lines;
}

static const GhosttyMouseButton ghostty_button[3] = {GHOSTTY_MOUSE_BUTTON_LEFT, GHOSTTY_MOUSE_BUTTON_RIGHT,
                                                     GHOSTTY_MOUSE_BUTTON_MIDDLE}; /* by cui_mouse_button */

/* Mouse reports to a program that tracks the mouse. Releases always go, so the
   program never sees a button stuck down; presses, motion and the wheel only
   when report is set. Motion is sent once per cell; the encoder drops what the
   tracking mode does not ask for. */
static void report_mouse(cui_ctx *ui, term_view *v, const cui_canvas_result *r, bool report) {
    session *s = v->s;
    GhosttyMods mods = ghostty_mods(cui_key_mods(ui));
    /* to the integer cell size session_resize got, so cell edges match */
    float gw = (float)(s->cols * s->cell_w), gh = (float)(s->rows * s->cell_h);
    float x = (r->mouse.x - PAD) * (float)s->cell_w / v->cell_w, y = (r->mouse.y - PAD) * (float)s->cell_h / v->cell_h;
    x = x < 0 ? 0 : x > gw - 1 ? gw - 1 : x;
    y = y < 0 ? 0 : y > gh - 1 ? gh - 1 : y;
    if (report && s->cell_w && s->cell_h) {
        int col = (int)(x / (float)s->cell_w), row = (int)(y / (float)s->cell_h);
        if ((r->hovered || v->reported) && (col != v->mouse_col || row != v->mouse_row)) {
            v->mouse_col = col, v->mouse_row = row;
            int b = v->reported & 1 ? 0 : v->reported & 2 ? 1 : v->reported & 4 ? 2 : -1;
            session_mouse(s, GHOSTTY_MOUSE_ACTION_MOTION, b < 0 ? GHOSTTY_MOUSE_BUTTON_UNKNOWN : ghostty_button[b], mods, x,
                          y, b >= 0);
        }
    }
    for (int b = 0; b < 3; b++) {
        uint8_t bit = (uint8_t)(1u << b);
        bool down = cui_mouse_down(ui, (cui_mouse_button)b);
        if (down && !(v->mdown & bit) && report && r->hovered) {
            cuix_focus(ui, v->id);
            v->reported |= bit;
            session_mouse(s, GHOSTTY_MOUSE_ACTION_PRESS, ghostty_button[b], mods, x, y, true);
        } else if (!down && (v->reported & bit)) {
            v->reported &= (uint8_t)~bit;
            session_mouse(s, GHOSTTY_MOUSE_ACTION_RELEASE, ghostty_button[b], mods, x, y, v->reported != 0);
        }
    }
    if (!report || !r->hovered) return;
    int lines = take_wheel(ui, v);
    for (int i = 0; i < abs(lines); i++)
        session_mouse(s, GHOSTTY_MOUSE_ACTION_PRESS, lines > 0 ? GHOSTTY_MOUSE_BUTTON_FOUR : GHOSTTY_MOUSE_BUTTON_FIVE,
                      mods, x, y, v->reported != 0);
}

/* Drag selects; the wheel scrolls history, or sends arrow keys on the alternate
   screen (xterm's alternate scroll). */
static void select_and_scroll(cui_ctx *ui, term_view *v, const cui_canvas_result *r) {
    session *s = v->s;
    if (r->pressed && !v->selecting) {
        cuix_focus(ui, v->id);
        v->selecting = true, v->dragged = false;
        v->press_at = r->mouse;
        cell_at(v, r->mouse, &v->press_x, &v->press_y);
        ghostty_terminal_set(s->term, GHOSTTY_TERMINAL_OPT_SELECTION, NULL);
    } else if (r->pressed) {
        if (fabsf(r->mouse.x - v->press_at.x) + fabsf(r->mouse.y - v->press_at.y) > 3) v->dragged = true;
        if (v->dragged) {
            uint16_t x, y;
            cell_at(v, r->mouse, &x, &y);
            select_cells(v, v->press_x, v->press_y, x, y);
        }
    } else {
        v->selecting = false;
    }
    if (!r->hovered) return;
    int lines = take_wheel(ui, v);
    if (!lines) return;
    if (session_alt_screen(s)) {
        GhosttyKey k = lines > 0 ? GHOSTTY_KEY_ARROW_UP : GHOSTTY_KEY_ARROW_DOWN;
        for (int i = 0; i < abs(lines); i++) session_key(s, k, 0, NULL);
    } else {
        session_scroll(s, -lines);
    }
}

/* With mouse tracking on, the mouse goes to the program; Shift+drag still selects. */
static void handle_mouse(cui_ctx *ui, term_view *v, const cui_canvas_result *r) {
    bool report = session_mouse_tracking(v->s) && !(cui_key_mods(ui) & CUI_MOD_SHIFT);
    report_mouse(ui, v, r, report);
    if (report) v->selecting = false;
    else select_and_scroll(ui, v, r);
    v->mdown = 0;
    for (int b = 0; b < 3; b++)
        if (cui_mouse_down(ui, (cui_mouse_button)b)) v->mdown |= (uint8_t)(1u << b);
    if (!report) v->mouse_col = v->mouse_row = -1; /* the next report starts with a motion */
}

/* The pointer for the OSC 22 shape the program asked for; CUI has 7. */
static cui_cursor pointer_shape(session *s) {
    GhosttyMouseShape m = GHOSTTY_MOUSE_SHAPE_TEXT;
    ghostty_terminal_get(s->term, GHOSTTY_TERMINAL_DATA_MOUSE_SHAPE, &m);
    switch (m) {
    case GHOSTTY_MOUSE_SHAPE_TEXT:
    case GHOSTTY_MOUSE_SHAPE_VERTICAL_TEXT: return CUI_CURSOR_TEXT;
    case GHOSTTY_MOUSE_SHAPE_POINTER: return CUI_CURSOR_HAND;
    case GHOSTTY_MOUSE_SHAPE_COL_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_E_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_W_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_EW_RESIZE: return CUI_CURSOR_RESIZE_EW;
    case GHOSTTY_MOUSE_SHAPE_ROW_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_N_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_S_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_NS_RESIZE: return CUI_CURSOR_RESIZE_NS;
    case GHOSTTY_MOUSE_SHAPE_MOVE:
    case GHOSTTY_MOUSE_SHAPE_ALL_SCROLL:
    case GHOSTTY_MOUSE_SHAPE_GRAB:
    case GHOSTTY_MOUSE_SHAPE_GRABBING: return CUI_CURSOR_MOVE;
    case GHOSTTY_MOUSE_SHAPE_NOT_ALLOWED:
    case GHOSTTY_MOUSE_SHAPE_NO_DROP: return CUI_CURSOR_NOT_ALLOWED;
    default: return CUI_CURSOR_ARROW;
    }
}

/* ---- paste confirmation ---- */

#define PREVIEW_MAX 4096 /* bytes of the paste the dialog shows */

/* The dialog for a held paste: the text as it will be sent, then Cancel or
   Paste. Esc and clicking outside cancel. */
static void confirm_paste(cui_ctx *ui, term_view *v) {
    if (!v->paste) return;
    char preview[PREVIEW_MAX + 8];
    size_t n = v->paste_len < PREVIEW_MAX ? v->paste_len : PREVIEW_MAX;
    while (n > 0 && n < v->paste_len && ((unsigned char)v->paste[n] & 0xC0) == 0x80) n--; /* whole characters */
    int lines = 1;
    for (size_t i = 0; i < v->paste_len; i++) lines += v->paste[i] == '\n';
    for (size_t i = 0; i < n; i++) { /* control bytes would not show; the encoder strips them */
        unsigned char c = (unsigned char)v->paste[i];
        preview[i] = (c < 0x20 && c != '\n' && c != '\t') || c == 0x7F ? '?' : (char)c;
    }
    strcpy(preview + n, n < v->paste_len ? "\n..." : "");
    char desc[160];
    if (session_mode(v->s, GHOSTTY_MODE_BRACKETED_PASTE))
        snprintf(desc, sizeof desc, "The text holds the end-of-paste sequence, so what follows it runs as commands.");
    else
        snprintf(desc, sizeof desc, "%d line%s. Each line break sends Enter, so each line runs as a command.", lines,
                 lines == 1 ? "" : "s");
    bool send = false;
    cui_dialog(ui, "Paste into the terminal?", &v->confirming, .description = desc, .width = 560) {
        cui_code_block(ui, "paste-preview", preview, .line_numbers = true, .max_height = 240, .no_header = true);
        cui_dialog_footer(ui) {
            if (cui_button(ui, "Cancel")) v->confirming = false;
            if (cui_button(ui, "Paste", .variant = CUI_BUTTON_DANGER)) send = true, v->confirming = false;
        }
    }
    if (send) session_paste(v->s, v->paste, v->paste_len, true);
    if (!v->confirming) {
        free(v->paste);
        v->paste = NULL;
        cuix_focus(ui, v->id);
    }
}

/* ---- drawing ---- */

/* One cell of the row being drawn. */
typedef struct term_cell {
    cui_color fg, bg;
    bool has_bg, bold;
    uint8_t span;      /* 2 for a wide character */
    uint8_t underline; /* GHOSTTY_STYLE_UNDERLINE: 0 none, 2 double */
    bool strike, over;
    uint8_t len;       /* of g; 0: no ink */
    uint8_t g[46];     /* the grapheme cluster, UTF-8; longer ones draw blank */
} term_cell;

static cui_color rgb(GhosttyColorRgb c) { return (cui_color){c.r, c.g, c.b, 255}; }
static bool same_color(cui_color a, cui_color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

/* Read the iterator's current row into v->line; returns its cell count. */
static int read_row(term_view *v, const GhosttyRenderStateColors *c) {
    int n = 0;
    while (ghostty_render_state_row_cells_next(v->cells)) {
        if (n == v->line_cap) {
            int cap = v->line_cap ? v->line_cap * 2 : 256;
            term_cell *p = realloc(v->line, (size_t)cap * sizeof *p);
            if (!p) break;
            v->line = p, v->line_cap = cap;
        }
        term_cell *t = &v->line[n++];
        memset(t, 0, sizeof *t);
        GhosttyCell raw = 0;
        GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
        ghostty_render_state_row_cells_get(v->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
        ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
        t->span = wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : wide == GHOSTTY_CELL_WIDE_SPACER_TAIL ? 0 : 1;
        GhosttyStyle st;
        ghostty_style_default(&st);
        ghostty_render_state_row_cells_get(v->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &st);
        GhosttyColorRgb fg, bg;
        if (ghostty_render_state_row_cells_get(v->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg) != GHOSTTY_SUCCESS)
            fg = c->foreground;
        t->has_bg = ghostty_render_state_row_cells_get(v->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) ==
                    GHOSTTY_SUCCESS;
        if (!t->has_bg) bg = c->background;
        bool selected = false;
        ghostty_render_state_row_cells_get(v->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_SELECTED, &selected);
        if (!!(st.flags & GHOSTTY_STYLE_FLAG_INVERSE) != selected) { /* a selected inverse cell shows plain */
            GhosttyColorRgb tmp = fg;
            fg = bg, bg = tmp;
            t->has_bg = true;
        }
        t->fg = rgb(fg), t->bg = rgb(bg);
        if (st.flags & GHOSTTY_STYLE_FLAG_FAINT) t->fg.a = 140;
        t->bold = st.flags & GHOSTTY_STYLE_FLAG_BOLD;
        t->underline = (uint8_t)GHOSTTY_STYLE_UNDERLINE(st);
        t->strike = st.flags & GHOSTTY_STYLE_FLAG_STRIKETHROUGH;
        t->over = st.flags & GHOSTTY_STYLE_FLAG_OVERLINE;
        GhosttyBuffer gb = {t->g, sizeof t->g, 0};
        if (t->span && !(st.flags & GHOSTTY_STYLE_FLAG_INVISIBLE) &&
            ghostty_render_state_row_cells_get(v->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &gb) ==
                GHOSTTY_SUCCESS)
            t->len = (uint8_t)gb.len;
    }
    return n;
}

static void draw_text(cui_ctx *ui, const term_view *v, float x, float y, cui_color col, bool bold, const char *s, int n) {
    cui_draw_text(ui, CUI_FONT_MONO, v->size, (cui_vec2){x, y}, col, s, n);
    /* no bold monospace face is built in: overstrike, as terminals once did */
    if (bold) cui_draw_text(ui, CUI_FONT_MONO, v->size, (cui_vec2){x + 0.6f, y}, col, s, n);
}

/* The code point when g is exactly one, else 0. */
static uint32_t single_codepoint(const uint8_t *g, int n) {
    int need = g[0] < 0x80 ? 1 : g[0] >= 0xF0 ? 4 : g[0] >= 0xE0 ? 3 : g[0] >= 0xC0 ? 2 : 0;
    if (!need || need != n) return 0;
    uint32_t c = need == 1 ? g[0] : g[0] & (0x7F >> need);
    for (int i = 1; i < n; i++) c = c << 6 | (g[i] & 0x3F);
    return c;
}

/* A cell drawn on its own: box drawing as rectangles, other glyphs centred in
   the cell, since a fallback font's advance rarely matches the grid. */
static void draw_glyph(cui_ctx *ui, const term_view *v, const term_cell *t, float x, float y, float text_dy, cui_color col) {
    cui_rect cell = {x, y, v->cell_w * t->span, v->cell_h};
    if (boxdraw(ui, single_codepoint(t->g, t->len), cell, col)) return;
    float w = cui_measure_text(ui, CUI_FONT_MONO, v->size, (const char *)t->g, t->len).width;
    float dx = w < cell.w ? (cell.w - w) / 2 : 0;
    draw_text(ui, v, x + dx, y + text_dy, col, t->bold, (const char *)t->g, t->len);
}

/* Backgrounds first, merged into runs of one color with edges on whole pixels so
   neighbours leave no seam; then text, ASCII in runs of one style. */
static void draw_row(cui_ctx *ui, const term_view *v, int n, float x0, float y, float text_dy) {
    float cw = v->cell_w, ch = v->cell_h;
    for (int i = 0; i < n;) {
        if (!v->line[i].has_bg) { i++; continue; }
        int j = i + 1;
        while (j < n && v->line[j].has_bg && same_color(v->line[j].bg, v->line[i].bg)) j++;
        float a = roundf(x0 + i * cw), b = roundf(x0 + j * cw);
        cui_draw_rect(ui, (cui_rect){a, y, b - a, ch}, CUI_CORNERS(0), v->line[i].bg);
        i = j;
    }
    char run[2048];
    int len = 0;
    float run_x = 0;
    cui_color run_col = {0};
    bool run_bold = false;
    float ty = y + text_dy;
    for (int i = 0; i <= n; i++) {
        const term_cell *t = i < n ? &v->line[i] : NULL;
        bool ascii = t && t->span == 1 && t->len == 1 && t->g[0] < 0x80;
        bool joins = len && t && (!t->len ? t->span != 0 : ascii && same_color(t->fg, run_col) && t->bold == run_bold);
        if (joins && len < (int)sizeof run) {
            run[len++] = t->len ? (char)t->g[0] : ' '; /* a space has no ink, so it joins any run */
        } else {
            if (len) draw_text(ui, v, run_x, ty, run_col, run_bold, run, len);
            len = 0;
            if (ascii) {
                run[len++] = (char)t->g[0];
                run_x = x0 + i * cw, run_col = t->fg, run_bold = t->bold;
            } else if (t && t->len) {
                draw_glyph(ui, v, t, x0 + i * cw, y, text_dy, t->fg);
            }
        }
        if (!t || !t->span) continue;
        float px = x0 + i * cw, w = cw * t->span;
        if (t->underline) cui_draw_rect(ui, (cui_rect){px, y + ch - 2, w, t->underline == 2 ? 2 : 1}, CUI_CORNERS(0), t->fg);
        if (t->strike) cui_draw_rect(ui, (cui_rect){px, y + ch * 0.55f, w, 1}, CUI_CORNERS(0), t->fg);
        if (t->over) cui_draw_rect(ui, (cui_rect){px, y, w, 1}, CUI_CORNERS(0), t->fg);
    }
}

static void paint(cui_ctx *ui, cui_rect rect, void *user) {
    term_view *v = *(term_view **)user;
    GhosttyRenderStateColors c = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    GhosttyRenderStateCursor cur = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
    if (ghostty_render_state_get(v->rs, GHOSTTY_RENDER_STATE_DATA_COLORS, &c) != GHOSTTY_SUCCESS) return;
    ghostty_render_state_get(v->rs, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cur);
    cui_draw_rect(ui, rect, CUI_CORNERS(0), rgb(c.background));
    if (ghostty_render_state_get(v->rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &v->rows) != GHOSTTY_SUCCESS) return;
    cui_push_clip(ui, rect);
    float cw = v->cell_w, ch = v->cell_h, x0 = rect.x + PAD;
    float text_dy = (ch - cui_measure_text(ui, CUI_FONT_MONO, v->size, "M", 1).height) * 0.5f;
    term_cell under = {0}; /* the cell under the cursor, redrawn on a block cursor */
    for (uint16_t y = 0; ghostty_render_state_row_iterator_next(v->rows); y++) {
        if (ghostty_render_state_row_get(v->rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &v->cells) != GHOSTTY_SUCCESS)
            continue;
        int n = read_row(v, &c);
        draw_row(ui, v, n, x0, rect.y + PAD + y * ch, text_dy);
        if (cur.viewport_has_value && y == cur.viewport_y && cur.viewport_x < n) under = v->line[cur.viewport_x];
    }
    if (cur.visible && cur.viewport_has_value) {
        cui_rect cr = {x0 + cur.viewport_x * cw, rect.y + PAD + cur.viewport_y * ch, cw * (under.span == 2 ? 2 : 1), ch};
        cui_color cc = rgb(c.cursor_has_value ? c.cursor : c.foreground);
        GhosttyCursorStyle style = v->focused ? cur.visual_style : GHOSTTY_CURSOR_STYLE_BLOCK_HOLLOW;
        if (style == GHOSTTY_CURSOR_STYLE_BLOCK) {
            cui_draw_rect(ui, cr, CUI_CORNERS(0), cc);
            if (under.len) draw_glyph(ui, v, &under, cr.x, cr.y, text_dy, rgb(c.background));
        } else if (style == GHOSTTY_CURSOR_STYLE_BAR) {
            cui_draw_rect(ui, (cui_rect){cr.x, cr.y, 2, ch}, CUI_CORNERS(0), cc);
        } else if (style == GHOSTTY_CURSOR_STYLE_UNDERLINE) {
            cui_draw_rect(ui, (cui_rect){cr.x, cr.y + ch - 2, cr.w, 2}, CUI_CORNERS(0), cc);
        } else {
            cui_draw_rect_border(ui, cr, CUI_CORNERS(0), (cui_color){0, 0, 0, 0}, cc, 1);
        }
    }
    GhosttyTerminalScrollbar sb = {0};
    if (ghostty_terminal_get(v->s->term, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &sb) == GHOSTTY_SUCCESS &&
        sb.offset + sb.len < sb.total) { /* scrolled into history: show where */
        float h = rect.h * (float)sb.len / (float)sb.total, top = rect.h * (float)sb.offset / (float)sb.total;
        cui_draw_rect(ui, (cui_rect){rect.x + rect.w - 5, rect.y + top, 3, h < 12 ? 12 : h}, CUI_CORNERS(1.5f),
                      (cui_color){c.foreground.r, c.foreground.g, c.foreground.b, 110});
    }
    cui_pop_clip(ui);
    ghostty_render_state_clean(v->rs);
}

void term_view_frame(cui_ctx *ui, term_view *v, const char *id) {
    session *s = v->s;
    double now = cui_time(ui);
    cui_text_metrics tm = cui_measure_text(ui, CUI_FONT_MONO, v->size, "M", 1);
    v->cell_w = tm.width > 0 ? tm.width : v->size * 0.6f;
    v->cell_h = ceilf(v->size * 1.35f);

    term_view *self = v;
    bool first = !v->id;
    v->id = cui_make_id(ui, id);
    cui_canvas_result r = cui_canvas(ui, id, CUI_FILL, CUI_FILL, paint, &self, sizeof self);
    v->rect = r.rect;
    if (r.rect.w > 0 && r.rect.h > 0) {
        int cols = (int)((r.rect.w - 2 * PAD) / v->cell_w), rows = (int)((r.rect.h - 2 * PAD) / v->cell_h);
        session_resize(s, (uint16_t)(cols < 2 ? 2 : cols > 1000 ? 1000 : cols), (uint16_t)(rows < 1 ? 1 : rows > 500 ? 500 : rows),
                       (uint32_t)v->cell_w, (uint32_t)v->cell_h);
    }
    if (first) cuix_focus(ui, v->id);
    handle_mouse(ui, v, &r);
    v->focused = cuix_focused(ui, v->id);
    bool has_focus = v->focused && cuix_window_focused(ui);
    if (has_focus != v->reported_focus) session_focus(s, has_focus), v->reported_focus = has_focus;
    if (v->focused && !v->paste) { /* while the dialog is open, keys are its */
        cuix_own_tab(ui, v->id);
        if (handle_keys(ui, v)) v->last_io = now;
    }
    if (r.hovered) cuix_set_cursor(ui, pointer_shape(s));
    confirm_paste(ui, v);

    size_t max = 1 << 20; /* per frame, so a flood of output still lets frames through */
    size_t n = session_pump(s, max, 8);
    if (n) v->last_io = now;
    if (s->held && !v->held_since) v->held_since = now;
    if (!s->held) v->held_since = 0;
    if (!s->held || now - v->held_since > HOLD_MAX) ghostty_render_state_update(v->rs, s->term);

    /* No thread wakes the window when the child writes, so poll: fast while
       busy, slowly when idle. */
    cui_request_wakeup(ui, n ? 0 : now - v->last_io < 1 ? 1 / 60.0f : 0.1f);
}
