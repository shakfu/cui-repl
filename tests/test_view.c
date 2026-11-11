/* test_view.c - term_view's input mapping, through a windowless CUI context:
   keys and text go in as a platform layer would send them, and the child (od in
   raw mode) shows in hex the bytes it got. */
#define _DEFAULT_SOURCE
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "check.h"
#include "term_view.h"

#include <poll.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static char clip_in[256], clip_out[256];
static void set_clip(void *u, const char *s) { (void)u, snprintf(clip_out, sizeof clip_out, "%s", s); }
static const char *get_clip(void *u) { return (void)u, clip_in; }

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

typedef struct {
    cui_ctx *ui;
    session s;
    term_view v;
} rig;

static void frame(rig *r) {
    cui_begin_frame(r->ui, 1 / 60.0f);
    term_view_frame(r->ui, &r->v, "terminal");
    cui_end_frame(r->ui);
}

/* Frames until the screen shows needle; the screen text, malloc'd. */
static char *frames_until(rig *r, const char *needle) {
    char *text = NULL;
    for (double end = now() + 5; now() < end;) {
        struct pollfd pfd = {.fd = r->s.pty.fd, .events = POLLIN};
        poll(&pfd, 1, 10);
        frame(r);
        free(text);
        text = session_format(&r->s, false);
        if (text && strstr(text, needle)) break;
    }
    return text;
}

/* A child that runs setup, then prints in hex the next count bytes it reads. */
static void open_rig_with(rig *r, const char *setup, int count) {
    char script[512];
    snprintf(script, sizeof script,
             "stty raw -echo; %sprintf READY; "
             "r=$(dd bs=1 count=%d 2>/dev/null | od -An -tx1 | tr -d ' \\n'); printf '\\r\\nGOT[%%s]' \"$r\"; sleep 5",
             setup, count);
    char *argv[] = {"sh", "-c", script, NULL};
    r->ui = cui_create(&(cui_ctx_desc){.theme = CUI_THEME_DARK});
    cui_set_viewport(r->ui, 800, 400, 1);
    cui_set_platform(r->ui, &(cui_platform_iface){.set_clipboard = set_clip, .get_clipboard = get_clip});
    CHECK(session_open(&r->s, argv, NULL, 80, 24) == 0);
    CHECK(term_view_init(&r->v, &r->s, 13) == 0);
    free(frames_until(r, "READY"));
}

static void open_rig(rig *r, int count) { open_rig_with(r, "", count); }

static char *close_rig(rig *r) {
    char *text = frames_until(r, "]");
    char *got = text ? strstr(text, "GOT[") : NULL;
    char *out = got ? strdup(got) : NULL;
    free(text);
    term_view_free(&r->v);
    session_close(&r->s);
    cui_destroy(r->ui);
    return out;
}

/* A key press as a platform sends it: the key, then its text if it types any. */
static void press(rig *r, cui_key k, int mods, const char *text) {
    cui_input_key(r->ui, k, true, mods);
    if (text) cui_input_text(r->ui, text);
    frame(r);
    cui_input_key(r->ui, k, false, mods);
    frame(r);
}

/* The centre of a cell, in window coordinates. */
static void mouse_to(rig *r, int col, int row) {
    cui_input_mouse_move(r->ui, r->v.rect.x + 6 + (col + 0.5f) * r->v.cell_w, r->v.rect.y + 6 + (row + 0.5f) * r->v.cell_h);
    frame(r);
}

static void mouse_button(rig *r, cui_mouse_button b, bool down) {
    cui_input_mouse_button(r->ui, b, down);
    frame(r);
}

static void set_mods(rig *r, int mods) { cui_input_key(r->ui, CUI_KEY_NONE, false, mods); }

/* Normal tracking (press and release) in SGR format. */
#define MOUSE_SGR "printf '\\033[?1000h\\033[?1006h'; "

#if defined(__APPLE__)
#define PASTE_MODS CUI_MOD_SUPER
#else
#define PASTE_MODS (CUI_MOD_CTRL | CUI_MOD_SHIFT)
#endif

TEST(typing_sends_each_character_once) {
    rig r;
    open_rig(&r, 4);
    press(&r, CUI_KEY_L, 0, "l");
    press(&r, CUI_KEY_S, CUI_MOD_SHIFT, "S");
    press(&r, CUI_KEY_SPACE, 0, " ");
    press(&r, CUI_KEY_ENTER, 0, NULL);
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[6c53200d]");
    free(got);
}

