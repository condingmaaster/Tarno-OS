// SPDX-License-Identifier: GPL-2.0-or-later
// THOS software centre (a legacy-protocol desktop client): search the Debian package index that `thos-pkg update`
// built (/opt/.index) and install or remove programs with `thos-pkg` in a terminal window.
//   type to search, Up/Down select, Enter or double-click installs (removes, when it is already installed), Esc clears the query.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"

#define SW 700
#define SH 460
#define HEAD 56
#define ROW 18
#define FOOT 22
#define VIS ((SH - HEAD - FOOT) / ROW)
#define MAXR 300
static uint32_t *px;
static int sock;
static char *index_buf;
static size_t index_len;
static char query[64];
static struct res { const char *line; size_t len; } res[MAXR];
static int nres, sel, top;
static char status[160];

static int load_index(void) {
    FILE *f = fopen("/opt/.index", "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    index_buf = malloc((size_t)n + 1);
    if (!index_buf) { fclose(f); return 0; }
    index_len = fread(index_buf, 1, (size_t)n, f);
    index_buf[index_len] = 0;
    fclose(f);
    return 1;
}
static void field(const struct res *r, int k, char *out, size_t cap) {      /* k-th '|' separated field of the index line */
    const char *p = r->line, *end = r->line + r->len;
    for (int i = 0; i < k && p < end; i++) { while (p < end && *p != '|') p++; if (p < end) p++; }
    size_t n = 0;
    while (p + n < end && p[n] != '|' && n + 1 < cap) { out[n] = p[n]; n++; }
    out[n] = 0;
}
static int installed(const char *name) {
    char p[200];
    snprintf(p, sizeof p, "/opt/.db/%s.list", name);
    struct stat st;
    return stat(p, &st) == 0;
}
static void search(void) {
    nres = 0; sel = 0; top = 0;
    if (!index_buf || !query[0]) return;
    size_t ql = strlen(query);
    for (int pass = 0; pass < 2 && nres < MAXR; pass++) {                 /* pass 0: names, pass 1: descriptions */
        const char *p = index_buf;
        while (*p && nres < MAXR) {
            const char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > 0) {
                const char *bar = memchr(p, '|', len);
                size_t nlen = bar ? (size_t)(bar - p) : len;
                int in_name = memmem(p, nlen, query, ql) != 0;
                if (!in_name) {                                           /* names: do a case-insensitive compare below */
                    char tmp[96]; size_t k = nlen < 95 ? nlen : 95;
                    memcpy(tmp, p, k); tmp[k] = 0;
                    in_name = strcasestr(tmp, query) != 0;
                }
                int hit = 0;
                if (pass == 0) hit = in_name;
                else if (!in_name) {
                    const char *d = p; int bars = 0;
                    while (d < p + len && bars < 6) { if (*d == '|') bars++; d++; }
                    char desc[160]; size_t dl = (size_t)(p + len - d); if (dl > 159) dl = 159;
                    memcpy(desc, d, dl); desc[dl] = 0;
                    hit = strcasestr(desc, query) != 0;
                }
                if (hit) { res[nres].line = p; res[nres].len = len; nres++; }
            }
            if (!nl) break;
            p = nl + 1;
        }
    }
    snprintf(status, sizeof status, "%d match%s", nres, nres == 1 ? "" : "es");
}
static void commit(void) { struct wl_msg c = { WL_COMMIT, 0, 0, SW, SH }; (void)!write(sock, &c, sizeof c); }
static void box(int x0, int y0, int x1, int y1, uint32_t c) {
    for (int y = y0; y < y1 && y < SH; y++) for (int x = x0; x < x1 && x < SW; x++) px[y * SW + x] = c;
}
static void render(void) {
    box(0, 0, SW, SH, 0x001c1c26);
    box(0, 0, SW, HEAD, 0x002a2a3a);
    gfx_str(px, SW, SH, 10, 6, "Software - Debian archive", 0x00ffffff, 0xFFFFFFFFu);
    box(8, 28, SW - 8, 50, 0x00101018);
    char q[80]; snprintf(q, sizeof q, "> %s_", query);
    gfx_str(px, SW, SH, 14, 31, q, 0x00f0f0b0, 0xFFFFFFFFu);
    if (!index_buf) {
        gfx_str(px, SW, SH, 14, HEAD + 14, "No package index yet. Press Enter to download it (thos-pkg update).", 0x00e0e0e8, 0xFFFFFFFFu);
    }
    for (int r = 0; r < VIS && top + r < nres; r++) {
        int i = top + r, y = HEAD + r * ROW;
        char name[64], ver[40], desc[160], size[16], row[200];
        field(&res[i], 0, name, sizeof name); field(&res[i], 1, ver, sizeof ver); field(&res[i], 5, size, sizeof size); field(&res[i], 6, desc, sizeof desc);
        long kb = atol(size) / 1024;
        if (i == sel) box(0, y, SW, y + ROW, 0x00364868);
        gfx_str(px, SW, SH, 8, y + 1, installed(name) ? "[*]" : "   ", 0x0080e080, 0xFFFFFFFFu);
        snprintf(row, sizeof row, "%-22.22s %-14.14s %5ldk  %s", name, ver, kb, desc);
        row[(SW - 50) / 8] = 0;
        gfx_str(px, SW, SH, 40, y + 1, row, 0x00e0e0e8, 0xFFFFFFFFu);
    }
    box(0, SH - FOOT, SW, SH, 0x002a2a3a);
    gfx_str(px, SW, SH, 8, SH - FOOT + 3, status, 0x00c0c0d0, 0xFFFFFFFFu);
    commit();
}
static void in_terminal(const char *cmd) {
    if (fork() != 0) return;
    setenv("HOME", "/tmp", 1); setenv("XDG_RUNTIME_DIR", "/run", 1);
    execl("/thosterm", "thosterm", "-c", cmd, (char *)0);
    _exit(127);
}
static void act(void) {
    if (!index_buf) { in_terminal("/bin/thos-pkg update"); snprintf(status, sizeof status, "updating the index ... reopen the software centre afterwards"); return; }
    if (sel >= nres) return;
    char name[64], cmd[200];
    field(&res[sel], 0, name, sizeof name);
    for (char *c = name; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '.' || *c == '+')) return;
    snprintf(cmd, sizeof cmd, "/bin/thos-pkg %s %s", installed(name) ? "remove" : "install", name);
    in_terminal(cmd);
    snprintf(status, sizeof status, "%s %s ...", installed(name) ? "removing" : "installing", name);
}
static void move(int to) {
    if (nres == 0) return;
    if (to < 0) to = 0;
    if (to >= nres) to = nres - 1;
    sel = to;
    if (sel < top) top = sel;
    if (sel >= top + VIS) top = sel - VIS + 1;
}

