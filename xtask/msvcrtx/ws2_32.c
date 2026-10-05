// ws2_32.dll — Winsock on top of the kernel's BSD socket system calls. SOCKET is the descriptor itself.
#include "wincommon.h"
typedef u64 SOCKET;
#define INVALID_SOCKET ((SOCKET)~0ULL)
enum { S_SOCKET = 41, S_CONNECT = 42, S_ACCEPT = 43, S_SENDTO = 44, S_RECVFROM = 45, S_SHUTDOWN = 48, S_BIND = 49, S_LISTEN = 50, S_GETSOCKNAME = 51, S_GETPEERNAME = 52,
       S_SETSOCKOPT = 54, S_GETSOCKOPT = 55, S_CLOSE = 3, S_POLL = 7, S_FCNTL = 72, S_IOCTL = 16, S_MMAP = 9, S_MUNMAP = 11, S_OPEN = 2, S_READ = 0, S_WRITE = 1 };

static volatile int g_wsaerr;
static int wsa_code(i64 e) {
    switch ((int)-e) { case 11: return 10035; case 111: return 10061; case 104: return 10054; case 115: return 10036; case 98: return 10048; case 110: return 10060;
        case 107: return 10057; case 22: return 10022; case 9: return 10038; case 32: return 10053; case 4: return 10004; case 13: return 10013; case 101: return 10051;
        case 113: return 10065; case 106: return 10056; case 97: return 10047; case 95: return 10045; case 88: return 10038; case 99: return 10049; default: return 10055; }
}
static i64 chk(i64 r) { if (r < 0 && r > -4096) { g_wsaerr = wsa_code(r); SetLastError((u32)g_wsaerr); return -1; } return r; }
EXPORT int WINAPI WSAGetLastError(void) { return g_wsaerr; }
EXPORT void WINAPI WSASetLastError(int e) { g_wsaerr = e; }
EXPORT int WINAPI WSAStartup(u16 ver, unsigned char *data) {
    (void)ver; for (int i = 0; i < 408; i++) data[i] = 0;
    *(u16 *)data = 0x0202; *(u16 *)(data + 2) = 0x0202;
    static const char d[] = "THOS Winsock"; for (int i = 0; d[i]; i++) data[6 + i] = (unsigned char)d[i];
    return 0;
}
EXPORT int WINAPI WSACleanup(void) { return 0; }
EXPORT SOCKET WINAPI socket(int af, int type, int proto) { i64 r = chk(sc3(S_SOCKET, af, type, proto)); return r < 0 ? INVALID_SOCKET : (SOCKET)r; }
EXPORT SOCKET WINAPI WSASocketW(int af, int type, int proto, void *info, u32 group, u32 flags) { (void)info; (void)group; (void)flags; return socket(af, type, proto); }
EXPORT int WINAPI closesocket(SOCKET s) { return (int)chk(sc3(S_CLOSE, s, 0, 0)); }
EXPORT int WINAPI shutdown(SOCKET s, int how) { return (int)chk(sc3(S_SHUTDOWN, s, how, 0)); }
EXPORT int WINAPI bind(SOCKET s, const void *a, int len) { return (int)chk(sc3(S_BIND, s, a, len)); }
EXPORT int WINAPI connect(SOCKET s, const void *a, int len) { return (int)chk(sc3(S_CONNECT, s, a, len)); }
EXPORT int WINAPI listen(SOCKET s, int backlog) { return (int)chk(sc3(S_LISTEN, s, backlog, 0)); }
EXPORT SOCKET WINAPI accept(SOCKET s, void *a, int *len) { i64 r = chk(sc3(S_ACCEPT, s, a, len)); return r < 0 ? INVALID_SOCKET : (SOCKET)r; }
EXPORT int WINAPI send(SOCKET s, const char *b, int n, int flags) { return (int)chk(sc6(S_SENDTO, s, (i64)b, n, flags | 0x4000, 0, 0)); }
EXPORT int WINAPI recv(SOCKET s, char *b, int n, int flags) { return (int)chk(sc6(S_RECVFROM, s, (i64)b, n, flags, 0, 0)); }
EXPORT int WINAPI sendto(SOCKET s, const char *b, int n, int flags, const void *to, int tolen) { return (int)chk(sc6(S_SENDTO, s, (i64)b, n, flags | 0x4000, (i64)to, tolen)); }
EXPORT int WINAPI recvfrom(SOCKET s, char *b, int n, int flags, void *from, int *fromlen) { return (int)chk(sc6(S_RECVFROM, s, (i64)b, n, flags, (i64)from, (i64)fromlen)); }
EXPORT int WINAPI getsockname(SOCKET s, void *a, int *len) { return (int)chk(sc3(S_GETSOCKNAME, s, a, len)); }
EXPORT int WINAPI getpeername(SOCKET s, void *a, int *len) { return (int)chk(sc3(S_GETPEERNAME, s, a, len)); }
typedef struct { u32 len; char *buf; } WSABUF;
EXPORT int WINAPI WSASend(SOCKET s, WSABUF *b, u32 n, u32 *sent, u32 flags, void *ov, void *cb) {
    (void)ov; (void)cb; u32 total = 0;
    for (u32 i = 0; i < n; i++) { int r = send(s, b[i].buf, (int)b[i].len, (int)flags); if (r < 0) return -1; total += (u32)r; if ((u32)r < b[i].len) break; }
    if (sent) *sent = total; return 0;
}
EXPORT int WINAPI WSARecv(SOCKET s, WSABUF *b, u32 n, u32 *got, u32 *flags, void *ov, void *cb) {
    (void)ov; (void)cb; u32 total = 0;
    for (u32 i = 0; i < n; i++) { int r = recv(s, b[i].buf, (int)b[i].len, flags ? (int)*flags : 0); if (r < 0) return -1; total += (u32)r; if ((u32)r < b[i].len) break; }
    if (got) *got = total; return 0;
}
EXPORT int WINAPI WSADuplicateSocketW(SOCKET s, u32 pid, void *info) { (void)s; (void)pid; (void)info; g_wsaerr = 10045; return -1; }

