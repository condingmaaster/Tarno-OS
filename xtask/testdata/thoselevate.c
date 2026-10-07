// SPDX-License-Identifier: GPL-2.0-or-later
// thoselevate <program> [args...]: ask the administrator password (typed masked) and run the program as root through the
// THOS-native elevate() call, then wait for it. Only the administrator account may do this.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define THOS_BASE 0x54480000L
static long thos3(long n, long a, long b, long c) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}
int main(int argc, char **argv) {
    if (argc < 2) { fputs("usage: thoselevate <program> [args...]\n", stderr); return 2; }
    char pw[128];
    printf("Administrator password: ");
    fflush(stdout);
    long n;
    struct termios old, t, chk;
    int pty = 0;
    if (isatty(0) && tcgetattr(0, &old) == 0) {            /* a terminal window (pty) really switches the echo off; the text console ignores the request */
        t = old; t.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(0, TCSANOW, &t);
        pty = tcgetattr(0, &chk) == 0 && !(chk.c_lflag & ECHO);
        if (!pty) tcsetattr(0, TCSANOW, &old);
    }
    if (pty) {
        n = fgets(pw, sizeof pw, stdin) ? (long)strlen(pw) : -1;
        tcsetattr(0, TCSANOW, &old);
        while (n > 0 && (pw[n - 1] == '\n' || pw[n - 1] == '\r')) pw[--n] = 0;
    } else n = thos3(THOS_BASE + 1, (long)pw, sizeof pw, 0);          /* the text console: the kernel reads it masked */
    putchar('\n');
    if (n < 0) { fputs("cannot read the password\n", stderr); return 1; }
    long pid = thos3(THOS_BASE, (long)argv[1], (long)&argv[1], (long)pw);
    memset(pw, 0, sizeof pw);
    if (pid == -13) { fputs("wrong password\n", stderr); return 1; }
    if (pid == -1) { fputs("only the administrator can do this\n", stderr); return 1; }
    if (pid < 0) { fprintf(stderr, "cannot start %s (error %ld)\n", argv[1], pid); return 1; }
    int st = 0;
    waitpid((pid_t)pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
