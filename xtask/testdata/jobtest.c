// SPDX-License-Identifier: GPL-2.0-or-later
// Job control: SIGSTOP / SIGTSTP stop a process, wait4(WUNTRACED) reports it, SIGCONT resumes it, a blocked
// read survives a stop/continue without EINTR, SIGKILL ends a stopped process.
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

#define CHECK(c, m) do { if (!(c)) { printf("job FAIL: %s\n", m); fflush(stdout); return 1; } } while (0)

int main(void) {
    int p[2];
    CHECK(pipe(p) == 0, "pipe");
    pid_t c = fork();
    if (c == 0) {
        char b[8] = {0};
        ssize_t n = read(p[0], b, sizeof b);              // blocks across the stop
        _exit(n == 3 && !strcmp(b, "abc") ? 0 : 20 + (n < 0 ? errno : 0));
    }
    usleep(200000);
    int st = 0;
    CHECK(kill(c, SIGSTOP) == 0, "kill stop");
    CHECK(waitpid(c, &st, WUNTRACED) == c && WIFSTOPPED(st) && WSTOPSIG(st) == SIGSTOP, "stop reported");
    usleep(100000);
    CHECK(waitpid(c, &st, WUNTRACED | WNOHANG) == 0, "stop reported only once");
    CHECK(kill(c, SIGCONT) == 0, "kill cont");
    usleep(100000);
    CHECK(write(p[1], "abc", 3) == 3, "write");
    CHECK(waitpid(c, &st, 0) == c && WIFEXITED(st) && WEXITSTATUS(st) == 0, "blocked read survived stop+continue");

    // SIGTSTP (what Ctrl+Z sends) stops too; SIGKILL ends a stopped process
    c = fork();
    if (c == 0) { for (;;) pause(); }
    usleep(200000);
    kill(c, SIGTSTP);
    CHECK(waitpid(c, &st, WUNTRACED) == c && WIFSTOPPED(st) && WSTOPSIG(st) == SIGTSTP, "SIGTSTP stops");
    kill(c, SIGKILL);
    CHECK(waitpid(c, &st, 0) == c && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL, "SIGKILL ends a stopped process");
    puts("job ok: SIGSTOP/SIGTSTP/SIGCONT, wait4(WUNTRACED), no EINTR, SIGKILL while stopped");
    return 0;
}