/* options: Windows' SOL_SOCKET numbers are not Linux's */
static int map_opt(int level, int opt, int *llevel, int *lopt) {
    if (level == 0xffff) {
        *llevel = 1;
        switch (opt) { case 4: *lopt = 2; return 1; case 8: *lopt = 9; return 1; case 0x1002: *lopt = 8; return 1; case 0x1001: *lopt = 7; return 1; case 0x1007: *lopt = 4; return 1;
            case 0x20: *lopt = 6; return 1; case 0x80: *lopt = 13; return 1; case 0x1008: *lopt = 3; return 1; default: return 0; }
    }
    *llevel = level; *lopt = opt; return level == 6 || level == 0;
}
EXPORT int WINAPI setsockopt(SOCKET s, int level, int opt, const char *v, int len) {
    int ll, lo;
    if ((level == 0xffff) && (opt == 0x1006 || opt == 0x1005) && len == 4) {                  /* SO_RCVTIMEO / SO_SNDTIMEO: DWORD ms -> timeval */
        i64 tv[2] = { *(const u32 *)v / 1000, (*(const u32 *)v % 1000) * 1000 };
        return (int)chk(sc6(S_SETSOCKOPT, s, 1, opt == 0x1006 ? 20 : 21, (i64)tv, 16, 0));
    }
    if (!map_opt(level, opt, &ll, &lo)) return 0;                                              /* unknown options are accepted and ignored */
    return (int)chk(sc6(S_SETSOCKOPT, s, ll, lo, (i64)v, len, 0));
}
EXPORT int WINAPI getsockopt(SOCKET s, int level, int opt, char *v, int *len) {
    int ll, lo;
    if (!map_opt(level, opt, &ll, &lo)) { if (v && len && *len >= 4) { *(int *)v = 0; *len = 4; } return 0; }
    return (int)chk(sc6(S_GETSOCKOPT, s, ll, lo, (i64)v, (i64)len, 0));
}
EXPORT int WINAPI ioctlsocket(SOCKET s, long cmd, u32 *arg) {
    if ((u32)cmd == 0x8004667eu) {                                                              /* FIONBIO */
        i64 fl = sc3(S_FCNTL, s, 3, 0); if (fl < 0) return (int)chk(fl);
        return (int)chk(sc3(S_FCNTL, s, 4, *arg ? (fl | 04000) : (fl & ~04000)));
    }
    if ((u32)cmd == 0x4004667fu) return (int)chk(sc3(S_IOCTL, s, 0x541B, arg));                 /* FIONREAD */
    g_wsaerr = 10022; return -1;
}

