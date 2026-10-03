// SPDX-License-Identifier: GPL-2.0-or-later
// THOS terminal emulator: a window (thoswl.h client) around a pty with a BusyBox shell, VT100/ANSI
// subset. Usage: thosterm [-l]   (-l logs every line that scrolls off to the serial console)
#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"

#define COLS 80
#define ROWS 24
#define PW (COLS * 8)
#define PH (ROWS * 16)

static const uint32_t pal[8] = { 0x00101010, 0x00cc3333, 0x0033cc33, 0x00cccc33, 0x003366cc, 0x00cc33cc, 0x0033cccc, 0x00c8c8c8 };
struct cell { unsigned char ch, fg, bg, inv; };
static struct cell grid[ROWS][COLS];
static int cx, cy, cur_fg = 7, cur_bg = 0, cur_inv, dirty0 = ROWS, dirty1 = -1, logging;
static uint32_t *px;
static int sock, master;
static pid_t child;

static void mark(int r) { if (r < dirty0) dirty0 = r; if (r > dirty1) dirty1 = r; }
static struct cell blank(void) { return (struct cell){ ' ', 7, 0, 0 }; }

static void log_row(int r) {
    if (!logging) return;
    char b[COLS + 1]; int n = COLS;
    for (int i = 0; i < COLS; i++) b[i] = grid[r][i].ch;
    while (n > 0 && b[n - 1] == ' ') n--;
    b[n] = 0;
    printf("term-line: %s\n", b);
    fflush(stdout);
}

static void scroll_up(void) {
    log_row(0);
    memmove(grid[0], grid[1], sizeof grid[0] * (ROWS - 1));
    for (int i = 0; i < COLS; i++) grid[ROWS - 1][i] = blank();
    mark(0); mark(ROWS - 1);
}
static void newline(void) {
    log_row(cy);
    if (cy == ROWS - 1) { int l = logging; logging = 0; scroll_up(); logging = l; } else cy++;
}
static void clear_range(int r, int c0, int c1) { for (int c = c0; c < c1; c++) grid[r][c] = blank(); mark(r); }

static void put(unsigned char c) {
    if (cx >= COLS) { cx = 0; newline(); }
    grid[cy][cx++] = (struct cell){ c, (unsigned char)cur_fg, (unsigned char)cur_bg, (unsigned char)cur_inv };
    mark(cy);
}

static int st, par[8], npar, priv;
static void csi(unsigned char f) {
    int a = npar > 0 ? par[0] : 0, b = npar > 1 ? par[1] : 0;
    switch (f) {
    case 'A': cy -= a ? a : 1; if (cy < 0) cy = 0; break;
    case 'B': cy += a ? a : 1; if (cy >= ROWS) cy = ROWS - 1; break;
    case 'C': cx += a ? a : 1; if (cx >= COLS) cx = COLS - 1; break;
    case 'D': cx -= a ? a : 1; if (cx < 0) cx = 0; break;
    case 'G': cx = (a ? a : 1) - 1; if (cx >= COLS) cx = COLS - 1; break;
    case 'd': cy = (a ? a : 1) - 1; if (cy >= ROWS) cy = ROWS - 1; break;
    case 'H': case 'f': cy = (a ? a : 1) - 1; cx = (b ? b : 1) - 1; if (cy >= ROWS) cy = ROWS - 1; if (cx >= COLS) cx = COLS - 1; break;
    case 'J':
        if (a == 0) { clear_range(cy, cx, COLS); for (int r = cy + 1; r < ROWS; r++) clear_range(r, 0, COLS); }
        else if (a == 1) { for (int r = 0; r < cy; r++) clear_range(r, 0, COLS); clear_range(cy, 0, cx + 1); }
        else for (int r = 0; r < ROWS; r++) clear_range(r, 0, COLS);
        break;
    case 'K':
        if (a == 0) clear_range(cy, cx, COLS); else if (a == 1) clear_range(cy, 0, cx + 1); else clear_range(cy, 0, COLS);
        break;
    case 'P': { int n = a ? a : 1; if (n > COLS - cx) n = COLS - cx;
        memmove(&grid[cy][cx], &grid[cy][cx + n], sizeof(struct cell) * (COLS - cx - n)); clear_range(cy, COLS - n, COLS); break; }
    case '@': { int n = a ? a : 1; if (n > COLS - cx) n = COLS - cx;
        memmove(&grid[cy][cx + n], &grid[cy][cx], sizeof(struct cell) * (COLS - cx - n)); clear_range(cy, cx, cx + n); break; }
    case 'L': { int n = a ? a : 1; for (; n > 0; n--) { memmove(grid[cy + 1], grid[cy], sizeof grid[0] * (ROWS - cy - 1)); clear_range(cy, 0, COLS); } for (int r = cy; r < ROWS; r++) mark(r); break; }
    case 'M': { int n = a ? a : 1; for (; n > 0; n--) { memmove(grid[cy], grid[cy + 1], sizeof grid[0] * (ROWS - cy - 1)); clear_range(ROWS - 1, 0, COLS); } for (int r = cy; r < ROWS; r++) mark(r); break; }
    case 'm':
        if (npar == 0) { cur_fg = 7; cur_bg = 0; cur_inv = 0; }
        for (int i = 0; i < npar; i++) {
            int p = par[i];
            if (p == 0) { cur_fg = 7; cur_bg = 0; cur_inv = 0; }
            else if (p == 7) cur_inv = 1;
            else if (p == 27) cur_inv = 0;
            else if (p >= 30 && p <= 37) cur_fg = p - 30;
            else if (p >= 40 && p <= 47) cur_bg = p - 40;
            else if (p == 39) cur_fg = 7;
            else if (p == 49) cur_bg = 0;
        }
        break;
    default: break;   /* modes, scroll regions, cursor save/restore: ignored */
    }
}

