// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop panel: an undecorated bar along the bottom with a Start button (menu of programs, each
// pinnable to the taskbar and to the desktop), the pinned launchers, and a clock.
//   /tmp/panel.conf    (else /panel.conf) pinned launchers on the bar, one "Label|command" per line (empty command = terminal)
//   /tmp/desktop.conf  (else /desktop.conf) icons on the desktop (read by the compositor), same format
//   /apps.conf     extra entries for the Start menu, same format
#define _GNU_SOURCE
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"

#define PH 32
static int PW = 1024, SH_H = 768;
#define MAXB 10
#define MAXD 16
#define MAXA 24
struct ent { char label[24]; char cmd[160]; };
static struct ent bar[MAXB], dsk[MAXD], apps[MAXA];
static int nbar, ndsk, napps;
static int bx0[MAXB], bx1[MAXB];
#define PANEL_USER "/tmp/panel.conf"
#define DESK_USER "/tmp/desktop.conf"
struct tbw { uint32_t id; int focused, x0, x1; char title[13]; };
#define MAXT 12
static struct tbw tbs[MAXT], tbs_new[MAXT];
static int ntb, ntb_new, ntb_expect;
#define START_X0 8
#define START_X1 68

static int sock_connect(void) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(s, (void *)&a, 2 + WL_SOCK_LEN) != 0) { close(s); return -1; }
    return s;
}

/* ---- config files ---- */
static int load_list(const char *path, struct ent *e, int max) {
    FILE *f = fopen(path, "r");
    int n = 0;
    for (char line[200]; f && fgets(line, sizeof line, f) && n < max;) {
        char *b = strchr(line, '|');
        if (!b) continue;
        *b = 0;
        b[1 + strcspn(b + 1, "\n")] = 0;
        snprintf(e[n].label, sizeof e[n].label, "%s", line);
        snprintf(e[n].cmd, sizeof e[n].cmd, "%s", b + 1);
        n++;
    }
    if (f) fclose(f);
    return n;
}
static void save_list(const char *path, struct ent *e, int n) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s|%s\n", e[i].label, e[i].cmd);
    fclose(f);
}
static int find(struct ent *e, int n, const char *label) {
    for (int i = 0; i < n; i++) if (!strcmp(e[i].label, label)) return i;
    return -1;
}
/* is the program of a command there? (empty command = the terminal) */
static int cmd_exists(const char *cmd) {
    char prog[160];
    if (!*cmd) return access("/thosterm", X_OK) == 0;
    if (!strncmp(cmd, "TERM:", 5)) cmd += 5;
    while (*cmd) {                                           /* leading VAR=value words are environment */
        const char *sp = cmd + strcspn(cmd, " ="), *w = cmd;
        if (*sp != '=') break;
        cmd = cmd + strcspn(cmd, " ");
        while (*cmd == ' ') cmd++;
        (void)w;
    }
    snprintf(prog, sizeof prog, "%s", cmd);
    prog[strcspn(prog, " ")] = 0;
    return access(prog, X_OK) == 0;
}
static void add_app(const char *label, const char *cmd) {
    if (napps >= MAXA || find(apps, napps, label) >= 0 || !cmd_exists(cmd)) return;
    snprintf(apps[napps].label, sizeof apps[0].label, "%s", label);
    snprintf(apps[napps].cmd, sizeof apps[0].cmd, "%s", cmd);
    napps++;
}
static void toggle_pin(struct ent *list, int *n, int max, const char *path, const struct ent *app) {
    int i = find(list, *n, app->label);
    if (i >= 0) { memmove(&list[i], &list[i + 1], (size_t)(*n - i - 1) * sizeof list[0]); (*n)--; }
    else if (*n < max) list[(*n)++] = *app;
    save_list(path, list, *n);
}

