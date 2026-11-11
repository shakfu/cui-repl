/* test_pty.c - pty.c against real child processes. */
#define _DEFAULT_SOURCE
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "check.h"
#include "pty.h"

#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Read into out until needle shows (NULL: until EOF) or 5 s pass. */
static bool read_until(pty_proc *p, const char *needle, char *out, size_t cap) {
    size_t len = strlen(out);
    double end = now() + 5;
    while (now() < end) {
        if (needle && strstr(out, needle)) return true;
        struct pollfd pfd = {.fd = p->fd, .events = POLLIN};
        poll(&pfd, 1, 50);
        ssize_t n = pty_read(p, out + len, cap - len - 1);
        if (n < 0) break;
        len += (size_t)n;
        out[len] = 0;
    }
    return needle ? strstr(out, needle) != NULL : true;
}

static int wait_exit(pty_proc *p) {
    double end = now() + 5;
    while (!pty_poll_exit(p) && now() < end) usleep(10000);
    return pty_exit_code(p);
}

static int run(char *const argv[], const char *cwd, char *const env[], uint16_t cols, uint16_t rows, char *out,
               size_t cap) {
    pty_proc p;
    out[0] = 0;
    if (pty_spawn(&p, argv, cwd, env, cols, rows) < 0) return -2;
    read_until(&p, NULL, out, cap);
    int code = wait_exit(&p);
    pty_close(&p, 100);
    return code;
}

TEST(output_and_exit_code) {
    char out[4096];
    char *argv[] = {"sh", "-c", "printf hello; exit 3", NULL};
    CHECK(run(argv, NULL, NULL, 80, 24, out, sizeof out) == 3);
    CHECK_CONTAINS(out, "hello");
}

TEST(stdio_is_a_tty) {
    char out[4096];
    char *argv[] = {"sh", "-c", "test -t 0 && test -t 1 && test -t 2 && echo is-tty", NULL};
    CHECK(run(argv, NULL, NULL, 80, 24, out, sizeof out) == 0);
    CHECK_CONTAINS(out, "is-tty");
}

TEST(initial_size) {
    char out[4096];
    char *argv[] = {"sh", "-c", "stty size", NULL};
    CHECK(run(argv, NULL, NULL, 100, 30, out, sizeof out) == 0);
    CHECK_CONTAINS(out, "30 100");
}

TEST(resize_reaches_child) {
    pty_proc p;
    char out[4096] = "";
    char *argv[] = {"sh", "-c", "trap 'stty size; exit 0' WINCH; echo ready; while :; do sleep 0.05; done", NULL};
    CHECK(pty_spawn(&p, argv, NULL, NULL, 80, 24) == 0);
    CHECK(read_until(&p, "ready", out, sizeof out));
    CHECK(pty_resize(&p, 120, 40, 0, 0) == 0);
    CHECK(read_until(&p, "40 120", out, sizeof out));
    CHECK(wait_exit(&p) == 0);
    pty_close(&p, 100);
}

TEST(env_and_cwd) {
    char dir[] = "/tmp/cui-repl-test-XXXXXX", real[PATH_MAX], out[8192];
    CHECK(mkdtemp(dir) != NULL);
    CHECK(realpath(dir, real) != NULL);
    char *env[] = {"TERM=xterm-256color", "CUI_REPL_TEST=yes", NULL};
    char *argv[] = {"sh", "-c", "echo \"term=$TERM test=$CUI_REPL_TEST\"; pwd -P", NULL};
    CHECK(run(argv, dir, env, 80, 24, out, sizeof out) == 0);
    CHECK_CONTAINS(out, "term=xterm-256color test=yes");
    CHECK_CONTAINS(out, real);
    rmdir(dir);
}

TEST(exec_failure_is_127) {
    char out[4096];
    char *argv[] = {"/nonexistent/cui-repl-no-such-program", NULL};
    CHECK(run(argv, NULL, NULL, 80, 24, out, sizeof out) == 127);
    CHECK_CONTAINS(out, "cui-repl-no-such-program");
}

TEST(write_reaches_child) {
    pty_proc p;
    char out[4096] = "";
    char *argv[] = {"sh", "-c", "read -r line; echo \"got:$line\"", NULL};
    CHECK(pty_spawn(&p, argv, NULL, NULL, 80, 24) == 0);
    /* Until the child opens its side, the write takes nothing (macOS: EAGAIN). */
    ssize_t w = 0;
    for (double end = now() + 5; w == 0 && now() < end; usleep(10000)) w = pty_write(&p, "abc\r", 4);
    CHECK(w == 4);
    CHECK(read_until(&p, "got:abc", out, sizeof out));
    CHECK(wait_exit(&p) == 0);
    pty_close(&p, 100);
}

TEST(close_hangs_up_child) {
    pty_proc p;
    char *argv[] = {"sleep", "30", NULL};
    CHECK(pty_spawn(&p, argv, NULL, NULL, 80, 24) == 0);
    double t0 = now();
    pty_close(&p, 200);
    CHECK(p.exited);
    CHECK(now() - t0 < 2);
    CHECK(pty_exit_code(&p) == 128 + SIGHUP || pty_exit_code(&p) == 128 + SIGKILL);
    pty_close(&p, 200); /* twice is safe */
}

TEST(close_kills_child_ignoring_hup) {
    pty_proc p;
    char out[4096] = "";
    char *argv[] = {"sh", "-c", "trap '' HUP; echo ready; while :; do sleep 0.05; done", NULL};
    CHECK(pty_spawn(&p, argv, NULL, NULL, 80, 24) == 0);
    CHECK(read_until(&p, "ready", out, sizeof out));
    pty_close(&p, 100);
    CHECK(p.exited);
    CHECK(pty_exit_code(&p) == 128 + SIGKILL);
}

int main(int argc, char **argv) { return run_tests(argc, argv); }
