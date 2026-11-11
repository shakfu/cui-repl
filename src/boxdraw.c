/* boxdraw.c - see boxdraw.h. Each line character is four arms from the cell
   centre (up, right, down, left), each none, light, heavy or double. */
#include "boxdraw.h"

#include <math.h>

enum { NO, LT, HV, DB };
#define ARMS(u, r, d, l) (uint8_t)((u) << 6 | (r) << 4 | (d) << 2 | (l))

/* U+2500..U+257F; 0 where the font draws it (diagonals). Dashed lines draw solid. */
static const uint8_t LINES[128] = {
    ARMS(NO, LT, NO, LT), ARMS(NO, HV, NO, HV), ARMS(LT, NO, LT, NO), ARMS(HV, NO, HV, NO), /* 2500 */
    ARMS(NO, LT, NO, LT), ARMS(NO, HV, NO, HV), ARMS(LT, NO, LT, NO), ARMS(HV, NO, HV, NO),
    ARMS(NO, LT, NO, LT), ARMS(NO, HV, NO, HV), ARMS(LT, NO, LT, NO), ARMS(HV, NO, HV, NO),
    ARMS(NO, LT, LT, NO), ARMS(NO, HV, LT, NO), ARMS(NO, LT, HV, NO), ARMS(NO, HV, HV, NO), /* 250C */
    ARMS(NO, NO, LT, LT), ARMS(NO, NO, LT, HV), ARMS(NO, NO, HV, LT), ARMS(NO, NO, HV, HV), /* 2510 */
    ARMS(LT, LT, NO, NO), ARMS(LT, HV, NO, NO), ARMS(HV, LT, NO, NO), ARMS(HV, HV, NO, NO),
    ARMS(LT, NO, NO, LT), ARMS(LT, NO, NO, HV), ARMS(HV, NO, NO, LT), ARMS(HV, NO, NO, HV),
    ARMS(LT, LT, LT, NO), ARMS(LT, HV, LT, NO), ARMS(HV, LT, LT, NO), ARMS(LT, LT, HV, NO), /* 251C */
    ARMS(HV, LT, HV, NO), ARMS(HV, HV, LT, NO), ARMS(LT, HV, HV, NO), ARMS(HV, HV, HV, NO), /* 2520 */
    ARMS(LT, NO, LT, LT), ARMS(LT, NO, LT, HV), ARMS(HV, NO, LT, LT), ARMS(LT, NO, HV, LT),
    ARMS(HV, NO, HV, LT), ARMS(HV, NO, LT, HV), ARMS(LT, NO, HV, HV), ARMS(HV, NO, HV, HV),
    ARMS(NO, LT, LT, LT), ARMS(NO, LT, LT, HV), ARMS(NO, HV, LT, LT), ARMS(NO, HV, LT, HV), /* 252C */
    ARMS(NO, LT, HV, LT), ARMS(NO, LT, HV, HV), ARMS(NO, HV, HV, LT), ARMS(NO, HV, HV, HV), /* 2530 */
    ARMS(LT, LT, NO, LT), ARMS(LT, LT, NO, HV), ARMS(LT, HV, NO, LT), ARMS(LT, HV, NO, HV),
    ARMS(HV, LT, NO, LT), ARMS(HV, LT, NO, HV), ARMS(HV, HV, NO, LT), ARMS(HV, HV, NO, HV),
    ARMS(LT, LT, LT, LT), ARMS(LT, LT, LT, HV), ARMS(LT, HV, LT, LT), ARMS(LT, HV, LT, HV), /* 253C */
    ARMS(HV, LT, LT, LT), ARMS(LT, LT, HV, LT), ARMS(HV, LT, HV, LT), ARMS(HV, LT, LT, HV), /* 2540 */
    ARMS(HV, HV, LT, LT), ARMS(LT, LT, HV, HV), ARMS(LT, HV, HV, LT), ARMS(HV, HV, LT, HV),
    ARMS(LT, HV, HV, HV), ARMS(HV, LT, HV, HV), ARMS(HV, HV, HV, LT), ARMS(HV, HV, HV, HV),
    ARMS(NO, LT, NO, LT), ARMS(NO, HV, NO, HV), ARMS(LT, NO, LT, NO), ARMS(HV, NO, HV, NO), /* 254C */
    ARMS(NO, DB, NO, DB), ARMS(DB, NO, DB, NO), ARMS(NO, DB, LT, NO), ARMS(NO, LT, DB, NO), /* 2550 */
    ARMS(NO, DB, DB, NO), ARMS(NO, NO, LT, DB), ARMS(NO, NO, DB, LT), ARMS(NO, NO, DB, DB),
    ARMS(LT, DB, NO, NO), ARMS(DB, LT, NO, NO), ARMS(DB, DB, NO, NO), ARMS(LT, NO, NO, DB),
    ARMS(DB, NO, NO, LT), ARMS(DB, NO, NO, DB), ARMS(LT, DB, LT, NO), ARMS(DB, LT, DB, NO), /* 255C */
    ARMS(DB, DB, DB, NO), ARMS(LT, NO, LT, DB), ARMS(DB, NO, DB, LT), ARMS(DB, NO, DB, DB), /* 2560 */
    ARMS(NO, DB, LT, DB), ARMS(NO, LT, DB, LT), ARMS(NO, DB, DB, DB), ARMS(LT, DB, NO, DB),
    ARMS(DB, LT, NO, LT), ARMS(DB, DB, NO, DB), ARMS(LT, DB, LT, DB), ARMS(DB, LT, DB, LT),
    ARMS(DB, DB, DB, DB), ARMS(NO, LT, LT, NO), ARMS(NO, NO, LT, LT), ARMS(LT, NO, NO, LT), /* 256C: rounded */
    ARMS(LT, LT, NO, NO), 0, 0, 0,                                                          /* 2570 */
    ARMS(NO, NO, NO, LT), ARMS(LT, NO, NO, NO), ARMS(NO, LT, NO, NO), ARMS(NO, NO, LT, NO), /* 2574 */
    ARMS(NO, NO, NO, HV), ARMS(HV, NO, NO, NO), ARMS(NO, HV, NO, NO), ARMS(NO, NO, HV, NO),
    ARMS(NO, HV, NO, LT), ARMS(LT, NO, HV, NO), ARMS(NO, LT, NO, HV), ARMS(HV, NO, LT, NO), /* 257C */
};