/* what the Start menu offers: the built-in programs that are installed, /apps.conf, and what thos-pkg put in /opt/apps.conf */
static void build_apps(void) {
    napps = 0;
    add_app("Terminal", "");
    add_app("Files", "/thosfiles");
    add_app("Software", "/thossoft");
    add_app("Text editor", "/thostext");
    add_app("Foot terminal", "/usr/bin/foot");
    add_app("Web browser", "/usr/bin/netsurf-gtk https://www.debian.org/");
    add_app("GTK demo", "/gtkhello 70");
    add_app("Win32 demo", "/thoswin");
    add_app("System info", "TERM:/bin/hwreport > /dev/null; /busybox less /tmp/hwreport.txt");
    static struct ent extra[MAXA];
    for (int f = 0; f < 2; f++) {
        int nx = load_list(f ? "/opt/apps.conf" : "/apps.conf", extra, MAXA);
        for (int i = 0; i < nx; i++) add_app(extra[i].label, extra[i].cmd);
    }
}

/* ---- drawing ---- */
static uint32_t *px;
static void rbox(uint32_t *buf, int stride, int x0, int y0, int x1, int y1, int r, uint32_t top, uint32_t bot) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int cx = x - x0 < r ? r - (x - x0) : (x1 - 1 - x < r ? r - (x1 - 1 - x) : 0), cy = y - y0 < r ? r - (y - y0) : (y1 - 1 - y < r ? r - (y1 - 1 - y) : 0);
            if (cx * cx + cy * cy > r * r) continue;
            buf[y * stride + x] = y < (y0 + y1) / 2 ? top : bot;
        }
}
static void draw_bar(void) {
    for (int i = 0; i < PW * PH; i++) px[i] = 0x00282832;
    rbox(px, PW, START_X0, 4, START_X1, PH - 4, 5, 0x0058a05c, 0x00468a4a);
    gfx_str(px, PW, PH, START_X0 + 10, 8, "Start", 0x00ffffff, 0xFFFFFFFFu);
    for (int i = 0, x = START_X1 + 8; i < nbar; i++) {
        bx0[i] = x; bx1[i] = x + 20 + 8 * (int)strlen(bar[i].label);
        rbox(px, PW, bx0[i], 4, bx1[i], PH - 4, 5, 0x00467bb8, 0x00386aa5);
        gfx_str(px, PW, PH, bx0[i] + 10, 8, bar[i].label, 0x00ffffff, 0xFFFFFFFFu);
        x = bx1[i] + 8;
    }
    /* the open windows: a darker button each, the focused one lit */
    int x = nbar ? bx1[nbar - 1] + 8 : START_X1 + 8;
    if (ntb) { for (int y = 6; y < PH - 6; y++) px[y * PW + x] = 0x00505068; x += 8; }
    for (int i = 0; i < ntb; i++) {
        int w = 20 + 8 * (int)strlen(tbs[i].title);
        if (w < 60) w = 60;
        if (x + w > PW - 90) { tbs[i].x0 = tbs[i].x1 = 0; continue; }
        tbs[i].x0 = x; tbs[i].x1 = x + w;
        if (tbs[i].focused) rbox(px, PW, x, 4, x + w, PH - 4, 5, 0x006a7fa8, 0x00566a94);
        else rbox(px, PW, x, 4, x + w, PH - 4, 5, 0x00444458, 0x003a3a4c);
        gfx_str(px, PW, PH, x + 10, 8, tbs[i].title, tbs[i].focused ? 0x00ffffff : 0x00c0c0d0, 0xFFFFFFFFu);
        x += w + 6;
    }
}

