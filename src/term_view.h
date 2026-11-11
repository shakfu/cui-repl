/* term_view.h - a CUI widget that draws a session's screen and sends it keys,
   text, pastes, wheel scrolls, mouse and focus reports. Single-threaded: it pumps the pty each frame. */
#ifndef CUI_REPL_TERM_VIEW_H
#define CUI_REPL_TERM_VIEW_H

#include "cui.h"
#include "session.h"

struct term_cell;

typedef struct {
    session *s;
    float size;                     /* font size in points */
    GhosttyRenderState rs;
    GhosttyRenderStateRowIterator rows;
    GhosttyRenderStateRowCells cells;
    cui_id id;
    cui_rect rect;                  /* the widget, last frame */
    bool focused;                   /* had keyboard focus this frame */
    bool reported_focus;            /* window and keyboard focus, as last reported */
    bool selecting, dragged;        /* a left-button drag is selecting */
    uint16_t press_x, press_y;      /* its first cell, viewport coordinates */
    cui_vec2 press_at;
    float wheel;                    /* wheel lines not yet scrolled */
    uint8_t mdown;                  /* mouse buttons down last frame, bit per cui_mouse_button */
    uint8_t reported;               /* buttons whose press went to the program */
    int mouse_col, mouse_row;       /* cell of the last reported motion */
    double last_io;                 /* the last input or output, for the poll rate */
    double held_since;              /* when a render hold began, 0 when none */
    float cell_w, cell_h;
    char *paste;                    /* an unsafe paste awaiting the user's answer, or NULL */
    size_t paste_len;
    bool confirming;                /* its dialog is open */
    struct term_cell *line;         /* the row being drawn */
    int line_cap;
} term_view;

/* Returns 0, or -1 when libghostty cannot allocate. */
int term_view_init(term_view *v, session *s, float font_size);
void term_view_free(term_view *v);

/* Fill the space given with the terminal; handle this frame's input and output.
   Takes focus on the first frame and when clicked. A paste that could run
   commands opens a dialog that asks first. */
void term_view_frame(cui_ctx *ui, term_view *v, const char *id);

#endif
