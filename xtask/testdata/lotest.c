// SPDX-License-Identifier: GPL-2.0-or-later
// Loopback networking: a TCP echo over 127.0.0.1 between parent and child, and a UDP round trip.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    {
        int u = socket(AF_INET, SOCK_DGRAM, 0), v = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in ua = { .sin_family = AF_INET, .sin_port = htons(5556), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
        if (bind(u, (void *)&ua, sizeof ua) != 0) { puts("lo FAIL: udp bind"); return 1; }
        sendto(v, "dgram", 5, 0, (void *)&ua, sizeof ua);
        char d[16] = {0};
        int n = recv(u, d, sizeof d, 0);
        if (n != 5 || strcmp(d, "dgram")) { printf("lo FAIL: udp got %d\n", n); return 1; }
        puts("lo: udp ok"); fflush(stdout);
    }
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(5555), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (bind(ls, (void *)&a, sizeof a) != 0 || listen(ls, 4) != 0) { puts("lo FAIL: listen"); return 1; }
    puts("lo: listening"); fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        int c = socket(AF_INET, SOCK_STREAM, 0);
        puts("lo: child connecting"); fflush(stdout);
        if (connect(c, (void *)&a, sizeof a) != 0) _exit(2);
        puts("lo: child connected"); fflush(stdout);
        write(c, "hello-lo", 8);
        puts("lo: child wrote"); fflush(stdout);
        char b[16] = {0};
        int n = read(c, b, sizeof b);
        printf("lo: child read %d\n", n); fflush(stdout);
        _exit(n == 8 && !strcmp(b, "HELLO-LO") ? 0 : 3);
    }
    puts("lo: parent accepting"); fflush(stdout);
    int s = accept(ls, 0, 0);
    puts("lo: accepted"); fflush(stdout);
    if (s < 0) { puts("lo FAIL: accept"); return 1; }
    char b[16] = {0};
    int n = read(s, b, sizeof b);
    printf("lo: parent read %d\n", n); fflush(stdout);
    for (int i = 0; i < n; i++) if (b[i] >= 'a' && b[i] <= 'z') b[i] -= 32;
    write(s, b, n);
    puts("lo: parent echoed"); fflush(stdout);
    int st; waitpid(p, &st, 0);
    puts("lo: child reaped"); fflush(stdout);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { printf("lo FAIL: client status %d\n", st); return 1; }
    puts("lo: tcp ok"); fflush(stdout);
    puts("lo ok: TCP echo and UDP over 127.0.0.1");
    return 0;
}