TEST(ctrl_and_special_keys_encode) {
    rig r;
    open_rig(&r, 7);
    press(&r, CUI_KEY_C, CUI_MOD_CTRL, NULL);         /* 03 */
    press(&r, CUI_KEY_TAB, CUI_MOD_SHIFT, NULL);      /* CSI Z */
    press(&r, CUI_KEY_BACKSPACE, 0, NULL);            /* 7f */
    press(&r, CUI_KEY_ESCAPE, 0, NULL);               /* 1b */
    press(&r, CUI_KEY_TAB, 0, NULL);                  /* 09: Tab types, not moves focus */
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[031b5b5a7f1b09]");
    free(got);
}

TEST(paste_shortcut_sends_clipboard) {
    rig r;
    snprintf(clip_in, sizeof clip_in, "hi");
    open_rig(&r, 2);
    press(&r, CUI_KEY_V, PASTE_MODS, PASTE_MODS == CUI_MOD_SUPER ? "v" : NULL);
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[6869]");
    free(got);
}

#if defined(__APPLE__)
TEST(command_shortcuts_send_nothing) {
    rig r;
    open_rig(&r, 1);
    press(&r, CUI_KEY_K, CUI_MOD_SUPER, "k");
    press(&r, CUI_KEY_X, 0, "x");
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[78]");
    free(got);
}
#endif

TEST(copy_shortcut_copies_selection) {
    char *argv[] = {"sh", "-c", "printf 'hello world'; sleep 5", NULL};
    rig r;
    r.ui = cui_create(&(cui_ctx_desc){.theme = CUI_THEME_DARK});
    cui_set_viewport(r.ui, 800, 400, 1);
    cui_set_platform(r.ui, &(cui_platform_iface){.set_clipboard = set_clip, .get_clipboard = get_clip});
    CHECK(session_open(&r.s, argv, NULL, 80, 24) == 0);
    CHECK(term_view_init(&r.v, &r.s, 13) == 0);
    free(frames_until(&r, "hello world"));
    GhosttyGridRef a = {0}, b = {0};
    GhosttyPoint pa = {.tag = GHOSTTY_POINT_TAG_VIEWPORT, .value = {.coordinate = {.x = 0, .y = 0}}};
    GhosttyPoint pb = {.tag = GHOSTTY_POINT_TAG_VIEWPORT, .value = {.coordinate = {.x = 4, .y = 0}}};
    ghostty_terminal_grid_ref(r.s.term, pa, &a);
    ghostty_terminal_grid_ref(r.s.term, pb, &b);
    ghostty_terminal_set(r.s.term, GHOSTTY_TERMINAL_OPT_SELECTION, &(GhosttySelection){.start = a, .end = b});
    clip_out[0] = 0;
    press(&r, CUI_KEY_C, PASTE_MODS, NULL);
    CHECK(!strcmp(clip_out, "hello"));
    term_view_free(&r.v);
    session_close(&r.s);
    cui_destroy(r.ui);
}

TEST(unsafe_paste_waits_for_cancel) {
    rig r;
    snprintf(clip_in, sizeof clip_in, "rm -rf ~\n");
    open_rig(&r, 1);
    press(&r, CUI_KEY_V, PASTE_MODS, PASTE_MODS == CUI_MOD_SUPER ? "v" : NULL);
    CHECK(r.v.paste != NULL);
    press(&r, CUI_KEY_ENTER, 0, NULL); /* the dialog has the keys: no Enter reaches the shell */
    press(&r, CUI_KEY_ESCAPE, 0, NULL);
    CHECK(r.v.paste == NULL);
    press(&r, CUI_KEY_X, 0, "x");
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[78]");
    free(got);
}

TEST(unsafe_paste_sends_once_confirmed) {
    rig r;
    snprintf(clip_in, sizeof clip_in, "a\nb");
    open_rig(&r, 3);
    press(&r, CUI_KEY_V, PASTE_MODS, PASTE_MODS == CUI_MOD_SUPER ? "v" : NULL);
    CHECK(r.v.paste != NULL);
    press(&r, CUI_KEY_TAB, CUI_MOD_SHIFT, NULL); /* to the last control: Paste */
    press(&r, CUI_KEY_ENTER, 0, NULL);
    CHECK(r.v.paste == NULL);
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[610d62]"); /* unbracketed: the newline went as Enter */
    free(got);
}

