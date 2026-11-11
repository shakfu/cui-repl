/* pty.h - a child process on a pseudo-terminal. POSIX only; no terminal emulation. */
#ifndef CUI_REPL_PTY_H
#define CUI_REPL_PTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    int fd;      /* the master side, non-blocking; -1 when closed */
    pid_t pid;   /* the child, a session leader; 0 once reaped */
    int status;  /* waitpid status, valid once exited */
    bool exited;
} pty_proc;

/* Run argv[0] (searched in PATH) on a new pty of cols x rows, in cwd (NULL: ours).
   env adds NAME=value entries, NULL-terminated, over the inherited environment.
   Returns 0, or -1 with errno set. A failed exec shows as exit status 127. */
int pty_spawn(pty_proc *p, char *const argv[], const char *cwd, char *const env[], uint16_t cols,
              uint16_t rows);

/* Bytes read (> 0), 0 when nothing is waiting, -1 once the child side is gone. */
ssize_t pty_read(pty_proc *p, void *buf, size_t cap);

/* Bytes written, possibly fewer than n when the pty buffer is full; -1 on error. */
ssize_t pty_write(pty_proc *p, const void *data, size_t n);

/* Report a new size to the child (SIGWINCH). Pixel sizes may be 0. */
int pty_resize(pty_proc *p, uint16_t cols, uint16_t rows, uint16_t width_px, uint16_t height_px);

/* Reap the child if it exited; true once it has. Does not block. */
bool pty_poll_exit(pty_proc *p);

/* Exit code as a shell reports it: the status, or 128 + signal. -1 before exit. */
int pty_exit_code(const pty_proc *p);

/* Hang up: close the master, SIGHUP the child's process group, SIGKILL after
   grace_ms, then reap. Safe to call twice. */
void pty_close(pty_proc *p, int grace_ms);

#endif