/* the start menu */
#define M_W 280
#define M_HEAD 30
#define M_ROW 28
#define M_FOOT 24
static int mfd = -1, mh, mshm = -1, hover = -1;
static uint32_t *mpx;
static unsigned long long closed_ms;
static unsigned long long ms_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000 + (unsigned long long)t.tv_nsec / 1000000;
}
#define ROWS (napps + 3)            /* the programs, then "Quit desktop", "Restart", "Shut down" */
static void draw_toggle(int x, int y, int on, char letter) {
    for (int j = 0; j < 18; j++)
        for (int i = 0; i < 20; i++) {
            int edge = j == 0 || j == 17 || i == 0 || i == 19;
            mpx[(y + j) * M_W + x + i] = on ? (edge ? 0x00a8d8ff : 0x004a8ad8) : (edge ? 0x00707090 : 0x00303044);
        }
    gfx_char(mpx, M_W, mh, x + 6, y + 1, (unsigned char)letter, on ? 0x00ffffff : 0x00888899, 0xFFFFFFFFu);
}
static void draw_menu(void) {
    for (int i = 0; i < M_W * mh; i++) mpx[i] = 0x00262634;
    for (int i = 0; i < M_W; i++) { mpx[i] = mpx[(mh - 1) * M_W + i] = 0x00606078; }
    for (int j = 0; j < mh; j++) { mpx[j * M_W] = mpx[j * M_W + M_W - 1] = 0x00606078; }
    for (int j = 1; j < M_HEAD; j++) for (int i = 1; i < M_W - 1; i++) mpx[j * M_W + i] = 0x00363650;
    gfx_str(mpx, M_W, mh, 12, 7, "THOS", 0x00ffffff, 0xFFFFFFFFu);
    for (int r = 0; r < ROWS; r++) {
        int y = M_HEAD + r * M_ROW;
        if (r == hover) for (int j = 0; j < M_ROW; j++) for (int i = 1; i < M_W - 1; i++) mpx[(y + j) * M_W + i] = 0x00364868;
        if (r >= napps) {
            if (r == napps) for (int i = 1; i < M_W - 1; i++) mpx[y * M_W + i] = 0x00606078;
            gfx_str(mpx, M_W, mh, 12, y + 6, r == napps ? "Quit desktop" : r == napps + 1 ? "Restart" : "Shut down", 0x00ff9090, 0xFFFFFFFFu);
            continue;
        }
        gfx_str(mpx, M_W, mh, 12, y + 6, apps[r].label, 0x00eeeeee, 0xFFFFFFFFu);
        draw_toggle(M_W - 52, y + 5, find(bar, nbar, apps[r].label) >= 0, 'T');
        draw_toggle(M_W - 28, y + 5, find(dsk, ndsk, apps[r].label) >= 0, 'D');
    }
    gfx_str(mpx, M_W, mh, 12, mh - M_FOOT + 4, "T = taskbar   D = desktop", 0x00888899, 0xFFFFFFFFu);
}
static void menu_commit(void) {
    struct wl_msg cm = { WL_COMMIT, 0, 0, M_W, (uint32_t)mh };
    (void)!write(mfd, &cm, sizeof cm);
}
static void menu_close(void) {
    if (mfd < 0) return;
    close(mfd);
    mfd = -1;
    if (mpx) shmdt(mpx);
    mpx = 0;
    closed_ms = ms_now();
}
static void menu_open(void) {
    build_apps();
    hover = -1;
    mh = M_HEAD + ROWS * M_ROW + M_FOOT;
    mfd = sock_connect();
    if (mfd < 0) return;
    int key = 0x5800 + (getpid() & 0x7ff);
    mshm = shmget(key, M_W * mh * 4, IPC_CREAT | 0600);
    mpx = mshm >= 0 ? shmat(mshm, 0, 0) : (void *)-1;
    if (mpx == (void *)-1) { mpx = 0; close(mfd); mfd = -1; return; }
    draw_menu();
    struct wl_msg m = { WL_CREATE, M_W, (uint32_t)mh, (uint32_t)key, 3 };       /* undecorated + start-menu behaviour */
    (void)!write(mfd, &m, sizeof m);
    if (read(mfd, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { menu_close(); return; }
    shmctl(mshm, IPC_RMID, 0);
    struct wl_msg pos = { WL_POS, 0, m.c - PH - mh, 0, 0 };
    (void)!write(mfd, &pos, sizeof pos);
    menu_commit();
}

static void launch(const char *cmd) {
    if (fork() != 0) return;
    if (!*cmd) execl("/thosterm", "thosterm", "-l", (char *)0);
    if (!strncmp(cmd, "TERM:", 5)) execl("/thosterm", "thosterm", "-c", cmd + 5, (char *)0);          /* "TERM:command" runs in a terminal window */
    setenv("XDG_RUNTIME_DIR", "/run", 1); setenv("HOME", "/tmp", 1); setenv("LC_ALL", "C.UTF-8", 1);
    setenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf", 1); setenv("GDK_BACKEND", "wayland", 1);
    execl("/busybox", "sh", "-c", cmd, (char *)0);
    _exit(127);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    int s = sock_connect();
    if (s < 0) { puts("panel FAIL: connect"); return 1; }
    struct wl_msg m = { WL_HELLO, 0, 0, 0, 0 };
    (void)!write(s, &m, sizeof m);
    if (read(s, &m, sizeof m) == (ssize_t)sizeof m && m.op == WL_CREATED && m.b >= 320 && m.b <= 4096) { PW = (int)m.b; SH_H = (int)m.c; }   /* span the screen */
    int key = 0x5000 + getpid();
    int id = shmget(key, PW * PH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0) { puts("panel FAIL: shm/font"); return 1; }
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);          /* Central Europe, no zone database needed */

    build_apps();
    /* the user's pins live in /tmp (writable for everyone); /panel.conf and /desktop.conf are the system defaults */
    nbar = load_list(access(PANEL_USER, F_OK) == 0 ? PANEL_USER : "/panel.conf", bar, MAXB);
    if (nbar == 0 && access(PANEL_USER, F_OK) != 0 && access("/panel.conf", F_OK) != 0) {     /* first start: a terminal launcher */
        strcpy(bar[0].label, "Terminal");
        bar[0].cmd[0] = 0;
        nbar = 1;
    }
    ndsk = load_list(access(DESK_USER, F_OK) == 0 ? DESK_USER : "/desktop.conf", dsk, MAXD);

    draw_bar();
    m = (struct wl_msg){ WL_CREATE, PW, PH, (uint32_t)key, 5 };     /* undecorated + wants the task list */
    (void)!write(s, &m, sizeof m);
    if (read(s, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("panel FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);      /* the compositor has attached: the segment lives until both sides unmap it */
    struct wl_msg pos = { WL_POS, 0, m.c - PH, 0, 0 };
    (void)!write(s, &pos, sizeof pos);
    struct wl_msg cm = { WL_COMMIT, 0, 0, PW, PH };
    (void)!write(s, &cm, sizeof cm);
    printf("panel ready\n");
    fflush(stdout);
    int shown = -1;
    for (;;) {
        /* clock: HH:MM (local time) at the right edge, redrawn when the minute changes */
        time_t now = time(0);
        struct tm lt;
        localtime_r(&now, &lt);
        int minute = (int)(now / 60);
        if (minute != shown) {
            shown = minute;
            char txt[8];
            int hh = lt.tm_hour, mm = lt.tm_min;
            txt[0] = '0' + hh / 10; txt[1] = '0' + hh % 10; txt[2] = ':'; txt[3] = '0' + mm / 10; txt[4] = '0' + mm % 10; txt[5] = 0;
            for (int y = 4; y < PH - 4; y++) for (int x = PW - 70; x < PW - 8; x++) px[y * PW + x] = 0x00282832;
            gfx_str(px, PW, PH, PW - 62, 8, txt, 0x00dddddd, 0xFFFFFFFFu);
            struct wl_msg u = { WL_COMMIT, PW - 70, 0, 70, PH };
            (void)!write(s, &u, sizeof u);
        }
        struct pollfd pf[2] = { { s, POLLIN, 0 }, { mfd, POLLIN, 0 } };
        if (poll(pf, 2, 15000) <= 0) continue;
        if (mfd >= 0 && pf[1].revents) {                      /* the menu surface: a click in it, or the compositor closed it */
            if (read(mfd, &m, sizeof m) != (ssize_t)sizeof m || m.op == WL_CLOSE) { menu_close(); continue; }
            if (m.op == WL_POINTER && m.c == 0) {            /* motion: light up the row under the pointer */
                int hr = (int)m.b >= M_HEAD ? ((int)m.b - M_HEAD) / M_ROW : -1;
                if (hr >= ROWS) hr = -1;
                if (hr != hover) { hover = hr; draw_menu(); menu_commit(); }
                continue;
            }
            if (m.op != WL_POINTER || m.c != 1) continue;
            int x = (int)m.a, y = (int)m.b, r = (y - M_HEAD) / M_ROW;
            if (y < M_HEAD || r >= ROWS) continue;
            if (r >= napps) {
                menu_close();
                if (r == napps) {                             /* Quit desktop */
                    int q = sock_connect();
                    struct wl_msg qm = { WL_QUIT, 0, 0, 0, 0 };
                    if (q >= 0) (void)!write(q, &qm, sizeof qm);
                } else if (fork() == 0) {                     /* Restart / Shut down: the power tools in /bin */
                    sync();
                    execl(r == napps + 1 ? "/bin/reboot" : "/bin/poweroff", r == napps + 1 ? "reboot" : "poweroff", (char *)0);
                    _exit(127);
                }
                continue;
            }
            if (x >= M_W - 54) {                              /* a pin toggle */
                if (x < M_W - 30) toggle_pin(bar, &nbar, MAXB, PANEL_USER, &apps[r]);
                else toggle_pin(dsk, &ndsk, MAXD, DESK_USER, &apps[r]);
                draw_menu();
                menu_commit();
                draw_bar();
                shown = -1;
                cm = (struct wl_msg){ WL_COMMIT, 0, 0, PW, PH };
                (void)!write(s, &cm, sizeof cm);
                continue;
            }
            char cmd[160];
            snprintf(cmd, sizeof cmd, "%s", apps[r].cmd);
            menu_close();
            launch(cmd);
            continue;
        }
        if (!(pf[0].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        if (read(s, &m, sizeof m) != (ssize_t)sizeof m) break;
        if (m.op == WL_CLOSE) break;
        if (m.op == WL_WINBEGIN) { ntb_expect = (int)m.a; ntb_new = 0; if (ntb_expect == 0) { ntb = 0; draw_bar(); shown = -1; cm = (struct wl_msg){ WL_COMMIT, 0, 0, PW, PH }; (void)!write(s, &cm, sizeof cm); } continue; }
        if (m.op == WL_WIN) {
            if (ntb_new < MAXT) {
                tbs_new[ntb_new].id = m.a & 0x7fffffffu; tbs_new[ntb_new].focused = (m.a >> 31) & 1;
                memcpy(tbs_new[ntb_new].title, &m.b, 12); tbs_new[ntb_new].title[12] = 0;
                ntb_new++;
            }
            if (--ntb_expect <= 0) {
                memcpy(tbs, tbs_new, sizeof tbs); ntb = ntb_new;
                draw_bar(); shown = -1;
                cm = (struct wl_msg){ WL_COMMIT, 0, 0, PW, PH };
                (void)!write(s, &cm, sizeof cm);
            }
            continue;
        }
        if (m.op == WL_POINTER) {
            if ((int)m.a >= START_X0 && (int)m.a < START_X1) {
                if (mfd >= 0) menu_close();
                else if (ms_now() - closed_ms > 400) menu_open();    /* (a click outside the menu has just closed it) */
                continue;
            }
            for (int i = 0; i < nbar; i++)
                if ((int)m.a >= bx0[i] && (int)m.a < bx1[i]) { launch(bar[i].cmd); break; }
            for (int i = 0; i < ntb; i++)
                if (tbs[i].x1 && (int)m.a >= tbs[i].x0 && (int)m.a < tbs[i].x1) { struct wl_msg r = { WL_RAISE, tbs[i].id, 0, 0, 0 }; (void)!write(s, &r, sizeof r); break; }
        }
    }
    return 0;
}