TEST(click_reports_sgr_when_tracking) {
    rig r;
    open_rig_with(&r, MOUSE_SGR, 18);
    mouse_to(&r, 2, 1);
    mouse_button(&r, CUI_MOUSE_LEFT, true);
    mouse_button(&r, CUI_MOUSE_LEFT, false);
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[1b5b3c303b333b324d1b5b3c303b333b326d]"); /* CSI < 0;3;2 M, then m */
    free(got);
}

TEST(right_button_and_wheel_report_when_tracking) {
    rig r;
    open_rig_with(&r, MOUSE_SGR, 18 + 3 * 10);
    mouse_to(&r, 0, 0);
    mouse_button(&r, CUI_MOUSE_RIGHT, true);
    mouse_button(&r, CUI_MOUSE_RIGHT, false);
    cui_input_scroll(r.ui, 0, 1); /* one notch: 3 lines, button 4 (64) each */
    frame(&r);
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[1b5b3c323b313b314d1b5b3c323b313b316d"
                        "1b5b3c36343b313b314d1b5b3c36343b313b314d1b5b3c36343b313b314d]");
    free(got);
}

TEST(shift_drag_selects_when_tracking) {
    char *argv[] = {"sh", "-c", "printf '\033[?1000h\033[?1006hhello world'; stty raw -echo; "
                                "r=$(dd bs=1 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n'); "
                                "printf '\r\nGOT[%s]' \"$r\"; sleep 5", NULL};
    rig r;
    r.ui = cui_create(&(cui_ctx_desc){.theme = CUI_THEME_DARK});
    cui_set_viewport(r.ui, 800, 400, 1);
    cui_set_platform(r.ui, &(cui_platform_iface){.set_clipboard = set_clip, .get_clipboard = get_clip});
    CHECK(session_open(&r.s, argv, NULL, 80, 24) == 0);
    CHECK(term_view_init(&r.v, &r.s, 13) == 0);
    free(frames_until(&r, "hello world"));
    CHECK(session_mouse_tracking(&r.s));
    set_mods(&r, CUI_MOD_SHIFT);
    mouse_to(&r, 0, 0);
    mouse_button(&r, CUI_MOUSE_LEFT, true);
    mouse_to(&r, 4, 0);
    mouse_button(&r, CUI_MOUSE_LEFT, false);
    set_mods(&r, 0);
    char *sel = session_format(&r.s, true);
    CHECK(sel && !strcmp(sel, "hello"));
    free(sel);
    press(&r, CUI_KEY_X, 0, "x");
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[78]"); /* the drag sent nothing */
    free(got);
}

TEST(drag_selects_without_tracking) {
    char *argv[] = {"sh", "-c", "printf 'hello world'; sleep 5", NULL};
    rig r;
    r.ui = cui_create(&(cui_ctx_desc){.theme = CUI_THEME_DARK});
    cui_set_viewport(r.ui, 800, 400, 1);
    CHECK(session_open(&r.s, argv, NULL, 80, 24) == 0);
    CHECK(term_view_init(&r.v, &r.s, 13) == 0);
    free(frames_until(&r, "hello world"));
    mouse_to(&r, 6, 0);
    mouse_button(&r, CUI_MOUSE_LEFT, true);
    mouse_to(&r, 10, 0);
    mouse_button(&r, CUI_MOUSE_LEFT, false);
    char *sel = session_format(&r.s, true);
    CHECK(sel && !strcmp(sel, "world"));
    free(sel);
    term_view_free(&r.v);
    session_close(&r.s);
    cui_destroy(r.ui);
}

TEST(focus_reports_only_when_enabled) {
    rig r;
    open_rig_with(&r, "printf '\\033[?1004h'; ", 6);
    cui_input_focus(r.ui, false);
    frame(&r);
    cui_input_focus(r.ui, true);
    frame(&r);
    char *got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[1b5b4f1b5b49]"); /* CSI O, CSI I */
    free(got);

    open_rig(&r, 1);
    cui_input_focus(r.ui, false);
    frame(&r);
    cui_input_focus(r.ui, true);
    frame(&r);
    press(&r, CUI_KEY_X, 0, "x");
    got = close_rig(&r);
    CHECK_CONTAINS(got, "GOT[78]");
    free(got);
}

TEST(pointer_follows_osc_22) {
    rig r;
    open_rig_with(&r, "printf '\\033]22;pointer\\033\\\\'; ", 1);
    mouse_to(&r, 1, 1);
    frame(&r);
    CHECK(cui_get_cursor(r.ui) == CUI_CURSOR_HAND);
    free(close_rig(&r));
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