static void feed(unsigned char c) {
    switch (st) {
    case 0:
        if (c == 27) st = 1;
        else if (c == '\r') cx = 0;
        else if (c == '\n') newline();
        else if (c == '\b') { if (cx > 0) cx--; }
        else if (c == '\t') { cx = (cx + 8) & ~7; if (cx >= COLS) cx = COLS - 1; }
        else if (c >= 32 && c < 127) put(c);
        else if (c >= 160) put(c);
        break;
    case 1:
        if (c == '[') { st = 2; npar = 0; par[0] = 0; priv = 0; }
        else if (c == ']') st = 3;
        else if (c == 'c') { for (int r = 0; r < ROWS; r++) clear_range(r, 0, COLS); cx = cy = 0; st = 0; }
        else st = 0;
        break;
    case 2:
        if (c == '?') priv = 1;
        else if (c >= '0' && c <= '9') { if (npar == 0) npar = 1; par[npar - 1] = par[npar - 1] * 10 + (c - '0'); }
        else if (c == ';') { if (npar == 0) npar = 1; if (npar < 8) par[npar++] = 0; }
        else if (c >= 0x40 && c <= 0x7e) { if (!priv) csi(c); st = 0; }
        break;
    case 3: if (c == 7 || c == 27) st = 0; break;
    }
}

static void render(void) {
    if (dirty1 < 0) return;
    for (int r = dirty0; r <= dirty1; r++)
        for (int c = 0; c < COLS; c++) {
            struct cell *k = &grid[r][c];
            int fg = k->fg, bg = k->bg, inv = k->inv ^ (r == cy && c == cx);
            if (inv) { int t = fg; fg = bg; bg = t; }
            gfx_char(px, PW, PH, c * 8, r * 16, k->ch, pal[fg], pal[bg]);
        }
    struct wl_msg m = { WL_COMMIT, 0, (uint32_t)(dirty0 * 16), PW, (uint32_t)((dirty1 - dirty0 + 1) * 16) };
    (void)!write(sock, &m, sizeof m);
    dirty0 = ROWS; dirty1 = -1;
}

static void key(struct wl_msg *m) {
    if (!m->b) return;
    unsigned a = m->a, c = m->c, mods = m->d;
    char out[8]; int n = 0;
    if (a == 0x28) out[n++] = '\r';
    else if (a == 0x2A) out[n++] = 0x7f;
    else if (a == 0x2B) out[n++] = '\t';
    else if (a == 0x29) out[n++] = 27;
    else if (a >= 0x4F && a <= 0x52) { out[n++] = 27; out[n++] = '['; out[n++] = "CDBA"[a - 0x4F]; }
    else if (a == 0x4C) { memcpy(out, "\033[3~", 4); n = 4; }
    else if (a == 0x4A) { memcpy(out, "\033[H", 3); n = 3; }
    else if (a == 0x4D) { memcpy(out, "\033[F", 3); n = 3; }
    else if (c) {
        if ((mods & 0x11) && c >= 'a' && c <= 'z') c -= 96;
        else if ((mods & 0x11) && c >= 'A' && c <= 'Z') c -= 64;
        out[n++] = (char)c;
    }
    if (n) (void)!write(master, out, n);
}

int main(int argc, char **argv) {
    logging = argc > 1 && !strcmp(argv[1], "-l");
    if (gfx_load_font() != 0) { puts("term FAIL: font"); return 1; }
    int ky = 0x5000 + getpid();
    int id = shmget(ky, PW * PH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1) { puts("term FAIL: shm"); return 1; }
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) grid[r][c] = blank();
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("term FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, PW, PH, (uint32_t)ky, 0 };
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("term FAIL: no CREATED"); return 1; }
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "terminal", 8);
    (void)!write(sock, &t, sizeof t);

    master = open("/dev/ptmx", O_RDWR);
    unsigned pn = 0;
    if (master < 0 || ioctl(master, 0x80045430, &pn) != 0) { puts("term FAIL: ptmx"); return 1; }
    unsigned short ws[4] = { ROWS, COLS, 0, 0 };
    ioctl(master, 0x5414, ws);
    child = fork();
    if (child == 0) {
        setsid();
        char path[32]; snprintf(path, sizeof path, "/dev/pts/%u", pn);
        int s = open(path, O_RDWR);
        if (s < 0) _exit(127);
        ioctl(s, 0x540e, 0);                       /* TIOCSCTTY */
        dup2(s, 0); dup2(s, 1); dup2(s, 2);
        if (s > 2) close(s);
        close(master); close(sock);
        setenv("TERM", "linux", 1);
        execl("/busybox", "sh", "-i", (char *)0);
        _exit(127);
    }
    mark(0); mark(ROWS - 1);
    render();
    printf("term ready\n");
    fflush(stdout);
    for (;;) {
        struct pollfd p[2] = { { sock, POLLIN, 0 }, { master, POLLIN, 0 } };
        if (poll(p, 2, -1) < 0) continue;
        if (p[1].revents & (POLLIN | POLLHUP)) {
            unsigned char buf[512];
            ssize_t n = read(master, buf, sizeof buf);
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; i++) feed(buf[i]);
            render();
        }
        if (p[0].revents & (POLLIN | POLLHUP)) {
            if (read(sock, &m, sizeof m) != (ssize_t)sizeof m) break;
            if (m.op == WL_CLOSE) break;
            if (m.op == WL_KEY) key(&m);
        }
    }
    kill(child, SIGHUP);
    waitpid(child, 0, 0);
    printf("term done\n");
    return 0;
}
