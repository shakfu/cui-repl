/* test_session.c - session.c: real children on a pty, emulated by libghostty-vt.
   Key and paste tests run `od -tx1` in raw mode, so the screen shows the bytes sent in hex. */
#define _DEFAULT_SOURCE
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "check.h"
#include "session.h"

#include <poll.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void pump_once(session *s) {
    if (s->pty.fd >= 0) {
        struct pollfd pfd = {.fd = s->pty.fd, .events = POLLIN};
        poll(&pfd, 1, 20);
    } else {
        usleep(20000);
    }
    session_pump(s, 1 << 20, 0);
}

/* Pump until the screen shows needle; the screen text, malloc'd. */
static char *wait_screen(session *s, const char *needle) {
    char *text = NULL;
    for (double end = now() + 5; now() < end; ) {
        pump_once(s);
        free(text);
        text = session_format(s, false);
        if (text && strstr(text, needle)) break;
        if (s->exited) break;
    }
    return text;
}

static void wait_exit(session *s) {
    for (double end = now() + 5; !s->exited && now() < end; ) pump_once(s);
}

/* The bytes a key or paste sends, in hex. ready runs first, so a test can set modes before its keys go. */
static char *sent_bytes(int count, const char *ready, void (*send)(session *)) {
    char script[512];
    snprintf(script, sizeof script,
             "stty raw -echo; printf '%s'; printf READY; "
             "r=$(dd bs=1 count=%d 2>/dev/null | od -An -tx1 | tr -d ' \\n'); printf '\\r\\nGOT[%%s]' \"$r\"",
             ready, count);
    char *argv[] = {"sh", "-c", script, NULL};
    session s;
    if (session_open(&s, argv, NULL, 120, 10) < 0) return NULL;
    free(wait_screen(&s, "READY"));
    send(&s);
    char *text = wait_screen(&s, "]");
    session_close(&s);
    char *got = text ? strstr(text, "GOT[") : NULL;
    char *out = got ? strdup(got) : NULL;
    free(text);
    return out;
}

TEST(screen_reflects_cursor_movement) {
    char *argv[] = {"sh", "-c", "printf 'abc\\rX'; printf '\\033[2;5Hhere'; printf '\\033[1;31mred\\033[0m'", NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 40, 5) == 0);
    wait_exit(&s);
    char *text = session_format(&s, false);
    CHECK_CONTAINS(text, "Xbc");
    CHECK_CONTAINS(text, "    herered");
    free(text);
    session_close(&s);
}

TEST(clear_screen_drops_old_text) {
    char *argv[] = {"sh", "-c", "printf 'old text'; printf '\\033[2J\\033[Hnew'", NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 40, 5) == 0);
    wait_exit(&s);
    char *text = session_format(&s, false);
    CHECK_CONTAINS(text, "new");
    CHECK(text && !strstr(text, "old text"));
    free(text);
    session_close(&s);
}

TEST(alt_screen_and_title) {
    char *argv[] = {"sh", "-c", "printf '\\033]0;my title\\007\\033[?1049hfull screen'; sleep 0.3", NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 40, 5) == 0);
    free(wait_screen(&s, "full screen"));
    CHECK(session_alt_screen(&s));
    CHECK(s.title_changed);
    CHECK(!strcmp(s.title, "my title"));
    wait_exit(&s);
    session_close(&s);
}

TEST(exit_code) {
    char *argv[] = {"sh", "-c", "exit 7", NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 40, 5) == 0);
    wait_exit(&s);
    CHECK(s.exited);
    CHECK(session_exit_code(&s) == 7);
    session_close(&s);
}

TEST(device_status_reply_reaches_child) {
    /* DSR 5n: the terminal answers ESC [ 0 n through write_pty */
    char *argv[] = {"sh", "-c",
                    "stty raw -echo; printf '\\033[5n'; "
                    "r=$(dd bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' \\n'); printf 'GOT[%s]' \"$r\"",
                    NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 80, 5) == 0);
    char *text = wait_screen(&s, "]");
    CHECK_CONTAINS(text, "GOT[1b5b306e]");
    free(text);
    session_close(&s);
}

