// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop panel: a floating dock along the bottom (Start button, pinned launchers, one button per open window, clock)
// and the Start menu (programs with pin-to-dock / pin-to-desktop toggles, power buttons).
//   /tmp/panel.conf    (else /panel.conf) pinned launchers on the dock, one "Label|command" per line (empty command = terminal)
//   /tmp/desktop.conf  (else /desktop.conf) icons on the desktop (read by the compositor), same format
//   /apps.conf, /opt/apps.conf   extra entries for the Start menu, same format
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
#include "thosui.h"

#define PH 56                                  /* the panel surface: the dock plus a margin of wallpaper around it */
static int PW = 1024;
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
#define DOCK_X 10
#define DOCK_Y 6
#define DOCK_H 44
#define START_X0 (DOCK_X + 6)
#define START_X1 (START_X0 + 40)

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
        const char *sp = cmd + strcspn(cmd, " =");
        if (*sp != '=') break;
        cmd = cmd + strcspn(cmd, " ");
        while (*cmd == ' ') cmd++;
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

/* ---- the dock ---- */
static uint32_t *px;          /* premultiplied ARGB for the compositor */
static uint32_t *col, *mask;  /* the colours drawn over black, and the shape of the dock */
static int clock_w = 78;
static char clock_hm[8], clock_day[24];
static void draw_bar(void) {
    for (int i = 0; i < PW * PH; i++) col[i] = mask[i] = 0;
    int dw = PW - 2 * DOCK_X;
    ui_rrect(mask, PW, PH, DOCK_X, DOCK_Y, dw, DOCK_H, 15, 0xffffffu);
    ui_rrect(col, PW, PH, DOCK_X, DOCK_Y, dw, DOCK_H, 15, 0x363e58u);                                    /* border */
    ui_rrect_g(col, PW, PH, DOCK_X + 1, DOCK_Y + 1, dw - 2, DOCK_H - 2, 14, 0x232a3cu, 0x171c29u, 255);
    /* Start: an accent tile */
    int sy = DOCK_Y + (DOCK_H - 36) / 2;
    ui_rrect_g(col, PW, PH, START_X0, sy, 36, 36, 11, UI_ACCENT_HI, 0x3f69d6u, 255);
    ui_icon(col, PW, PH, START_X0 + 18, sy + 18, 0xf1b2, 18, 0xffffffu);
    /* pinned launchers */
    int x = START_X0 + 36 + 14;
    for (int i = 0; i < nbar; i++) {
        bx0[i] = x; bx1[i] = x + 38;
        ui_tile(col, PW, PH, x + 1, sy, 36, bar[i].label);
        x += 44;
    }
    /* open windows */
    if (ntb) { for (int y = DOCK_Y + 12; y < DOCK_Y + DOCK_H - 12; y++) col[y * PW + x - 2] = 0x39425eu; x += 10; }
    int limit = PW - DOCK_X - clock_w - 24;
    for (int i = 0; i < ntb; i++) {
        char t[16]; snprintf(t, sizeof t, "%.12s", tbs[i].title[0] ? tbs[i].title : "Window");
        int tw = ui_text_width(tbs[i].focused ? UI_BOLD : UI_REG, 12, t), w = 14 + 24 + 8 + tw + 14;
        if (x + w > limit) { tbs[i].x0 = tbs[i].x1 = 0; continue; }
        tbs[i].x0 = x; tbs[i].x1 = x + w;
        int by = DOCK_Y + 5, bh = DOCK_H - 10;
        if (tbs[i].focused) { ui_rrect(col, PW, PH, x, by, w, bh, 10, 0x2d3650u); ui_rrect(col, PW, PH, x + w / 2 - 12, by + bh - 3, 24, 3, 1, UI_ACCENT_HI); }
        else ui_rrect(col, PW, PH, x, by, w, bh, 10, 0x1d2333u);
        ui_rrect_g(col, PW, PH, x + 8, by + (bh - 24) / 2, 24, 24, 7, ui_shade(ui_app_color(tbs[i].title), 15), ui_shade(ui_app_color(tbs[i].title), -20), 255);
        ui_icon(col, PW, PH, x + 20, by + bh / 2, ui_app_glyph(tbs[i].title), 11, 0xffffffu);
        ui_text(col, PW, PH, x + 40, by + bh / 2 + 4, t, tbs[i].focused ? UI_BOLD : UI_REG, 12, tbs[i].focused ? UI_TEXT : 0xb4bbd0u);
        x += w + 6;
    }
    /* clock */
    int cx = PW - DOCK_X - clock_w - 8;
    ui_text(col, PW, PH, cx + (clock_w - ui_text_width(UI_BOLD, 15, clock_hm)) / 2, DOCK_Y + 22, clock_hm, UI_BOLD, 15, UI_TEXT);
    ui_text(col, PW, PH, cx + (clock_w - ui_text_width(UI_REG, 10, clock_day)) / 2, DOCK_Y + 36, clock_day, UI_REG, 10, UI_MUTED);
    for (int i = 0; i < PW * PH; i++) px[i] = ((mask[i] & 255) << 24) | (col[i] & 0x00ffffffu);
}