/* select over fd_set {count, array[64]} */
typedef struct { u32 count; u32 pad; SOCKET arr[64]; } FDSET;
typedef struct { int fd; short events, revents; } POLLFD;
EXPORT int WINAPI select(int nfds, FDSET *r, FDSET *w, FDSET *e, const long *tv) {
    (void)nfds; POLLFD pf[192]; int np = 0; struct { FDSET *set; short ev; } kinds[3] = { { r, 1 }, { w, 4 }, { e, 2 } };
    for (int k = 0; k < 3; k++) if (kinds[k].set) for (u32 i = 0; i < kinds[k].set->count && np < 192; i++) { pf[np].fd = (int)kinds[k].set->arr[i]; pf[np].events = kinds[k].ev; pf[np].revents = 0; np++; }
    i64 ms = tv ? (i64)tv[0] * 1000 + tv[1] / 1000 : -1;
    i64 n = chk(sc3(S_POLL, pf, np, ms));
    if (n < 0) return -1;
    int idx = 0, ready = 0;
    for (int k = 0; k < 3; k++) if (kinds[k].set) {
        FDSET *s = kinds[k].set; u32 out = 0;
        for (u32 i = 0; i < s->count; i++, idx++) {
            short rev = pf[idx].revents;
            int hit = k == 0 ? (rev & (1 | 8 | 16)) : k == 1 ? (rev & 4) : (rev & 8);
            if (hit) { s->arr[out++] = s->arr[i]; ready++; }
        }
        s->count = out;
    }
    return ready;
}

