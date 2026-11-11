/* cuix.c - CUI itself plus the accessors in cuix.h, which read struct cui_ctx. */
#include "cui.c"

#include "cuix.h"

int cuix_take_text(cui_ctx *ui, char *buf, int cap) {
    int n = ui->text_in_len < cap - 1 ? ui->text_in_len : cap - 1;
    if (n < 0) n = 0;
    memcpy(buf, ui->text_in, (size_t)n);
    buf[n] = 0;
    ui->text_in_len = 0;
    ui->text_in[0] = 0;
    return n;
}

void cuix_focus(cui_ctx *ui, cui_id id) { ui->focus = id; }
bool cuix_window_focused(cui_ctx *ui) { return ui->window_focused; }
bool cuix_focused(cui_ctx *ui, cui_id id) { return id && ui->focus == id; }
void cuix_own_tab(cui_ctx *ui, cui_id id) { ui->tab_owner = id; }
void cuix_eat_key(cui_ctx *ui, cui_key key) { ui->kpressed[key] = 0; }

float cuix_take_scroll(cui_ctx *ui) {
    float dy = ui->scroll_dy;
    ui->scroll_dy = 0;
    return dy;
}

void cuix_set_cursor(cui_ctx *ui, cui_cursor cursor) { cui__set_cursor(ui, cursor); }

void cuix_set_clipboard(cui_ctx *ui, const char *utf8) {
    if (ui->platform.set_clipboard) ui->platform.set_clipboard(ui->platform.user, utf8);
}

const char *cuix_clipboard(cui_ctx *ui) {
    return ui->platform.get_clipboard ? ui->platform.get_clipboard(ui->platform.user) : NULL;
}