static void box(cui_ctx *ui, float x0, float y0, float x1, float y1, cui_color c) {
    x0 = roundf(x0), y0 = roundf(y0), x1 = roundf(x1), y1 = roundf(y1);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    cui_draw_rect(ui, (cui_rect){x0, y0, x1 - x0, y1 - y0}, CUI_CORNERS(0), c);
}

/* How far past the centre a stroke of an arm reaches, toward the far side of
   the centre. A double arm's stroke on side `side` (the perpendicular arm next
   to it) stops at that arm's near stroke when it is double, at its line when
   single, and runs to the outer corner when it is absent. */
static float reach(int side, int other, float g, float t) {
    if (side == DB) return -g;
    if (side) return 0;
    if (other == DB) return g;
    return other ? t / 2 : g;
}

static void lines(cui_ctx *ui, uint8_t arms, cui_rect r, cui_color c) {
    float t = fmaxf(1, roundf(r.w / 8)), g = roundf(t * 1.5f), cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    int a[4] = {arms >> 6 & 3, arms >> 4 & 3, arms >> 2 & 3, arms & 3}; /* up, right, down, left */
    for (int i = 0; i < 4; i++) {
        if (!a[i]) continue;
        int before = a[(i + 3) % 4], after = a[(i + 1) % 4]; /* the perpendicular arms */
        bool vertical = i == 0 || i == 2;
        /* toward the centre from this arm's end: +1 for up and left, -1 for down and right */
        float dir = i == 0 || i == 3 ? 1 : -1;
        if (a[i] != DB) {
            float w = a[i] == HV ? 2 * t : t, ext = before == DB || after == DB ? g + t / 2 : w / 2;
            if (vertical) box(ui, cx - w / 2, i == 0 ? r.y : cy - ext, cx + w / 2, i == 0 ? cy + ext : r.y + r.h, c);
            else box(ui, i == 3 ? r.x : cx - ext, cy - w / 2, i == 3 ? cx + ext : r.x + r.w, cy + w / 2, c);
            continue;
        }
        /* two light strokes, g either side of the centre line; the stroke at -g
           sits beside the left arm (vertical arms) or the up arm (horizontal) */
        for (int k = 0; k < 2; k++) {
            float off = k ? g : -g;
            int side = vertical ? (k ? a[1] : a[3]) : (k ? a[2] : a[0]);
            int other = vertical ? (k ? a[3] : a[1]) : (k ? a[0] : a[2]);
            float e = reach(side, other, g, t) * dir;
            if (vertical) {
                float x = cx + off;
                if (i == 0) box(ui, x - t / 2, r.y, x + t / 2, cy + e + t / 2, c);
                else box(ui, x - t / 2, cy + e - t / 2, x + t / 2, r.y + r.h, c);
            } else {
                float y = cy + off;
                if (i == 3) box(ui, r.x, y - t / 2, cx + e + t / 2, y + t / 2, c);
                else box(ui, cx + e - t / 2, y - t / 2, r.x + r.w, y + t / 2, c);
            }
        }
    }
}

