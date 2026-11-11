/* boxdraw.h - box-drawing lines (U+2500-257F) and block elements (U+2580-259F)
   drawn as rectangles that fill the cell, so they join across cells; a font's
   glyphs leave gaps at a terminal's line height. */
#ifndef CUI_REPL_BOXDRAW_H
#define CUI_REPL_BOXDRAW_H

#include "cui.h"

#include <stdbool.h>
#include <stdint.h>

/* Draw cp into cell; false when cp is not one it draws (leave it to the font). */
bool boxdraw(cui_ctx *ui, uint32_t cp, cui_rect cell, cui_color color);

#endif
