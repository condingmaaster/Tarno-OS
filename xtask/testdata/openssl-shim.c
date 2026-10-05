// SPDX-License-Identifier: GPL-2.0-or-later
// `openssl s_client -quiet -connect host:port -servername sni`, the only thing BusyBox wget asks of the
// openssl helper: connect, then let BusyBox's own ssl_client speak TLS over that socket (stdin/stdout stay
// the plaintext side). Note: ssl_client does not verify certificates.
#define _GNU_SOURCE
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char host[256] = "", port[16] = "443", sni[256] = "";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-connect") && i + 1 < argc) {
            char *c = strrchr(argv[++i], ':');
            if (c) { snprintf(host, sizeof host, "%.*s", (int)(c - argv[i]), argv[i]); snprintf(port, sizeof port, "%s", c + 1); }
            else snprintf(host, sizeof host, "%s", argv[i]);
        } else if (!strcmp(argv[i], "-servername") && i + 1 < argc) snprintf(sni, sizeof sni, "%s", argv[++i]);
    }
    if (!host[0]) { fputs("openssl: only `s_client -connect host:port` is supported\n", stderr); return 1; }
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *res;
    if (getaddrinfo(host, port, &hints, &res) != 0) { fprintf(stderr, "openssl: cannot resolve %s\n", host); return 1; }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0 || connect(s, res->ai_addr, res->ai_addrlen) != 0) { fprintf(stderr, "openssl: cannot connect to %s:%s\n", host, port); return 1; }
    char fd[16]; snprintf(fd, sizeof fd, "%d", s);
    execl("/busybox", "ssl_client", "-s", fd, "-n", sni[0] ? sni : host, (char *)0);
    perror("exec ssl_client");
    return 127;
}
