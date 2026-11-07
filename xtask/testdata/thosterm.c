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
#include "thosresize.h"
#include "thosgfx.h"
#include "thosui.h"
#include "thosmotion.h"

static int COLS=80;
static int ROWS=24;
#define FSIZE 15                                  /* the monospace font's pixel size */
#define CW 9                                      /* cell size in pixels (Hack at 15 px: advance 9) */
#define CH 20
#define PAD 12
static int PW=744;
static int PH=504;

#define TERM_BG 0x11141cu
#define TERM_FG 0xd5d9e6u
static const uint32_t pal[16] = { 0x11141c, 0xf7768e, 0x9ece6a, 0xe0af68, 0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xd5d9e6,
                                  0x414868, 0xff9ab0, 0xb9f27c, 0xffc777, 0x9db8ff, 0xd0b4ff, 0xa4e4ff, 0xf3f6ff };
struct cell { unsigned char ch, fg, bg, inv; };
static struct cell *grid;
#define CELL(r,c) grid[(r)*COLS+(c)]
static struct thos_resize resize;
static int cx, cy, cur_fg = 7, cur_bg = 0, cur_inv, dirty0 = 24, dirty1 = -1, logging;
static uint32_t *px;
static int sock, master;
static pid_t child;

static void mark(int r) { if (r < dirty0) dirty0 = r; if (r > dirty1) dirty1 = r; }
static struct cell blank(void) { return (struct cell){ ' ', 7, 0, 0 }; }

/* Latin-15 glyph index <-> Unicode (only the 8 positions where Latin-15 differs from Latin-1 matter here) */
static unsigned latin15_to_cp(unsigned char g) {
    switch (g) { case 0xA4: return 0x20AC; case 0xA6: return 0x160; case 0xA8: return 0x161; case 0xB4: return 0x17D;
                 case 0xB8: return 0x17E; case 0xBC: return 0x152; case 0xBD: return 0x153; case 0xBE: return 0x178; }
    return g;
}
static unsigned char cp_to_latin15(unsigned cp) {
    if (cp == 0x20AC) return 0xA4;
    if (cp == 0xB4) return 0x27;                 /* the acute accent has no Latin-15 glyph of its own here */
    if (cp < 256) return (unsigned char)cp;
    return '?';
}
static int put_utf8(char *o, unsigned cp) {
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F)); return 3;
}

static void log_row(int r) {
    if (!logging) return;
    char b[COLS * 3 + 1]; int n = 0;
    for (int i = 0; i < COLS; i++) n += put_utf8(b + n, latin15_to_cp(CELL(r,i).ch));
    while (n > 0 && b[n - 1] == ' ') n--;
    b[n] = 0;
    printf("term-line: %s\n", b);
    fflush(stdout);
}

static void scroll_up(void) {
    log_row(0);
    memmove(grid,grid+COLS,sizeof *grid*COLS*(ROWS-1));
    for (int i = 0; i < COLS; i++) CELL(ROWS - 1,i) = blank();
    mark(0); mark(ROWS - 1);
}
static void newline(void) {
    log_row(cy);
    if (cy == ROWS - 1) { int l = logging; logging = 0; scroll_up(); logging = l; } else cy++;
}
static void clear_range(int r, int c0, int c1) { for (int c = c0; c < c1; c++) CELL(r,c) = blank(); mark(r); }

static void put(unsigned char c) {
    if (cx >= COLS) { cx = 0; newline(); }
    CELL(cy,cx++) = (struct cell){ c, (unsigned char)cur_fg, (unsigned char)cur_bg, (unsigned char)cur_inv };
    mark(cy);
}

