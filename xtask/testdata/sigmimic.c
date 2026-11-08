// SPDX-License-Identifier: GPL-2.0-or-later
// Mimics how Dropbear runs `ssh host command`: three pipes, fork+exec of a shell, SIGCHLD with a self-pipe, select() on the pipes.
// Prints what the parent saw; "mimic: done" only when the child's output reached EOF AND the child was reaped.
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int selfp[2];
static volatile int got_chld, reaped_in_handler, handler_status;
static volatile pid_t child_pid;
/* like Dropbear: the handler itself calls waitpid(pid, WNOHANG) exactly once per signal, with no retry later */
static void on_chld(int s) {
    (void)s; got_chld = 1;
    int st;
    if (child_pid > 0 && waitpid(child_pid, &st, WNOHANG) == child_pid) { reaped_in_handler = 1; handler_status = st; }
    (void)!write(selfp[1], "x", 1);
}

int main(void) {
    int in[2], out[2], err[2];
    if (pipe(in) || pipe(out) || pipe(err) || pipe(selfp)) return 1;
    struct sigaction sa = { .sa_handler = on_chld };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, 0);
    pid_t c = fork();
    child_pid = c;
    if (c == 0) {
        dup2(in[0], 0); dup2(out[1], 1); dup2(err[1], 2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]); close(err[0]); close(err[1]); close(selfp[0]); close(selfp[1]);
        execl("/busybox", "sh", "-c", "echo mimic-child-output", (char *)0);
        _exit(127);
    }
    close(in[0]); close(out[1]); close(err[1]);                      /* the parent keeps in[1] open, as Dropbear does until the channel closes */
    int out_eof = 0, err_eof = 0, reaped = 0, status = 0;
    time_t t0 = time(0);
    char b[256];
    while (!(out_eof && err_eof && reaped) && time(0) - t0 < 8) {
        fd_set r; FD_ZERO(&r); int mx = selfp[0];
        FD_SET(selfp[0], &r);
        if (!out_eof) { FD_SET(out[0], &r); if (out[0] > mx) mx = out[0]; }
        if (!err_eof) { FD_SET(err[0], &r); if (err[0] > mx) mx = err[0]; }
        struct timeval tv = { 1, 0 };
        int n = select(mx + 1, &r, 0, 0, &tv);
        if (n < 0 && errno != EINTR) { printf("mimic: select error %d\n", errno); return 1; }
        if (n > 0 && FD_ISSET(out[0], &r)) { ssize_t k = read(out[0], b, sizeof b); if (k > 0) { b[k] = 0; printf("mimic: stdout %s", b); } else { out_eof = 1; puts("mimic: stdout EOF"); } }
        if (n > 0 && FD_ISSET(err[0], &r)) { ssize_t k = read(err[0], b, sizeof b); if (k <= 0) { err_eof = 1; puts("mimic: stderr EOF"); } }
        if (n > 0 && FD_ISSET(selfp[0], &r)) { (void)!read(selfp[0], b, 1); puts("mimic: SIGCHLD seen"); }
        if (!reaped && reaped_in_handler) { reaped = 1; status = handler_status; printf("mimic: child reaped in the SIGCHLD handler, status %d\n", WEXITSTATUS(status)); }
    }
    printf("mimic: %s (out_eof %d err_eof %d reaped %d got_chld %d)\n", out_eof && err_eof && reaped ? "done" : "HANG", out_eof, err_eof, reaped, got_chld);
    if (!reaped) { int st; pid_t r = waitpid(c, &st, WNOHANG); printf("mimic: late waitpid says %d\n", (int)r); }
    return 0;
}