/* What the terminal answers to query, as hex, on a 100x10 grid of 9x18 px cells. */
static char *reply_to(const char *query, int count) {
    char script[512];
    snprintf(script, sizeof script,
             "stty raw -echo; printf '%s'; "
             "r=$(dd bs=1 count=%d 2>/dev/null | od -An -tx1 | tr -d ' \\n'); printf 'GOT[%%s]' \"$r\"",
             query, count);
    char *argv[] = {"sh", "-c", script, NULL};
    session s;
    if (session_open(&s, argv, NULL, 80, 5) < 0) return NULL;
    session_resize(&s, 100, 10, 9, 18); /* before the first pump, so before the query is read */
    char *text = wait_screen(&s, "]");
    session_close(&s);
    char *got = text ? strstr(text, "GOT[") : NULL;
    char *out = got ? strdup(got) : NULL;
    free(text);
    return out;
}

TEST(size_queries_answer) {
    char *got = reply_to("\\033[18t", 11);
    CHECK_CONTAINS(got, "GOT[1b5b383b31303b313030" "74]"); /* CSI 8;10;100 t */
    free(got);
    got = reply_to("\\033[16t", 9);
    CHECK_CONTAINS(got, "GOT[1b5b363b31383b39" "74]"); /* CSI 6;18;9 t */
    free(got);
    got = reply_to("\\033[14t", 12);
    CHECK_CONTAINS(got, "GOT[1b5b343b3138303b393030" "74]"); /* CSI 4;180;900 t */
    free(got);
}

static void resize_100x10(session *s) { session_resize(s, 100, 10, 9, 18); }

TEST(in_band_resize_report) {
    char *got = sent_bytes(20, "\\033[?2048h", resize_100x10); /* mode 2048 */
    CHECK_CONTAINS(got, "GOT[1b5b34383b31303b3130303b3138303b393030" "74]"); /* CSI 48;10;100;180;900 t */
    free(got);
}

TEST(background_query_answers) {
    /* OSC 11 ?: programs read the background this way (vim, fish, delta) */
    char *got = reply_to("\\033]11;?\\033\\\\", 25);
    CHECK_CONTAINS(got, "GOT[1b5d31313b7267623a303030302f303030302f303030301b5c]"); /* rgb:0000/0000/0000 */
    free(got);
}

TEST(color_scheme_follows_background) {
    char *got = reply_to("\\033[?996n", 9);
    CHECK_CONTAINS(got, "GOT[1b5b3f3939373b316e]"); /* CSI ? 997;1 n: dark */
    free(got);
    got = reply_to("\\033]11;#ffffff\\033\\\\\\033[?996n", 9); /* OSC 11 white, then ask */
    CHECK_CONTAINS(got, "GOT[1b5b3f3939373b326e]"); /* light */
    free(got);
}

static void send_up(session *s) { session_key(s, GHOSTTY_KEY_ARROW_UP, 0, NULL); }
static void send_enter(session *s) { session_key(s, GHOSTTY_KEY_ENTER, 0, NULL); }
static void send_ctrl_c(session *s) { session_key(s, GHOSTTY_KEY_C, GHOSTTY_MODS_CTRL, "c"); }
static void send_backspace(session *s) { session_key(s, GHOSTTY_KEY_BACKSPACE, 0, NULL); }
static void send_text(session *s) { session_text(s, "h\xc3\xa9", 3); }
static GhosttyResult paste_result;
static void paste_two_lines(session *s) { paste_result = session_paste(s, "a\nb", 3, true); }
static void paste_with_escape(session *s) { paste_result = session_paste(s, "x\033y", 3, false); }
/* An unsafe paste without consent, then "x" to show what got through. */
static void paste_unconfirmed(session *s, const char *text) {
    paste_result = session_paste(s, text, strlen(text), false);
    session_text(s, "x", 1);
}
static void paste_line_unconfirmed(session *s) { paste_unconfirmed(s, "rm -rf ~\n"); }
static void paste_end_bracket_unconfirmed(session *s) { paste_unconfirmed(s, "a\033[201~b"); }
static void paste_lines_bracketed(session *s) { paste_result = session_paste(s, "a\nb", 3, false); }

TEST(arrow_key_follows_cursor_key_mode) {
    char *got = sent_bytes(3, "", send_up);
    CHECK_CONTAINS(got, "GOT[1b5b41]");
    free(got);
    got = sent_bytes(3, "\\033[?1h", send_up); /* DECCKM: application cursor keys */
    CHECK_CONTAINS(got, "GOT[1b4f41]");
    free(got);
}

TEST(control_keys) {
    char *got = sent_bytes(1, "", send_enter);
    CHECK_CONTAINS(got, "GOT[0d]");
    free(got);
    got = sent_bytes(1, "", send_ctrl_c);
    CHECK_CONTAINS(got, "GOT[03]");
    free(got);
    got = sent_bytes(1, "", send_backspace);
    CHECK_CONTAINS(got, "GOT[7f]");
    free(got);
}

