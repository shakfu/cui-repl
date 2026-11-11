/* cuix.h - CUI internals a terminal widget needs that cui.h does not expose:
   typed text, keyboard and window focus, Tab, the wheel and the clipboard. cuix.c compiles
   them into the same unit as cui.c, so a CUI upgrade that renames a field breaks
   the build here and nowhere else. */
#ifndef CUI_REPL_CUIX_H
#define CUI_REPL_CUIX_H

#include "cui.h"

/* The text typed this frame, consumed so no other widget sees it. Control
   characters never arrive here; CUI reports those as keys. Returns its length. */
int cuix_take_text(cui_ctx *ui, char *buf, int cap);

void cuix_focus(cui_ctx *ui, cui_id id);
bool cuix_window_focused(cui_ctx *ui);
bool cuix_focused(cui_ctx *ui, cui_id id);

/* Next frame, Tab reaches the focused id instead of moving focus. Call each frame. */
void cuix_own_tab(cui_ctx *ui, cui_id id);

/* Consume a key press so CUI's own shortcuts skip it. */
void cuix_eat_key(cui_ctx *ui, cui_key key);

/* Wheel lines this frame (+ is up), consumed. */
float cuix_take_scroll(cui_ctx *ui);

void cuix_set_cursor(cui_ctx *ui, cui_cursor cursor); /* the pointer shape, this frame */

void cuix_set_clipboard(cui_ctx *ui, const char *utf8);
const char *cuix_clipboard(cui_ctx *ui); /* NULL when unavailable; valid until the next call */

#endif