static int st, par[8], npar, priv;
static unsigned u8cp, u8need;
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
        memmove(&CELL(cy,cx), &CELL(cy,cx + n), sizeof(struct cell) * (COLS - cx - n)); clear_range(cy, COLS - n, COLS); break; }
    case '@': { int n = a ? a : 1; if (n > COLS - cx) n = COLS - cx;
        memmove(&CELL(cy,cx + n), &CELL(cy,cx), sizeof(struct cell) * (COLS - cx - n)); clear_range(cy, cx, cx + n); break; }
    case 'L': { int n = a ? a : 1; for (; n > 0; n--) { memmove(grid+(cy+1)*COLS,grid+cy*COLS,sizeof *grid*COLS*(ROWS-cy-1)); clear_range(cy, 0, COLS); } for (int r = cy; r < ROWS; r++) mark(r); break; }
    case 'M': { int n = a ? a : 1; for (; n > 0; n--) { memmove(grid+cy*COLS,grid+(cy+1)*COLS,sizeof *grid*COLS*(ROWS-cy-1)); clear_range(ROWS - 1, 0, COLS); } for (int r = cy; r < ROWS; r++) mark(r); break; }
    case 'm':
        if (npar == 0) { cur_fg = 7; cur_bg = 0; cur_inv = 0; }
        for (int i = 0; i < npar; i++) {
            int p = par[i];
            if (p == 0) { cur_fg = 7; cur_bg = 0; cur_inv = 0; }
            else if (p == 7) cur_inv = 1;
            else if (p == 27) cur_inv = 0;
            else if (p == 1) { if (cur_fg < 8) cur_fg += 8; }
            else if (p == 22) cur_fg &= 7;
            else if (p >= 30 && p <= 37) cur_fg = p - 30;
            else if (p >= 90 && p <= 97) cur_fg = p - 90 + 8;
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
        else if (c >= 0xC2 && c <= 0xF4) { u8cp = c >= 0xE0 ? c & 0x0F : c & 0x1F; u8need = c >= 0xE0 ? (c >= 0xF0 ? 3 : 2) : 1; }
        else if (c >= 0x80 && c < 0xC0 && u8need) { u8cp = (u8cp << 6) | (c & 0x3F); if (--u8need == 0) put(cp_to_latin15(u8cp)); }
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

static int last_cy, first_paint = 1;
static struct tween cur_tw;                         /* the cursor breathes: it fades out and in while the terminal is idle */
static float cur_alpha = 1;
static void render(void) {
    if (first_paint) { for (int i = 0; i < PW * PH; i++) px[i] = TERM_BG; first_paint = 0; dirty0 = 0; dirty1 = ROWS - 1; }
    mark(last_cy); mark(cy); last_cy = cy;
    if (dirty1 < 0) return;
    for (int r = dirty0; r <= dirty1; r++)
        for (int c = 0; c < COLS; c++) {
            struct cell *k = &CELL(r,c);
            int fg = k->fg, bg = k->bg, inv = k->inv;
            uint32_t fc = pal[fg], bc = bg == 0 ? TERM_BG : pal[bg];
            if (fg == 7) fc = TERM_FG;
            if (inv) { uint32_t t2 = fc; fc = bc == TERM_BG && !k->bg ? TERM_BG : bc; bc = t2; }
            int x0 = PAD + c * CW, y0 = PAD + r * CH;
            for (int j = 0; j < CH; j++) for (int i = 0; i < CW; i++) px[(y0 + j) * PW + x0 + i] = bc;
            unsigned cp = latin15_to_cp(k->ch);
            if (cp > 32) {
                char u[4]; int n = put_utf8(u, cp); u[n] = 0;
                ui_text(px, PW, PH, x0, y0 + 15, u, UI_MONO, FSIZE, fc);
            }
            if (r == cy && c == cx) {                                     /* a soft block cursor */
                unsigned ca = (unsigned)(150 * cur_alpha);
                for (int j = 0; j < CH; j++) for (int i = 0; i < CW; i++) { uint32_t *p = &px[(y0 + j) * PW + x0 + i]; *p = ui_mix(*p, UI_ACCENT_HI, ca); }
            }
        }
    struct wl_msg m = { WL_COMMIT, 0, (uint32_t)(PAD + dirty0 * CH), PW, (uint32_t)((dirty1 - dirty0 + 1) * CH) };
    (void)!write(sock, &m, sizeof m);
    dirty0 = ROWS; dirty1 = -1;
}

static int resize_cells(void) {
    int cols=(PW-2*PAD)/CW,rows=(PH-2*PAD)/CH;
    if(cols<1||rows<1||cols>1024||rows>512) return 0;
    struct cell *next=malloc((size_t)rows*cols*sizeof *next);
    if(!next) return 0;
    for(int i=0;i<rows*cols;i++) next[i]=blank();
    int shift=cy>=rows?cy-rows+1:0;
    int keep=COLS<cols?COLS:cols;
    for(int r=0;r<rows&&r+shift<ROWS;r++) memcpy(next+r*cols,grid+(r+shift)*COLS,(size_t)keep*sizeof *grid);
    free(grid);grid=next;COLS=cols;ROWS=rows;
    cy-=shift;if(cy<0)cy=0;if(cx>=COLS)cx=COLS-1;
    last_cy=cy;first_paint=1;dirty0=0;dirty1=ROWS-1;
    unsigned short ws[4]={ROWS,COLS,PW,PH};ioctl(master,0x5414,ws);
    printf("term resized: %dx%d cells\n",COLS,ROWS);fflush(stdout);
    return 1;
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
    else if (c >= 0x80) { n = put_utf8(out, c); }
    else if (c) {
        if ((mods & 0x11) && c >= 'a' && c <= 'z') c -= 96;
        else if ((mods & 0x11) && c >= 'A' && c <= 'Z') c -= 64;
        out[n++] = (char)c;
    }
    if (n) (void)!write(master, out, n);
}

int main(int argc, char **argv) {
    logging = argc > 1 && !strcmp(argv[1], "-l");
    const char *run = 0;                              /* `-c command`: run it (then wait for Enter) instead of a shell */
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "-c")) run = argv[i + 1];
    if (gfx_load_font() != 0 || ui_init() != 0) { puts("term FAIL: font"); return 1; }
    grid=malloc((size_t)ROWS*COLS*sizeof *grid);
    if(!grid) return 1;
    int ky = 0x5000 + getpid();
    int id = shmget(ky, PW * PH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1) { puts("term FAIL: shm"); return 1; }
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) CELL(r,c) = blank();
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("term FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, PW, PH, (uint32_t)ky, WL_RESIZABLE };
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("term FAIL: no CREATED"); return 1; }
    thos_resize_limits(sock,294,184);
    ui_cursor(sock, 1);
    shmctl(id, IPC_RMID, 0);      /* the compositor has attached: the segment lives until both sides unmap it */
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "Terminal", 8);
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
        setenv("PS1", "\n\033[1;94mthos$\033[0m ", 1);
        if (run) {
            char line[8192];
            if(strlen(run)>sizeof line-100) {fputs("Command is too long.\n",stderr);_exit(2);}
            snprintf(line, sizeof line, "%s; echo; echo '[finished - press Enter to close]'; read x", run);
            execl("/busybox", "sh", "-c", line, (char *)0);
        }
        execl("/busybox", "sh", "-i", (char *)0);
        _exit(127);
    }
    mark(0); mark(ROWS - 1);
    render();
    printf("term ready\n");
    fflush(stdout);
    long long phase_t = mo_now_ms();
    int cur_on = 1;
    tween_start(&cur_tw, 1, 1, 0);
    for (;;) {
        struct pollfd p[2] = { { sock, POLLIN, 0 }, { master, POLLIN, 0 } };
        int fading = tween_busy(&cur_tw);
        long long now = mo_now_ms();
        int wait = fading ? MO_FRAME_MS : (int)(530 - (now - phase_t));
        if (wait < 1) wait = 1;
        int pr = poll(p, 2, wait);
        if (pr < 0) continue;
        now = mo_now_ms();
        int typed = 0;
        if (pr > 0 && (p[1].revents & (POLLIN | POLLHUP))) {
            unsigned char buf[512];
            ssize_t n = read(master, buf, sizeof buf);
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; i++) feed(buf[i]);
            typed = 1;
        }
        if (pr > 0 && (p[0].revents & (POLLIN | POLLHUP))) {
            if (read(sock, &m, sizeof m) != (ssize_t)sizeof m) break;
            if (m.op == WL_CLOSE) break;
            int resized=thos_resize_event(sock,&m,&resize,&px,&PW,&PH);
            if(resized) {
                if(resized==2) {if(!resize_cells()) break;render();thos_resize_continue(sock,&resize);}
                continue;
            }
            if (m.op == WL_KEY) { key(&m); typed = 1; }
        }
        if (typed) { cur_on = 1; phase_t = now; tween_start(&cur_tw, cur_alpha, 1, 60); }       /* activity: the cursor is solid */
        else if (now - phase_t >= 530 && !fading) { cur_on = !cur_on; phase_t = now; tween_start(&cur_tw, cur_alpha, cur_on ? 1.0f : 0.15f, 140); }
        float na = tween_value(&cur_tw, ease_in_out);
        if (typed || na != cur_alpha) { cur_alpha = na; mark(cy); render(); }
    }
    kill(child, SIGHUP);
    waitpid(child, 0, 0);
    thos_resize_dispose(&resize);free(grid);
    printf("term done\n");
    return 0;
}
