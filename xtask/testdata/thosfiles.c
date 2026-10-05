// SPDX-License-Identifier: GPL-2.0-or-later
// THOS file manager (a legacy-protocol desktop client): browse directories, open things by their type, install packages.
//   click / Up / Down / PgUp / PgDn / Home / End select, double-click or Enter opens, Backspace goes up,
//   Delete (twice) removes the selected file, R refreshes.
//   Opening a .deb/.rpm/.zip/.tar.* runs `thos-pkg install` in a terminal window, a .exe or an executable runs it there,
//   text files open in `less`.
#define _GNU_SOURCE
#include <dirent.h>
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

#define FW 600
#define FH 420
#define HEAD 30
#define ROW 18
#define FOOT 22
#define VIS ((FH - HEAD - FOOT) / ROW)
#define MAXE 512
static uint32_t *px;
static int sock;
static char cwd[512] = "/";
static struct ent { char name[96]; int dir, exec; long long size; } ent[MAXE];
static int nent, sel, top;
static char status[120];
static int armed = -1;                         /* Delete pressed once on this entry */

static int cmp_ent(const void *a, const void *b) {
    const struct ent *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}
static void load(void) {
    nent = 0; sel = 0; top = 0; armed = -1;
    DIR *d = opendir(cwd);
    if (!d) { snprintf(status, sizeof status, "cannot read %s", cwd); return; }
    struct dirent *e;
    while ((e = readdir(d)) && nent < MAXE) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        struct ent *n = &ent[nent];
        snprintf(n->name, sizeof n->name, "%s", e->d_name);
        char full[700];
        snprintf(full, sizeof full, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", n->name);
        struct stat st;
        n->dir = 0; n->exec = 0; n->size = 0;
        if (stat(full, &st) == 0) { n->dir = S_ISDIR(st.st_mode); n->exec = !n->dir && (st.st_mode & 0111); n->size = st.st_size; }
        nent++;
    }
    closedir(d);
    qsort(ent, (size_t)nent, sizeof ent[0], cmp_ent);
    snprintf(status, sizeof status, "%d items", nent);
}
static void commit(void) { struct wl_msg c = { WL_COMMIT, 0, 0, FW, FH }; (void)!write(sock, &c, sizeof c); }
static void box(int x0, int y0, int x1, int y1, uint32_t c) {
    for (int y = y0; y < y1 && y < FH; y++) for (int x = x0; x < x1 && x < FW; x++) px[y * FW + x] = c;
}
static void size_str(long long v, char *out) {
    if (v < 1024) snprintf(out, 16, "%lld B", v);
    else if (v < 1024 * 1024) snprintf(out, 16, "%lld kB", v / 1024);
    else snprintf(out, 16, "%lld MB", v / (1024 * 1024));
}
static void render(void) {
    box(0, 0, FW, FH, 0x001c1c26);
    box(0, 0, FW, HEAD, 0x002a2a3a);
    box(8, 4, 52, 26, 0x00386aa5);
    gfx_str(px, FW, FH, 14, 7, "Up", 0x00ffffff, 0xFFFFFFFFu);
    gfx_str(px, FW, FH, 62, 7, cwd, 0x00e8e8f0, 0xFFFFFFFFu);
    for (int r = 0; r < VIS && top + r < nent; r++) {
        int i = top + r, y = HEAD + r * ROW;
        struct ent *e = &ent[i];
        if (i == sel) box(0, y, FW, y + ROW, 0x00364868);
        gfx_str(px, FW, FH, 10, y + 1, e->dir ? "[+]" : (e->exec ? "[x]" : "   "), e->dir ? 0x00e8c860 : 0x0080e080, 0xFFFFFFFFu);
        char nm[60]; snprintf(nm, sizeof nm, "%.56s", e->name);
        gfx_str(px, FW, FH, 44, y + 1, nm, e->dir ? 0x00f0e0a0 : 0x00e0e0e8, 0xFFFFFFFFu);
        if (!e->dir) { char sz[16]; size_str(e->size, sz); gfx_str(px, FW, FH, FW - 16 - 8 * (int)strlen(sz) - 8, y + 1, sz, 0x00a0a0b8, 0xFFFFFFFFu); }
    }
    box(0, FH - FOOT, FW, FH, 0x002a2a3a);
    gfx_str(px, FW, FH, 8, FH - FOOT + 3, status, 0x00c0c0d0, 0xFFFFFFFFu);
    commit();
}
static void go(const char *to) {
    char p[512];
    if (!strcmp(to, "..")) {
        snprintf(p, sizeof p, "%s", cwd);
        char *s = strrchr(p, '/');
        if (s == p) p[1] = 0; else if (s) *s = 0;
    } else snprintf(p, sizeof p, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", to);
    DIR *d = opendir(p);
    if (!d) { snprintf(status, sizeof status, "cannot open %s", p); return; }
    closedir(d);
    snprintf(cwd, sizeof cwd, "%s", p);
    load();
}
static int has_suffix(const char *n, const char *suf) {
    size_t a = strlen(n), b = strlen(suf);
    return a >= b && !strcasecmp(n + a - b, suf);
}
static void in_terminal(const char *cmd) {
    if (fork() != 0) return;
    setenv("HOME", "/tmp", 1); setenv("XDG_RUNTIME_DIR", "/run", 1); setenv("LC_ALL", "C.UTF-8", 1);
    if (getenv("FILES_LOG")) execl("/thosterm", "thosterm", "-l", "-c", cmd, (char *)0);   /* -l: terminal lines also to the serial log */
    execl("/thosterm", "thosterm", "-c", cmd, (char *)0);
    _exit(127);
}
static void open_entry(struct ent *e) {
    if (e->dir) { go(e->name); return; }
    if (strchr(e->name, '\'')) { snprintf(status, sizeof status, "that name has a quote in it"); return; }
    char full[700], cmd[900];
    snprintf(full, sizeof full, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", e->name);
    static const char *pkg[] = { ".deb", ".rpm", ".zip", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".msi" };
    for (unsigned k = 0; k < sizeof pkg / sizeof pkg[0]; k++)
        if (has_suffix(e->name, pkg[k])) {
            snprintf(cmd, sizeof cmd, "/bin/thos-pkg install '%s'", full);
            in_terminal(cmd);
            snprintf(status, sizeof status, "installing %s ...", e->name);
            return;
        }
    if (has_suffix(e->name, ".sh")) snprintf(cmd, sizeof cmd, "/busybox sh '%s'", full);
    else if (has_suffix(e->name, ".exe") || e->exec) snprintf(cmd, sizeof cmd, "'%s'", full);
    else snprintf(cmd, sizeof cmd, "/busybox less '%s'", full);
    in_terminal(cmd);
}
static void select_to(int i) {
    if (nent == 0) return;
    if (i < 0) i = 0;
    if (i >= nent) i = nent - 1;
    sel = i; armed = -1;
    if (sel < top) top = sel;
    if (sel >= top + VIS) top = sel - VIS + 1;
}

int main(void) {
    int key = 0x5000 + getpid();
    int id = shmget(key, FW * FH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0) { puts("files FAIL: shm/font"); return 1; }
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("files FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, FW, FH, (uint32_t)key, 0 };
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("files FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "Files", 5);
    (void)!write(sock, &t, sizeof t);
    char *home = getenv("FILES_START");
    if (home) snprintf(cwd, sizeof cwd, "%s", home);
    load();
    render();
    printf("files ready\n");
    fflush(stdout);
    int last_row = -1;
    long long last_ms = 0;
    while (read(sock, &m, sizeof m) == (ssize_t)sizeof m) {
        if (m.op == WL_CLOSE) break;
        if (m.op == WL_POINTER && m.c == 1) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            long long now = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
            if ((int)m.b < HEAD) { if ((int)m.a >= 8 && (int)m.a < 52) go(".."); }
            else {
                int r = ((int)m.b - HEAD) / ROW, i = top + r;
                if (r < VIS && i < nent) {
                    if (i == last_row && now - last_ms < 800) { select_to(i); open_entry(&ent[i]); last_row = -1; }
                    else { select_to(i); last_row = i; last_ms = now; }
                }
            }
            render();
        } else if (m.op == WL_KEY && m.b) {
            switch (m.a) {
                case 0x52: select_to(sel - 1); break;
                case 0x51: select_to(sel + 1); break;
                case 0x4B: select_to(sel - VIS); break;
                case 0x4E: select_to(sel + VIS); break;
                case 0x4A: select_to(0); break;
                case 0x4D: select_to(nent - 1); break;
                case 0x28: if (nent) open_entry(&ent[sel]); break;
                case 0x2A: go(".."); break;
                case 0x4C:                                    /* Delete, twice */
                    if (!nent) break;
                    if (armed != sel) { armed = sel; snprintf(status, sizeof status, "press Delete again to remove %.60s", ent[sel].name); }
                    else {
                        char full[700];
                        snprintf(full, sizeof full, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", ent[sel].name);
                        int rc = ent[sel].dir ? rmdir(full) : unlink(full);
                        snprintf(status, sizeof status, rc == 0 ? "removed %.60s" : "could not remove %.60s", ent[sel].name);
                        int keep = sel;
                        load();
                        select_to(keep);
                    }
                    break;
                default:
                    if (m.c == 'r' || m.c == 'R') { load(); snprintf(status, sizeof status, "%d items", nent); }
                    else continue;
            }
            render();
        }
    }
    printf("files done\n");
    return 0;
}