/* Quadrants of U+2596..U+259F: 1 upper left, 2 upper right, 4 lower left, 8 lower right. */
static const uint8_t QUADS[10] = {4, 8, 1, 1 | 4 | 8, 1 | 8, 1 | 2 | 4, 1 | 2 | 8, 2, 2 | 4, 2 | 4 | 8};

static void blocks(cui_ctx *ui, uint32_t cp, cui_rect r, cui_color c) {
    float x1 = r.x + r.w, y1 = r.y + r.h, mx = r.x + r.w / 2, my = r.y + r.h / 2;
    if (cp == 0x2580) box(ui, r.x, r.y, x1, my, c);
    else if (cp >= 0x2581 && cp <= 0x2588) box(ui, r.x, y1 - r.h * (float)(cp - 0x2580) / 8, x1, y1, c);
    else if (cp >= 0x2589 && cp <= 0x258F) box(ui, r.x, r.y, r.x + r.w * (float)(0x2590 - cp) / 8, y1, c);
    else if (cp == 0x2590) box(ui, mx, r.y, x1, y1, c);
    else if (cp >= 0x2591 && cp <= 0x2593) {
        cui_color s = c;
        s.a = (uint8_t)(c.a * (cp - 0x2590) / 4); /* shades: 25, 50 and 75 percent */
        box(ui, r.x, r.y, x1, y1, s);
    } else if (cp == 0x2594) box(ui, r.x, r.y, x1, r.y + r.h / 8, c);
    else if (cp == 0x2595) box(ui, x1 - r.w / 8, r.y, x1, y1, c);
    else {
        uint8_t q = QUADS[cp - 0x2596];
        if (q & 1) box(ui, r.x, r.y, mx, my, c);
        if (q & 2) box(ui, mx, r.y, x1, my, c);
        if (q & 4) box(ui, r.x, my, mx, y1, c);
        if (q & 8) box(ui, mx, my, x1, y1, c);
    }
}

bool boxdraw(cui_ctx *ui, uint32_t cp, cui_rect cell, cui_color color) {
    if (cp >= 0x2500 && cp < 0x2580 && LINES[cp - 0x2500]) {
        lines(ui, LINES[cp - 0x2500], cell, color);
        return true;
    }
    if (cp >= 0x2580 && cp <= 0x259F) {
        blocks(ui, cp, cell, color);
        return true;
    }
    return false;
}
