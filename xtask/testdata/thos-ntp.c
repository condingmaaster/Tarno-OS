// SPDX-License-Identifier: GPL-2.0-or-later
// thos-ntp [server]: ask a network time server (SNTP, UDP 123) for the time and set the clock (root; the kernel also writes the CMOS clock).
// Exit 0 when the clock was set. Writes a line to the kernel log (/dev/kmsg). Default server: pool.ntp.org.
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static void klog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void klog(const char *fmt, ...) {
    char l[200]; va_list ap; va_start(ap, fmt); int n = vsnprintf(l, sizeof l, fmt, ap); va_end(ap);
    FILE *f = fopen("/dev/kmsg", "w"); if (f) { fwrite(l, 1, (size_t)n, f); fputc('\n', f); fclose(f); }
    fputs(l, stdout); fputc('\n', stdout);
}

int main(int argc, char **argv) {
    const char *server = argc > 1 ? argv[1] : "pool.ntp.org";
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM }, *res = 0;
    if (getaddrinfo(server, "123", &hints, &res) != 0 || !res) { klog("timesync: cannot resolve %s", server); return 1; }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 1;
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        unsigned char pkt[48] = { 0x1b };                                   /* LI 0, version 3, mode 3 (client) */
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        if (sendto(fd, pkt, sizeof pkt, 0, a->ai_addr, a->ai_addrlen) != (ssize_t)sizeof pkt) continue;
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 3000) <= 0) continue;
        unsigned char rsp[96];
        ssize_t n = recv(fd, rsp, sizeof rsp, 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (n < 48 || (rsp[0] & 7) != 4 || rsp[1] == 0) continue;           /* server mode, stratum != 0 */
        uint32_t secs = (uint32_t)rsp[40] << 24 | rsp[41] << 16 | rsp[42] << 8 | rsp[43];
        uint32_t frac = (uint32_t)rsp[44] << 24 | rsp[45] << 16 | rsp[46] << 8 | rsp[47];
        if (secs < 2208988800u + 1700000000u) continue;                     /* not a plausible time */
        long long rtt_ns = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);
        long long unix_ns = ((long long)secs - 2208988800LL) * 1000000000LL + (long long)(((unsigned long long)frac * 1000000000ULL) >> 32) + rtt_ns / 2;
        struct timespec ts = { .tv_sec = unix_ns / 1000000000LL, .tv_nsec = unix_ns % 1000000000LL };
        struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
        long long off = ((long long)ts.tv_sec - now.tv_sec);
        if (syscall(SYS_clock_settime, CLOCK_REALTIME, &ts) != 0) { klog("timesync: cannot set the clock (%s)", strerror(errno)); return 1; }
        klog("timesync: clock set from %s, it was %lld s off (round trip %lld ms)", server, off, rtt_ns / 1000000);
        return 0;
    }
    klog("timesync: no answer from %s", server);
    return 1;
}