int main(void) {
    int key = 0x5000 + getpid();
    int id = shmget(key, SW * SH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0) { puts("soft FAIL: shm/font"); return 1; }
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("soft FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, SW, SH, (uint32_t)key, 0 };
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("soft FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "Software", 8);
    (void)!write(sock, &t, sizeof t);
    load_index();
    const char *q0 = getenv("SOFT_QUERY");
    if (q0) { snprintf(query, sizeof query, "%s", q0); search(); }
    else snprintf(status, sizeof status, index_buf ? "type a name or a word from the description" : "no index");
    render();
    printf("soft ready\n");
    fflush(stdout);
    int last_row = -1;
    long long last_ms = 0;
    while (read(sock, &m, sizeof m) == (ssize_t)sizeof m) {
        if (m.op == WL_CLOSE) break;
        if (m.op == WL_POINTER && m.c == 1) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            long long now = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
            if ((int)m.b >= HEAD) {
                int r = ((int)m.b - HEAD) / ROW, i = top + r;
                if (r < VIS && i < nres) {
                    move(i);
                    if (i == last_row && now - last_ms < 800) { act(); last_row = -1; } else { last_row = i; last_ms = now; }
                }
            }
            render();
        } else if (m.op == WL_KEY && m.b) {
            size_t ql = strlen(query);
            if (m.a == 0x52) move(sel - 1);
            else if (m.a == 0x51) move(sel + 1);
            else if (m.a == 0x4B) move(sel - VIS);
            else if (m.a == 0x4E) move(sel + VIS);
            else if (m.a == 0x28) act();
            else if (m.a == 0x29) { query[0] = 0; search(); }
            else if (m.a == 0x2A) { if (ql) { query[ql - 1] = 0; search(); } }
            else if (m.c >= 32 && m.c < 127 && ql + 1 < sizeof query) { query[ql] = (char)m.c; query[ql + 1] = 0; search(); }
            else continue;
            render();
        }
    }
    printf("soft done\n");
    return 0;
}