TEST(text_passes_through) {
    char *got = sent_bytes(3, "", send_text);
    CHECK_CONTAINS(got, "GOT[68c3a9]");
    free(got);
}

TEST(paste_brackets_when_asked) {
    char *got = sent_bytes(3, "", paste_two_lines);
    CHECK_CONTAINS(got, "GOT[610d62]"); /* unbracketed: newline becomes Enter */
    free(got);
    got = sent_bytes(15, "\\033[?2004h", paste_two_lines);
    CHECK_CONTAINS(got, "GOT[1b5b3230307e610a621b5b3230317e]");
    free(got);
}

TEST(unsafe_paste_needs_consent) {
    char *got = sent_bytes(1, "", paste_line_unconfirmed);
    CHECK(paste_result == GHOSTTY_REJECTED);
    CHECK_CONTAINS(got, "GOT[78]"); /* the paste sent nothing */
    free(got);
    got = sent_bytes(1, "\\033[?2004h", paste_end_bracket_unconfirmed); /* would close the bracket early */
    CHECK(paste_result == GHOSTTY_REJECTED);
    CHECK_CONTAINS(got, "GOT[78]");
    free(got);
}

TEST(bracketed_lines_paste_without_consent) {
    char *got = sent_bytes(15, "\\033[?2004h", paste_lines_bracketed);
    CHECK(paste_result == GHOSTTY_SUCCESS);
    CHECK_CONTAINS(got, "GOT[1b5b3230307e610a621b5b3230317e]");
    free(got);
}

TEST(paste_strips_escape) {
    char *got = sent_bytes(3, "", paste_with_escape);
    CHECK_CONTAINS(got, "GOT[782079]"); /* ESC becomes a space */
    free(got);
}

TEST(resize_updates_grid_and_child) {
    char *argv[] = {"sh", "-c", "trap 'stty size; exit 0' WINCH; echo ready; while :; do sleep 0.05; done", NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 80, 24) == 0);
    free(wait_screen(&s, "ready"));
    session_resize(&s, 100, 30, 8, 16);
    CHECK(s.cols == 100 && s.rows == 30);
    char *text = wait_screen(&s, "30 100");
    CHECK_CONTAINS(text, "30 100");
    free(text);
    session_close(&s);
}

TEST(selection_copies_text) {
    char *argv[] = {"sh", "-c", "printf 'hello world'", NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 40, 5) == 0);
    wait_exit(&s);
    CHECK(session_format(&s, true) == NULL); /* nothing selected */
    GhosttyGridRef a = {0}, b = {0};
    GhosttyPoint pa = {.tag = GHOSTTY_POINT_TAG_VIEWPORT, .value = {.coordinate = {.x = 6, .y = 0}}};
    GhosttyPoint pb = {.tag = GHOSTTY_POINT_TAG_VIEWPORT, .value = {.coordinate = {.x = 10, .y = 0}}};
    CHECK(ghostty_terminal_grid_ref(s.term, pa, &a) == GHOSTTY_SUCCESS);
    CHECK(ghostty_terminal_grid_ref(s.term, pb, &b) == GHOSTTY_SUCCESS);
    GhosttySelection sel = {.start = a, .end = b};
    CHECK(ghostty_terminal_set(s.term, GHOSTTY_TERMINAL_OPT_SELECTION, &sel) == GHOSTTY_SUCCESS);
    char *text = session_format(&s, true);
    CHECK(text && !strcmp(text, "world"));
    free(text);
    session_close(&s);
}

TEST(fast_output_is_not_paced_by_frames) {
    /* 1.3 MB through a ~1 KB pty buffer, pumped once per 60 Hz frame: one drain
       per frame would take about 20 s, the 8 ms budget well under 5 */
    char *argv[] = {"sh", "-c", "seq 1 200000", NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 80, 24) == 0);
    double t0 = now();
    while (!s.exited && now() - t0 < 30) {
        session_pump(&s, 1 << 20, 8);
        usleep(16000);
    }
    CHECK(s.exited);
    CHECK(now() - t0 < 5);
    char *text = session_format(&s, false);
    CHECK_CONTAINS(text, "200000");
    free(text);
    session_close(&s);
}

TEST(close_after_failed_open_is_safe) {
    char *argv[] = {"/nonexistent/cui-repl-no-such-program", NULL};
    session s;
    CHECK(session_open(&s, argv, NULL, 40, 5) == 0); /* the exec fails in the child */
    wait_exit(&s);
    CHECK(session_exit_code(&s) == 127);
    session_close(&s);
    session_close(&s);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