/* name resolution: numbers, localhost, or an A query to the first nameserver */
static int parse_ip(const char *s, u32 *out) {
    u32 v = 0; int parts = 0;
    for (;;) {
        if (*s < '0' || *s > '9') return 0;
        u32 n = 0; while (*s >= '0' && *s <= '9') { n = n * 10 + (u32)(*s++ - '0'); if (n > 255) return 0; }
        v |= n << (8 * parts); parts++;
        if (parts == 4) return *s == 0 ? (*out = v, 1) : 0;
        if (*s++ != '.') return 0;
    }
}
static int resolv_server(u32 *ip) {
    char buf[512]; i64 fd = sc3(S_OPEN, "/etc/resolv.conf", 0, 0); if (fd < 0) return 0;
    i64 n = sc3(S_READ, fd, buf, sizeof buf - 1); sc3(S_CLOSE, fd, 0, 0); if (n <= 0) return 0;
    buf[n] = 0;
    for (char *p = buf; *p; p++) if (p[0] == 'n' && p[1] == 'a' && p[2] == 'm' && p[3] == 'e' && p[4] == 's' && p[5] == 'e' && p[6] == 'r' && p[7] == ' ') { char *q = p + 8; char t[16]; int i = 0; while (*q && *q != '\n' && i < 15) t[i++] = *q++; t[i] = 0; return parse_ip(t, ip); }
    return 0;
}
static int dns_a(const char *name, u32 *ip) {
    u32 ns; if (!resolv_server(&ns)) return 0;
    unsigned char q[300]; int o = 0; q[o++] = 0x54; q[o++] = 0x48; q[o++] = 1; q[o++] = 0; q[o++] = 0; q[o++] = 1; for (int i = 0; i < 6; i++) q[o++] = 0;
    const char *p = name;
    while (*p) { const char *d = p; while (*d && *d != '.') d++; int l = (int)(d - p); if (l == 0 || l > 63 || o + l + 6 > 290) return 0; q[o++] = (unsigned char)l; for (int i = 0; i < l; i++) q[o++] = (unsigned char)p[i]; p = *d ? d + 1 : d; }
    q[o++] = 0; q[o++] = 0; q[o++] = 1; q[o++] = 0; q[o++] = 1;
    i64 s = sc3(S_SOCKET, 2, 2, 0); if (s < 0) return 0;
    unsigned char sa[16] = { 2, 0, 0, 53, (unsigned char)ns, (unsigned char)(ns >> 8), (unsigned char)(ns >> 16), (unsigned char)(ns >> 24) };
    unsigned char r[600]; i64 n = -1;
    i64 tv[2] = { 3, 0 }; sc6(S_SETSOCKOPT, s, 1, 20, (i64)tv, 16, 0);
    for (int attempt = 0; attempt < 3 && n <= 12; attempt++) { sc6(S_SENDTO, s, (i64)q, o, 0, (i64)sa, 16); n = sc6(S_RECVFROM, s, (i64)r, sizeof r, 0, 0, 0); }
    sc3(S_CLOSE, s, 0, 0);
    if (n <= 12 || r[0] != 0x54 || (r[3] & 15) != 0) return 0;
    int an = (r[6] << 8) | r[7]; int i = 12;
    while (i < n && r[i]) i += r[i] < 192 ? r[i] + 1 : 2; i += 5;                              /* skip the question */
    for (int a = 0; a < an && i + 12 <= n; a++) {
        if (r[i] >= 192) i += 2; else { while (i < n && r[i]) i += r[i] + 1; i++; }
        int type = (r[i] << 8) | r[i + 1], rdlen = (r[i + 8] << 8) | r[i + 9]; i += 10;
        if (type == 1 && rdlen == 4 && i + 4 <= n) { *ip = r[i] | (r[i + 1] << 8) | (r[i + 2] << 16) | ((u32)r[i + 3] << 24); return 1; }
        i += rdlen;
    }
    return 0;
}
typedef struct ADDRINFOA { int flags, family, socktype, protocol; u64 addrlen; char *canon; void *addr; struct ADDRINFOA *next; } ADDRINFOA;
EXPORT int WINAPI getaddrinfo(const char *node, const char *service, const ADDRINFOA *hints, ADDRINFOA **res) {
    u32 ip = 0; u32 port = 0;
    if (service) { if (*service >= '0' && *service <= '9') { while (*service >= '0' && *service <= '9') port = port * 10 + (u32)(*service++ - '0'); } else if (service[0] == 'h' && service[1] == 't' && service[2] == 't' && service[3] == 'p') port = service[4] == 's' ? 443 : 80; }
    if (hints && hints->family == 23) return 11001;
    if (!node) ip = hints && (hints->flags & 1) ? 0 : 0x0100007f;
    else if (!parse_ip(node, &ip)) {
        if (node[0] == 'l' && node[1] == 'o' && node[2] == 'c' && node[3] == 'a' && node[4] == 'l' && node[5] == 'h' && node[6] == 'o' && node[7] == 's' && node[8] == 't' && !node[9]) ip = 0x0100007f;
        else if (!dns_a(node, &ip)) return 11001;                                               /* WSAHOST_NOT_FOUND */
    }
    unsigned char *page = (unsigned char *)sc6(S_MMAP, 0, 4096, 3, 0x22, -1, 0);
    if ((i64)page < 0 && (i64)page > -4096) return 8;
    ADDRINFOA *ai = (ADDRINFOA *)page; unsigned char *sa = page + 64;
    ai->family = 2; ai->socktype = hints && hints->socktype ? hints->socktype : 1; ai->protocol = ai->socktype == 1 ? 6 : 17; ai->addrlen = 16; ai->addr = sa;
    sa[0] = 2; sa[2] = (unsigned char)(port >> 8); sa[3] = (unsigned char)port; sa[4] = (unsigned char)ip; sa[5] = (unsigned char)(ip >> 8); sa[6] = (unsigned char)(ip >> 16); sa[7] = (unsigned char)(ip >> 24);
    *res = ai; return 0;
}
EXPORT void WINAPI freeaddrinfo(ADDRINFOA *ai) { if (ai) sc3(S_MUNMAP, (u64)ai & ~4095ULL, 4096, 0); }
EXPORT int WINAPI GetHostNameW(u16 *buf, int n) { static const char *h = "THOS"; if (n < 5) { g_wsaerr = 10014; return -1; } for (int i = 0; i < 5; i++) buf[i] = (u16)h[i]; return 0; }
EXPORT int WINAPI gethostname(char *buf, int n) { if (n < 5) return -1; const char *h = "THOS"; for (int i = 0; i < 5; i++) buf[i] = h[i]; return 0; }
EXPORT u32 WINAPI htonl(u32 v) { return __builtin_bswap32(v); }
EXPORT u32 WINAPI ntohl(u32 v) { return __builtin_bswap32(v); }
EXPORT u16 WINAPI htons(u16 v) { return (u16)__builtin_bswap16(v); }
EXPORT u16 WINAPI ntohs(u16 v) { return (u16)__builtin_bswap16(v); }
EXPORT u32 WINAPI inet_addr(const char *s) { u32 v; return parse_ip(s, &v) ? v : 0xFFFFFFFFu; }
__attribute__((dllexport)) int __stdcall DllMain(void *h, unsigned reason, void *res) { (void)h; (void)reason; (void)res; return 1; }
