// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
// THOS event descriptors: eventfd, timerfd, signalfd, epoll (level + edge), poll wake-up latency.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/wait.h>

#define CHECK(c, m) do { if (!(c)) { printf("ev FAIL: %s (errno %d)\n", m, errno); fflush(stdout); return 1; } } while (0)

static int64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }
static int64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000LL + t.tv_nsec / 1000; }

int main(void) {
    // 1. eventfd: counter, semaphore, non-blocking
    int ef = eventfd(0, EFD_NONBLOCK);
    CHECK(ef >= 0, "eventfd");
    uint64_t v = 3;
    CHECK(write(ef, &v, 8) == 8, "eventfd write");
    v = 4; write(ef, &v, 8);
    CHECK(read(ef, &v, 8) == 8 && v == 7, "eventfd read sums");
    CHECK(read(ef, &v, 8) < 0 && errno == EAGAIN, "eventfd empty -> EAGAIN");
    int es = eventfd(2, EFD_SEMAPHORE | EFD_NONBLOCK);
    CHECK(read(es, &v, 8) == 8 && v == 1 && read(es, &v, 8) == 8 && v == 1 && read(es, &v, 8) < 0, "eventfd semaphore");

    // 2. epoll + eventfd (level-triggered)
    int ep = epoll_create1(EPOLL_CLOEXEC);
    CHECK(ep >= 0, "epoll_create1");
    struct epoll_event e = { .events = EPOLLIN, .data.u64 = 0xE0 };
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, ef, &e) == 0, "epoll_ctl add eventfd");
    struct epoll_event out[8];
    int64_t t0 = now_ms();
    CHECK(epoll_wait(ep, out, 8, 100) == 0, "epoll_wait times out with nothing ready");
    int64_t waited = now_ms() - t0;
    CHECK(waited >= 90 && waited < 400, "epoll_wait timeout length");
    v = 1; write(ef, &v, 8);
    CHECK(epoll_wait(ep, out, 8, 1000) == 1 && out[0].data.u64 == 0xE0 && (out[0].events & EPOLLIN), "eventfd readiness through epoll");
    CHECK(epoll_wait(ep, out, 8, 0) == 1, "level-triggered: still ready until read");
    read(ef, &v, 8);
    CHECK(epoll_wait(ep, out, 8, 0) == 0, "not ready after read");

    // 3. timerfd: one-shot and periodic, observed through epoll
    int tf = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    CHECK(tf >= 0, "timerfd_create");
    struct itimerspec its = { .it_value = { 0, 60 * 1000000 } };
    CHECK(timerfd_settime(tf, 0, &its, NULL) == 0, "timerfd_settime");
    e.data.u64 = 0x7F; CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, tf, &e) == 0, "add timerfd");
    t0 = now_ms();
    CHECK(epoll_wait(ep, out, 8, 2000) == 1 && out[0].data.u64 == 0x7F, "timerfd fires");
    waited = now_ms() - t0;
    CHECK(waited >= 50 && waited < 300, "timerfd one-shot delay about 60 ms");
    CHECK(read(tf, &v, 8) == 8 && v == 1, "timerfd read count 1");
    its.it_value.tv_nsec = 20 * 1000000; its.it_interval.tv_nsec = 20 * 1000000;
    timerfd_settime(tf, 0, &its, NULL);
    usleep(250000);
    CHECK(read(tf, &v, 8) == 8 && v >= 5 && v <= 14, "periodic timerfd counted expirations");
    struct itimerspec cur;
    CHECK(timerfd_gettime(tf, &cur) == 0 && cur.it_interval.tv_nsec == 20 * 1000000, "timerfd_gettime");
    epoll_ctl(ep, EPOLL_CTL_DEL, tf, NULL);
    close(tf);

    // 4. pipe through epoll: wake-up latency (child stamps the time into the pipe)
    int pp[2]; CHECK(pipe(pp) == 0, "pipe");
    e.events = EPOLLIN; e.data.u64 = 0x91;
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, pp[0], &e) == 0, "add pipe");
    pid_t c = fork();
    if (c == 0) {
        usleep(120000);
        int64_t t = now_us();
        write(pp[1], &t, 8);
        _exit(0);
    }
    CHECK(epoll_wait(ep, out, 8, 3000) == 1 && out[0].data.u64 == 0x91, "pipe readiness through epoll");
    int64_t woke = now_us(), stamp = 0;
    CHECK(read(pp[0], &stamp, 8) == 8, "pipe read");
    int64_t lat = woke - stamp;
    printf("ev: epoll wake-up latency %lld us\n", (long long)lat);
    CHECK(lat < 8000, "epoll wake-up latency under 8 ms (event driven, not a poll loop)");
    waitpid(c, 0, 0);

    // same through poll()
    c = fork();
    if (c == 0) { usleep(120000); int64_t t = now_us(); write(pp[1], &t, 8); _exit(0); }
    struct pollfd pf = { pp[0], POLLIN, 0 };
    CHECK(poll(&pf, 1, 3000) == 1 && (pf.revents & POLLIN), "poll readiness");
    woke = now_us();
    CHECK(read(pp[0], &stamp, 8) == 8, "pipe read 2");
    printf("ev: poll wake-up latency %lld us\n", (long long)(woke - stamp));
    CHECK(woke - stamp < 8000, "poll wake-up latency under 8 ms");
    waitpid(c, 0, 0);

    // 5. socketpair + edge-triggered + HUP
    int sp[2]; CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0, "socketpair");
    e.events = EPOLLIN | EPOLLET | EPOLLRDHUP; e.data.u64 = 0x55;
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, sp[0], &e) == 0, "add socket (edge)");
    write(sp[1], "ab", 2);
    CHECK(epoll_wait(ep, out, 8, 1000) == 1 && out[0].data.u64 == 0x55, "edge: data arrives");
    CHECK(epoll_wait(ep, out, 8, 50) == 0, "edge: not reported again while nothing new happened");
    char b[8];
    CHECK(read(sp[0], b, 8) == 2, "drain");
    write(sp[1], "c", 1);
    CHECK(epoll_wait(ep, out, 8, 1000) == 1, "edge: fires again after drain + new data");
    read(sp[0], b, 8);
    close(sp[1]);
    int n = epoll_wait(ep, out, 8, 1000);
    CHECK(n == 1 && (out[0].events & (EPOLLHUP | EPOLLRDHUP | EPOLLIN)), "peer close shows up as HUP/RDHUP");
    // closing the watched descriptor removes it from the interest list
    close(sp[0]);
    CHECK(epoll_wait(ep, out, 8, 30) == 0, "closed descriptor is dropped from the epoll set");
    CHECK(epoll_ctl(ep, EPOLL_CTL_DEL, 9999, NULL) < 0 && errno == EBADF, "ctl on a bad fd");
    CHECK(epoll_ctl(ep, EPOLL_CTL_ADD, pp[0], &e) < 0 && errno == EEXIST, "ctl add twice -> EEXIST");

    // 6. signalfd
    sigset_t m; sigemptyset(&m); sigaddset(&m, SIGUSR1); sigaddset(&m, SIGUSR2);
    CHECK(sigprocmask(SIG_BLOCK, &m, NULL) == 0, "sigprocmask");
    int sf = signalfd(-1, &m, SFD_NONBLOCK);
    CHECK(sf >= 0, "signalfd");
    struct signalfd_siginfo si;
    CHECK(read(sf, &si, sizeof si) < 0 && errno == EAGAIN, "signalfd empty");
    e.events = EPOLLIN; e.data.u64 = 0x5F; epoll_ctl(ep, EPOLL_CTL_ADD, sf, &e);
    kill(getpid(), SIGUSR2);
    CHECK(epoll_wait(ep, out, 8, 1000) >= 1, "signalfd readiness");
    CHECK(read(sf, &si, sizeof si) == sizeof si && si.ssi_signo == SIGUSR2, "signalfd delivers the signal number");

    // 7. inotify accepts watches (and never reports)
    int in = inotify_init1(IN_NONBLOCK);
    CHECK(in >= 0, "inotify_init1");
    CHECK(inotify_add_watch(in, "/tmp", IN_CREATE) > 0, "inotify_add_watch");
    char ib[64];
    CHECK(read(in, ib, sizeof ib) < 0 && errno == EAGAIN, "inotify idle");

    printf("ev ok: eventfd, timerfd, signalfd, epoll level/edge/HUP/timeouts, inotify stub, wake-up latency\n");
    fflush(stdout);
    return 0;
}
