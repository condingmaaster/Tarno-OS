/* Winsock over loopback: a listener and a client in one thread. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { puts("ws FAIL startup"); return 1; }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(8099); a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(ls, (struct sockaddr *)&a, sizeof a) != 0 || listen(ls, 1) != 0) { printf("ws FAIL bind/listen %d\n", WSAGetLastError()); return 1; }
    SOCKET c = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(c, (struct sockaddr *)&a, sizeof a) != 0) { printf("ws FAIL connect %d\n", WSAGetLastError()); return 1; }
    SOCKET s = accept(ls, 0, 0);
    if (s == INVALID_SOCKET) { puts("ws FAIL accept"); return 1; }
    send(c, "ping-over-winsock", 17, 0);
    char buf[64] = { 0 }; int n = recv(s, buf, sizeof buf - 1, 0);
    printf("ws server got %d: %s\n", n, buf);
    send(s, "pong", 4, 0);
    fd_set rs; FD_ZERO(&rs); FD_SET(c, &rs); struct timeval tv = { 2, 0 };
    int ready = select(0, &rs, 0, 0, &tv);
    n = recv(c, buf, sizeof buf - 1, 0); buf[n > 0 ? n : 0] = 0;
    printf("ws client got %s select=%d\n", buf, ready);
    struct addrinfo hints; memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = 0; int gr = getaddrinfo("127.0.0.1", "80", &hints, &res);
    printf("ws getaddrinfo=%d port=%d\n", gr, res ? ntohs(((struct sockaddr_in *)res->ai_addr)->sin_port) : -1);
    closesocket(c); closesocket(s); closesocket(ls); WSACleanup();
    puts("wsock-done");
    return 0;
}
