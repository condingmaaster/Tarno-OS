// SPDX-License-Identifier: GPL-2.0-or-later
// How a daemon like Dropbear learns that a session's shell exited: SIGCHLD handler -> write to a self-pipe -> select() -> waitpid().
// 40 rounds; the children exit at once, after a short sleep, and after writing into a pipe. Reports how many rounds the parent was told in time.
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int selfpipe[2];
static void handler(int sig) { int e = errno; (void)sig; (void)!write(selfpipe[1], "x", 1); errno = e; }

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handler;
    sa.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, 0);
    if (pipe(selfpipe)) return 1;
    int told = 0, reaped = 0, lost = 0;
    for (int r = 0; r < 40; r++) {
        int mode = r % 3;
        int out[2];
        if (pipe(out)) return 1;
        pid_t p = fork();
        if (p == 0) {
            close(out[0]);
            if (mode == 1) usleep(30000);
            if (mode == 2) { (void)!write(out[1], "hello\n", 6); usleep(10000); }
            _exit(r);
        }
        close(out[1]);
        /* wait up to 3 s for the notification; drain the pipe; reap with waitpid. `told` = the child was reaped after a notification arrived,
           `lost` = no notification came although the child had exited (it was only found by polling after the 3 s) */
        int got = 0, done = 0;
        for (int step = 0; step < 30 && !done; step++) {
            fd_set rf;
            struct timeval tv = { 0, 100000 };
            FD_ZERO(&rf);
            FD_SET(selfpipe[0], &rf);
            int n = select(selfpipe[0] + 1, &rf, 0, 0, &tv);
            if (n > 0 && FD_ISSET(selfpipe[0], &rf)) {
                char c[64];
                (void)!read(selfpipe[0], c, sizeof c);
                got = 1;
            }
            int st;
            pid_t w = waitpid(p, &st, WNOHANG);
            if (w == p) { done = 1; if (WIFEXITED(st) && WEXITSTATUS(st) == r) reaped++; }
        }
        for (int k = 0; k < 10 && !got; k++) {                  /* the handler may run a moment after the child was found (signals are delivered on return to user mode) */
            fd_set rf;
            struct timeval tv = { 0, 20000 };
            FD_ZERO(&rf);
            FD_SET(selfpipe[0], &rf);
            if (select(selfpipe[0] + 1, &rf, 0, 0, &tv) > 0) { char c[64]; (void)!read(selfpipe[0], c, sizeof c); got = 1; }
        }
        if (done && got) told++; else lost++;
        if (!done) { int st; waitpid(p, &st, 0); }
        close(out[0]);
    }
    printf("sigchld: told %d, reaped %d, lost %d of 40\n", told, reaped, lost);
    /* a daemon blocked in select() with a long timeout must be woken by SIGCHLD (the handler writes the self-pipe): how long does it sleep? */
    int slow = 0;
    for (int r = 0; r < 6; r++) {
        pid_t p = fork();
        if (p == 0) { usleep(50000); _exit(0); }
        fd_set rf;
        struct timeval tv = { 4, 0 };
        FD_ZERO(&rf);
        FD_SET(selfpipe[0], &rf);
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        select(selfpipe[0] + 1, &rf, 0, 0, &tv);
        clock_gettime(CLOCK_MONOTONIC, &b);
        long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
        char c[64];
        (void)!read(selfpipe[0], c, 0);
        fd_set rf2; struct timeval z = { 0, 0 }; FD_ZERO(&rf2); FD_SET(selfpipe[0], &rf2);
        if (select(selfpipe[0] + 1, &rf2, 0, 0, &z) > 0) (void)!read(selfpipe[0], c, sizeof c);
        int st; waitpid(p, &st, 0);
        printf("sigchld: blocked select woke after %ld ms\n", ms);
        if (ms > 1500) slow++;
    }
    printf("sigchld: slow wakeups %d of 6\n", slow);
    return slow ? 1 : (lost ? 1 : 0);
}