/* ---- the Start menu ---- */
#define M_W 340
#define M_HEAD 52
#define M_ROW 40
#define M_FOOT 60
#define ROWS (napps)
#define CHIP_W 30
static int mfd = -1, mh, mshm = -1, hover = -1;
static uint32_t *mpx, *mcol, *mmask;
static unsigned long long closed_ms;
static unsigned long long ms_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000 + (unsigned long long)t.tv_nsec / 1000000;
}
static int chip_x(int which) { return M_W - 16 - CHIP_W * 2 - 6 + which * (CHIP_W + 6); }   /* 0 = dock, 1 = desktop */
static void draw_chip(int x, int y, int on, uint32_t glyph) {
    if (on) ui_rrect(mcol, M_W, mh, x, y, CHIP_W, 26, 8, UI_ACCENT);
    else ui_card(mcol, M_W, mh, x, y, CHIP_W, 26, 8, 0x1d2333u, 0x36405c);
    ui_icon(mcol, M_W, mh, x + CHIP_W / 2, y + 13, glyph, 12, on ? 0xffffffu : 0x8e97aeu);
}
static void foot_btn_rect(int i, int *x, int *w) { int gap = 8, bw = (M_W - 32 - 2 * gap) / 3; *x = 16 + i * (bw + gap); *w = bw; }
static void draw_menu(void) {
    for (int i = 0; i < M_W * mh; i++) mcol[i] = mmask[i] = 0;
    ui_rrect(mmask, M_W, mh, 0, 0, M_W, mh, 18, 0xffffffu);
    ui_rrect(mcol, M_W, mh, 0, 0, M_W, mh, 18, 0x39425eu);
    ui_rrect_g(mcol, M_W, mh, 1, 1, M_W - 2, mh - 2, 17, 0x212839u, 0x161b28u, 255);
    ui_text(mcol, M_W, mh, 20, 32, "THOS", UI_BOLD, 18, UI_TEXT);
    ui_text(mcol, M_W, mh, 20 + ui_text_width(UI_BOLD, 18, "THOS") + 10, 32, "Programs", UI_REG, 12, UI_MUTED);
    ui_icon(mcol, M_W, mh, chip_x(0) + CHIP_W / 2, 22, 0xf0c9, 10, UI_MUTED);
    ui_icon(mcol, M_W, mh, chip_x(1) + CHIP_W / 2, 22, 0xf108, 10, UI_MUTED);
    for (int r = 0; r < ROWS; r++) {
        int y = M_HEAD + r * M_ROW;
        if (r == hover) ui_rrect(mcol, M_W, mh, 8, y + 1, M_W - 16, M_ROW - 2, 10, 0x2a3248u);
        ui_rrect_g(mcol, M_W, mh, 16, y + 6, 28, 28, 8, ui_shade(ui_app_color(apps[r].label), 18), ui_shade(ui_app_color(apps[r].label), -18), 255);
        ui_icon(mcol, M_W, mh, 30, y + 20, ui_app_glyph(apps[r].label), 13, 0xffffffu);
        ui_text(mcol, M_W, mh, 56, y + 25, apps[r].label, UI_REG, 14, UI_TEXT);
        draw_chip(chip_x(0), y + 7, find(bar, nbar, apps[r].label) >= 0, 0xf0c9);
        draw_chip(chip_x(1), y + 7, find(dsk, ndsk, apps[r].label) >= 0, 0xf108);
    }
    int fy = mh - M_FOOT + 10;
    for (int i = 0; i < 3; i++) {
        int x, w; foot_btn_rect(i, &x, &w);
        int on = hover == ROWS + i;
        ui_card(mcol, M_W, mh, x, fy, w, 38, 11, on ? (i == 2 ? 0x4a2630u : 0x2a3248u) : 0x1b2130u, on && i == 2 ? 0xb9505eu : 0x34405aU);
        uint32_t ic = i == 0 ? 0xf08b : i == 1 ? IC_REFRESH : IC_POWER;
        const char *lab = i == 0 ? "Quit" : i == 1 ? "Restart" : "Shut down";
        uint32_t c = i == 2 ? 0xff8f9cu : UI_TEXT;
        int tw = ui_text_width(UI_REG, 12, lab);
        ui_icon(mcol, M_W, mh, x + (w - tw - 20) / 2 + 7, fy + 19, ic, 13, c);
        ui_text(mcol, M_W, mh, x + (w - tw - 20) / 2 + 22, fy + 24, lab, UI_REG, 12, c);
    }
    for (int i = 0; i < M_W * mh; i++) mpx[i] = ((mmask[i] & 255) << 24) | (mcol[i] & 0x00ffffffu);
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
    if (!mcol) { mcol = malloc((size_t)M_W * 700 * 4); mmask = malloc((size_t)M_W * 700 * 4); }
    if (!mcol || !mmask || mh > 700) { menu_close(); return; }
    draw_menu();
    struct wl_msg m = { WL_CREATE, M_W, (uint32_t)mh, (uint32_t)key, 3 | 8 };       /* undecorated + start-menu behaviour + alpha */
    (void)!write(mfd, &m, sizeof m);
    if (read(mfd, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { menu_close(); return; }
    shmctl(mshm, IPC_RMID, 0);
    struct wl_msg pos = { WL_POS, 10, m.c - PH - mh - 2, 0, 0 };
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
static void commit_all(int s) { struct wl_msg cm = { WL_COMMIT, 0, 0, (uint32_t)PW, PH }; (void)!write(s, &cm, sizeof cm); }
static void update_clock(void) {
    time_t now = time(0);
    struct tm lt;
    localtime_r(&now, &lt);
    static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }, *mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    snprintf(clock_hm, sizeof clock_hm, "%02d:%02d", lt.tm_hour, lt.tm_min);
    snprintf(clock_day, sizeof clock_day, "%s %d %s", wd[lt.tm_wday], lt.tm_mday, mo[lt.tm_mon]);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    int s = sock_connect();
    if (s < 0) { puts("panel FAIL: connect"); return 1; }
    struct wl_msg m = { WL_HELLO, 0, 0, 0, 0 };
    (void)!write(s, &m, sizeof m);
    if (read(s, &m, sizeof m) == (ssize_t)sizeof m && m.op == WL_CREATED && m.b >= 320 && m.b <= 4096) PW = (int)m.b;   /* span the screen */
    int key = 0x5000 + getpid();
    int id = shmget(key, PW * PH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    col = malloc((size_t)PW * PH * 4); mask = malloc((size_t)PW * PH * 4);
    if (px == (void *)-1 || !col || !mask || gfx_load_font() != 0 || ui_init() != 0) { puts("panel FAIL: shm/font"); return 1; }
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
    update_clock();
    draw_bar();
    m = (struct wl_msg){ WL_CREATE, PW, PH, (uint32_t)key, 1 | 4 | 8 };     /* undecorated, wants the task list, has alpha */
    (void)!write(s, &m, sizeof m);
    if (read(s, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("panel FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);      /* the compositor has attached: the segment lives until both sides unmap it */
    struct wl_msg pos = { WL_POS, 0, m.c - PH, 0, 0 };
    (void)!write(s, &pos, sizeof pos);
    commit_all(s);
    printf("panel ready\n");
    fflush(stdout);
    int shown = -1;
    for (;;) {
        time_t now = time(0);
        int minute = (int)(now / 60);
        if (minute != shown) {                                  /* redraw when the minute changes */
            shown = minute;
            update_clock();
            draw_bar();
            commit_all(s);
        }
        struct pollfd pf[2] = { { s, POLLIN, 0 }, { mfd, POLLIN, 0 } };
        if (poll(pf, 2, 15000) <= 0) continue;
        if (mfd >= 0 && pf[1].revents) {                      /* the menu surface: pointer motion, a click in it, or the compositor closed it */
            if (read(mfd, &m, sizeof m) != (ssize_t)sizeof m || m.op == WL_CLOSE) { menu_close(); continue; }
            if (m.op != WL_POINTER) continue;
            int x = (int)m.a, y = (int)m.b;
            int r = y >= M_HEAD && y < M_HEAD + ROWS * M_ROW ? (y - M_HEAD) / M_ROW : -1;
            int fb = -1;
            if (y >= mh - M_FOOT + 10 && y < mh - M_FOOT + 48) for (int i = 0; i < 3; i++) { int bx, bw; foot_btn_rect(i, &bx, &bw); if (x >= bx && x < bx + bw) fb = i; }
            int hv = r >= 0 ? r : fb >= 0 ? ROWS + fb : -1;
            if (m.c == 0) { if (hv != hover) { hover = hv; draw_menu(); menu_commit(); } continue; }
            if (m.c != 1) continue;
            if (fb >= 0) {                                    /* Quit desktop / Restart / Shut down */
                menu_close();
                if (fb == 0) {
                    int q = sock_connect();
                    struct wl_msg qm = { WL_QUIT, 0, 0, 0, 0 };
                    if (q >= 0) (void)!write(q, &qm, sizeof qm);
                } else if (fork() == 0) {
                    sync();
                    execl(fb == 1 ? "/bin/reboot" : "/bin/poweroff", fb == 1 ? "reboot" : "poweroff", (char *)0);
                    _exit(127);
                }
                continue;
            }
            if (r < 0 || r >= napps) continue;
            if (x >= chip_x(0) - 2) {                         /* a pin toggle */
                if (x < chip_x(1) - 2) toggle_pin(bar, &nbar, MAXB, PANEL_USER, &apps[r]);
                else toggle_pin(dsk, &ndsk, MAXD, DESK_USER, &apps[r]);
                draw_menu();
                menu_commit();
                draw_bar();
                commit_all(s);
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
        if (m.op == WL_WINBEGIN) { ntb_expect = (int)m.a; ntb_new = 0; if (ntb_expect == 0) { ntb = 0; draw_bar(); commit_all(s); } continue; }
        if (m.op == WL_WIN) {
            if (ntb_new < MAXT) {
                tbs_new[ntb_new].id = m.a & 0x7fffffffu; tbs_new[ntb_new].focused = (m.a >> 31) & 1;
                memcpy(tbs_new[ntb_new].title, &m.b, 12); tbs_new[ntb_new].title[12] = 0;
                ntb_new++;
            }
            if (--ntb_expect <= 0) {
                memcpy(tbs, tbs_new, sizeof tbs); ntb = ntb_new;
                draw_bar();
                commit_all(s);
            }
            continue;
        }
        if (m.op == WL_POINTER && m.c == 1) {
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
