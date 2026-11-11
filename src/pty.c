/* pty.c - see pty.h. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif
#include "pty.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

/* environ with each NAME=value in add replacing an entry of that NAME. Only the
   array is allocated; the strings are borrowed. */
static char **merge_env(char *const add[]) {
    size_t n = 0, k = 0;
    while (environ[n]) n++;
    while (add && add[k]) k++;
    char **out = malloc((n + k + 1) * sizeof *out);
    if (!out) return NULL;
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        const char *eq = strchr(environ[i], '=');
        size_t len = eq ? (size_t)(eq - environ[i]) + 1 : strlen(environ[i]);
        bool replaced = false;
        for (size_t j = 0; j < k && !replaced; j++) replaced = !strncmp(environ[i], add[j], len);
        if (!replaced) out[m++] = environ[i];
    }
    for (size_t j = 0; j < k; j++) out[m++] = add[j];
    out[m] = NULL;
    return out;
}

static void fail(const char *what) {
    /* only async-signal-safe calls: we are a child of a possibly threaded parent */
    const char *e = strerror(errno);
    (void)!write(2, what, strlen(what));
    (void)!write(2, ": ", 2);
    (void)!write(2, e, strlen(e));
    (void)!write(2, "\r\n", 2);
}

int pty_spawn(pty_proc *p, char *const argv[], const char *cwd, char *const env[], uint16_t cols,
              uint16_t rows) {
    memset(p, 0, sizeof *p);
    p->fd = -1;
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0) return -1;
    if (grantpt(m) < 0 || unlockpt(m) < 0) goto fail_master;
    const char *name = ptsname(m);
    if (!name) goto fail_master;
    char slave[256];
    snprintf(slave, sizeof slave, "%s", name);
    struct winsize ws = {.ws_row = rows, .ws_col = cols};
    ioctl(m, TIOCSWINSZ, &ws);
    char **envp = merge_env(env);
    if (!envp) goto fail_master;
    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 4096) maxfd = 4096;

    pid_t pid = fork();
    if (pid < 0) {
        free(envp);
        goto fail_master;
    }
    if (pid == 0) {
        setsid();
        int s = open(slave, O_RDWR);
        if (s < 0) { fail("open pty"); _exit(127); }
        ioctl(s, TIOCSCTTY, 0); /* Linux takes the tty on open; BSD and macOS need this */
        ioctl(s, TIOCSWINSZ, &ws); /* macOS resets the size the master set when the slave first opens */
        dup2(s, 0), dup2(s, 1), dup2(s, 2);
        for (int fd = 3; fd < maxfd; fd++) close(fd); /* the window system's sockets, our master */
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        static const int sigs[] = {SIGPIPE, SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGCHLD, SIGTSTP, SIGTTIN, SIGTTOU};
        for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++) signal(sigs[i], SIG_DFL);
        if (cwd && chdir(cwd) < 0) { fail(cwd); _exit(127); }
        environ = envp;
        execvp(argv[0], argv);
        fail(argv[0]);
        _exit(127);
    }
    free(envp);
    fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK);
    fcntl(m, F_SETFD, FD_CLOEXEC);
    p->fd = m;
    p->pid = pid;
    return 0;

fail_master:;
    int e = errno;
    close(m);
    errno = e;
    return -1;
}

ssize_t pty_read(pty_proc *p, void *buf, size_t cap) {
    if (p->fd < 0) return -1;
    for (;;) {
        ssize_t n = read(p->fd, buf, cap);
        if (n > 0) return n;
        if (n == 0) return -1;
        if (errno == EINTR) continue;
        return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1; /* EIO: the child side closed */
    }
}

ssize_t pty_write(pty_proc *p, const void *data, size_t n) {
    if (p->fd < 0) return -1;
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(p->fd, (const char *)data + done, n - done);
        if (w > 0) { done += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        return done ? (ssize_t)done : -1;
    }
    return (ssize_t)done;
}

int pty_resize(pty_proc *p, uint16_t cols, uint16_t rows, uint16_t width_px, uint16_t height_px) {
    if (p->fd < 0) return -1;
    struct winsize ws = {.ws_row = rows, .ws_col = cols, .ws_xpixel = width_px, .ws_ypixel = height_px};
    return ioctl(p->fd, TIOCSWINSZ, &ws);
}

bool pty_poll_exit(pty_proc *p) {
    if (p->exited) return true;
    if (p->pid <= 0) return false;
    int st;
    pid_t r = waitpid(p->pid, &st, WNOHANG);
    if (r == p->pid) {
        p->status = st;
        p->exited = true;
        p->pid = 0;
    }
    return p->exited;
}

int pty_exit_code(const pty_proc *p) {
    if (!p->exited) return -1;
    return WIFEXITED(p->status) ? WEXITSTATUS(p->status) : 128 + WTERMSIG(p->status);
}

void pty_close(pty_proc *p, int grace_ms) {
    if (p->fd >= 0) close(p->fd);
    p->fd = -1;
    if (p->exited || p->pid <= 0) return;
    pid_t pid = p->pid;
    kill(-pid, SIGHUP);
    kill(pid, SIGHUP); /* in case it left its own group */
    struct timespec tick = {0, 10 * 1000 * 1000};
    for (int t = 0; t < grace_ms && !pty_poll_exit(p); t += 10) nanosleep(&tick, NULL);
    if (pty_poll_exit(p)) return;
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    p->status = st;
    p->exited = true;
    p->pid = 0;
}
