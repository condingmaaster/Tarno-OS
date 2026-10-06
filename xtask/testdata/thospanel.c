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
#include "thosmotion.h"

#define PH 64                                  /* the panel surface: floating glass pills with a margin of wallpaper around them */
static int PW = 1024;
#define MAXB 10
#define MAXD 16
#define MAXA 24
struct ent { char label[24]; char cmd[160]; };
static struct ent bar[MAXB], dsk[MAXD], apps[MAXA];
static int nbar, ndsk, napps;
#define PANEL_USER "/tmp/panel.conf"
#define DESK_USER "/tmp/desktop.conf"
struct tbw { uint32_t id; int focused, minimized, x0, x1; char title[13]; };
#define MAXT 12
static struct tbw tbs[MAXT], tbs_new[MAXT];
static int ntb, ntb_new, ntb_expect;
#define DOCK_X 12
#define DOCK_Y 9
#define DOCK_H 46
#define GLASS 236                              /* the pills are slightly see-through */
static int start_x0, start_x1;

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
    add_app("Settings", "/thossettings");
    add_app("Text editor", "/thosedit");
    add_app("Foot terminal", "/usr/bin/foot");
    add_app("Web browser", "/usr/bin/netsurf-gtk https://www.debian.org/");
    add_app("GTK demo", "/gtkhello 70");
    add_app("Win32 demo", "/thoswin");
    { FILE *bd = fopen("/proc/bootdisk", "r");              /* booted from a stick: offer the installer */
      char b[16] = "";
      if (bd) { if (fgets(b, sizeof b, bd)) {} fclose(bd); }
      if (!strncmp(b, "usb", 3)) add_app("Install THOS", "TERM:/bin/thos-install"); }
    add_app("System info", "TERM:/bin/hwreport > /dev/null; /busybox less /tmp/hwreport.txt");
    static struct ent extra[MAXA];
    for (int f = 0; f < 2; f++) {
        int nx = load_list(f ? "/opt/apps.conf" : "/apps.conf", extra, MAXA);
        for (int i = 0; i < nx; i++) add_app(extra[i].label, extra[i].cmd);
    }
}

/* ---- the dock: three floating glass pills (Start, apps and windows, clock) ----
   The middle pill is a row of items on springs: launchers, a separator, one button per open window. A new item grows in, a closed
   one shrinks away, and the pill stretches or contracts with the sum of their widths, so it overshoots and settles like a spring. */
static uint32_t *px;          /* premultiplied ARGB for the compositor */
static uint32_t *col, *mask;  /* the colours drawn over black, and the shape of the pills */
static char clock_hm[8], clock_day[24];
static int hov_start;         /* the pointer is over the Start pill */
enum { K_LAUNCH = 0, K_SEP = 1, K_WIN = 2 };
#define MAXI 40
struct dk { int kind, key, focused, minimized, present, x0, x1; char label[16]; struct spring w, pres, hov, foc, dip; };
static struct dk items[MAXI];
static int nitems;
static struct spring start_hov, start_dip;                /* dip: the press feedback, 1 at the moment of the click and springing back to 0 */
static int dock_avail(void) { return PW - 2 * DOCK_X - 118 - 112 - 2 * 14 - 20; }
static struct dk *item_new(int kind, int key, const char *label, int at) {
    if (nitems >= MAXI) return 0;
    memmove(&items[at + 1], &items[at], (size_t)(nitems - at) * sizeof items[0]);
    struct dk *d = &items[at];
    memset(d, 0, sizeof *d);
    d->kind = kind; d->key = key; d->present = 1;
    snprintf(d->label, sizeof d->label, "%s", label);
    spring_init(&d->w, 0, SP_DOCK); spring_init(&d->pres, 0, SP_SNAPPY); spring_init(&d->hov, 0, SP_HOVER); spring_init(&d->foc, 0, SP_SNAPPY); spring_init(&d->dip, 0, SP_SNAPPY);
    nitems++;
    return d;
}
static int launcher_key(const char *label) { int h = 7; for (const char *p = label; *p; p++) h = h * 31 + (unsigned char)*p; return h & 0x7fffffff; }
/* the width a window button wants */
static float win_want(const struct dk *d) { char t[16]; snprintf(t, sizeof t, "%.12s", d->label[0] ? d->label : "Window"); return (float)(14 + 24 + 8 + ui_text_width(d->focused ? UI_BOLD : UI_REG, 12, t) + 14 + 6); }
/* bring the items in line with the pinned launchers (bar[]) and the window list (tbs[]) and set every spring's target */
static void dock_sync(void) {
    for (int i = 0; i < nitems; i++) items[i].present = items[i].kind == K_SEP;
    for (int i = 0; i < nbar; i++) {                                         /* launchers, in pin order, before the separator */
        int key = launcher_key(bar[i].label), at = -1, last = -1;
        for (int j = 0; j < nitems; j++) { if (items[j].kind == K_LAUNCH) last = j; if (items[j].kind == K_LAUNCH && items[j].key == key) at = j; }
        if (at < 0) { struct dk *d = item_new(K_LAUNCH, key, bar[i].label, last + 1); (void)d; for (int j = 0; j < nitems; j++) if (items[j].kind == K_LAUNCH && items[j].key == key) at = j; }
        if (at >= 0) items[at].present = 1;
    }
    int has_sep = 0;
    for (int j = 0; j < nitems; j++) if (items[j].kind == K_SEP) has_sep = 1;
    if (!has_sep) { int at = 0; for (int j = 0; j < nitems; j++) if (items[j].kind == K_LAUNCH) at = j + 1; item_new(K_SEP, 0, "", at); for (int j = 0; j < nitems; j++) if (items[j].kind == K_SEP) items[j].present = 1; }
    for (int i = 0; i < ntb; i++) {                                           /* windows, in creation order, at the end */
        int at = -1;
        for (int j = 0; j < nitems; j++) if (items[j].kind == K_WIN && items[j].key == (int)tbs[i].id) at = j;
        if (at < 0) { item_new(K_WIN, (int)tbs[i].id, tbs[i].title, nitems); at = nitems - 1; }
        items[at].present = 1; items[at].focused = tbs[i].focused; items[at].minimized = tbs[i].minimized;
        snprintf(items[at].label, sizeof items[at].label, "%s", tbs[i].title);
    }
    /* targets; when the windows would not fit, their buttons share the room that is left */
    float fixed = 0, want = 0; int nw = 0;
    for (int j = 0; j < nitems; j++) {
        struct dk *d = &items[j];
        if (d->kind == K_LAUNCH && d->present) fixed += 46;
        if (d->kind == K_WIN && d->present) { want += win_want(d); nw++; }
    }
    if (nw) fixed += 12;
    float room = (float)dock_avail() - fixed, scale = want > room && want > 0 ? (room > 40 * nw ? room / want : (40.0f * nw) / want) : 1.0f;
    for (int j = 0; j < nitems; j++) {
        struct dk *d = &items[j];
        float w = d->kind == K_LAUNCH ? 46 : d->kind == K_SEP ? (nw ? 12 : 0) : win_want(d) * scale;
        if (!d->present) w = 0;
        spring_to(&d->w, w);
        spring_to(&d->pres, d->present ? 1.0f : 0.0f);
        spring_to(&d->foc, d->focused ? 1.0f : 0.0f);
    }
}
static int item_busy(const struct dk *d) { return spring_busy(&d->w) || spring_busy(&d->pres) || spring_busy(&d->hov) || spring_busy(&d->foc) || spring_busy(&d->dip); }
static int dock_busy(void) {
    if (spring_busy(&start_hov) || spring_busy(&start_dip)) return 1;
    for (int j = 0; j < nitems; j++) if (item_busy(&items[j])) return 1;
    return 0;
}
static void dock_step(float dt) {
    spring_step(&start_hov, dt); spring_step(&start_dip, dt);
    for (int j = 0; j < nitems; j++) {
        struct dk *d = &items[j];
        spring_step(&d->w, dt); spring_step(&d->pres, dt); spring_step(&d->hov, dt); spring_step(&d->foc, dt); spring_step(&d->dip, dt);
        if (d->w.p < 0) d->w.p = 0;
    }
    for (int j = nitems - 1; j >= 0; j--)                                     /* gone for good once shrunk away */
        if (!items[j].present && items[j].w.p < 0.6f && items[j].pres.p < 0.03f && !item_busy(&items[j])) { memmove(&items[j], &items[j + 1], (size_t)(nitems - j - 1) * sizeof items[0]); nitems--; }
}
static void pill(int x, int w) {                                    /* one glass capsule: rim light on top, a tinted body, a gloss sheen */
    if (w < 8) return;
    ui_rrect(mask, PW, PH, x, DOCK_Y, w, DOCK_H, 17, 0xffffffu);
    ui_rrect_g(col, PW, PH, x, DOCK_Y, w, DOCK_H, 17, 0x5b678c, 0x2a3148, 255);                       /* rim */
    ui_rrect_g(col, PW, PH, x + 1, DOCK_Y + 1, w - 2, DOCK_H - 2, 16, 0x262d44, 0x151a2a, 255);
    ui_rrect_ga(col, PW, PH, x + 2, DOCK_Y + 2, w - 4, DOCK_H / 2 - 2, 15, 0xffffffu, 0xffffffu, 34, 4);   /* sheen */
}
static unsigned fadeto(float a) { return a <= 0 ? 0 : a >= 1 ? 255 : (unsigned)(a * 255); }
static void draw_bar(void) {
    for (int i = 0; i < PW * PH; i++) col[i] = mask[i] = 0;
    int sy = DOCK_Y + (DOCK_H - 36) / 2;
    /* left: Start */
    int lw = 118;
    pill(DOCK_X, lw);
    start_x0 = DOCK_X; start_x1 = DOCK_X + lw;
    if (start_hov.p > 0.02f) ui_rrect_g(col, PW, PH, DOCK_X + 3, sy - 3, lw - 6, 42, 14, 0x2e3858u, 0x2e3858u, fadeto(start_hov.p));
    float lift = start_hov.p * 1.0f;
    int ss = (int)(36 * (1.0f - 0.16f * (start_dip.p > 1 ? 1 : start_dip.p)) + 0.5f), so = (36 - ss) / 2;
    ui_rrect_g(col, PW, PH, DOCK_X + 6 + so, sy - (int)(lift + 0.5f) + so, ss, ss, 12, UI_ACCENT_HI, 0x3560d4, 255);
    ui_gloss(col, PW, PH, DOCK_X + 6 + so, sy - (int)(lift + 0.5f) + so, ss, ss, 12);
    ui_icon(col, PW, PH, DOCK_X + 24, sy + 18 - (int)(lift + 0.5f), 0xf00a, 16, 0xffffffu);
    ui_text(col, PW, PH, DOCK_X + 52, DOCK_Y + 29, "Start", UI_BOLD, 15, UI_TEXT);
    /* right: clock */
    int rw = 112, rx = PW - DOCK_X - rw;
    pill(rx, rw);
    ui_text(col, PW, PH, rx + (rw - ui_text_width(UI_BOLD, 17, clock_hm)) / 2, DOCK_Y + 22, clock_hm, UI_BOLD, 17, UI_TEXT);
    ui_text(col, PW, PH, rx + (rw - ui_text_width(UI_REG, 11, clock_day)) / 2, DOCK_Y + 38, clock_day, UI_REG, 11, UI_MUTED);
    /* centre: the items on springs */
    float sum = 0;
    for (int j = 0; j < nitems; j++) sum += items[j].w.p;
    int aw = (int)(sum + 20.5f), ax = (PW - aw) / 2;
    if (ax < DOCK_X + lw + 14) ax = DOCK_X + lw + 14;
    if (nitems && aw >= 20) pill(ax, aw);
    float xf = (float)ax + 10;
    for (int j = 0; j < nitems; j++) {
        struct dk *d = &items[j];
        int x = (int)(xf + 0.5f), w = (int)(d->w.p + 0.5f);
        d->x0 = x; d->x1 = x + w;
        xf += d->w.p;
        if (w < 2) continue;
        float pr = d->pres.p < 0 ? 0 : d->pres.p > 1.2f ? 1.2f : d->pres.p;
        if (d->kind == K_SEP) {
            for (int y = DOCK_Y + 13; y < DOCK_Y + DOCK_H - 13; y++) col[y * PW + x + w / 2] = ui_mix(col[y * PW + x + w / 2], 0x4a5578u, fadeto(pr));
        } else if (d->kind == K_LAUNCH) {
            float h = d->hov.p < 0 ? 0 : d->hov.p;
            float dp = d->dip.p > 1 ? 1 : d->dip.p < 0 ? 0 : d->dip.p;
            int size = (int)((36 + 6 * h) * (0.55f + 0.45f * (pr > 1 ? 1 : pr)) * (1.0f - 0.16f * dp) + 0.5f), cx = x + w / 2;
            int ty = sy + (36 - size) / 2 - (int)(7 * h + 0.5f);
            if (h > 0.02f) ui_rrect_g(col, PW, PH, cx - 22, sy - 4, 44, 44, 14, 0x2e3858u, 0x2e3858u, fadeto(h));
            ui_set_clip(x, 0, x + w, PH);
            ui_tile(col, PW, PH, cx - size / 2, ty, size, d->label);
            ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
        } else {
            float dp = d->dip.p > 1 ? 1 : d->dip.p < 0 ? 0 : d->dip.p;
            int pin = (int)(dp * 4 + 0.5f), by = DOCK_Y + 5 + pin, bh = DOCK_H - 10 - 2 * pin;
            float f = d->foc.p < 0 ? 0 : d->foc.p > 1 ? 1 : d->foc.p, h = d->hov.p < 0 ? 0 : d->hov.p > 1 ? 1 : d->hov.p;
            ui_set_clip(x, 0, x + w - 6 > x ? x + w - 6 : x + 1, PH);
            uint32_t bg0 = ui_mix(ui_mix(0x1c2234u, 0x2a3350u, fadeto(h)), 0x34405fu, fadeto(f)), bg1 = ui_mix(ui_mix(0x1c2234u, 0x2a3350u, fadeto(h)), 0x232c45u, fadeto(f));
            ui_rrect_g(col, PW, PH, x + pin, by, w - 6 - 2 * pin, bh, 12, bg0, bg1, 255);
            if (f > 0.02f) { ui_gloss(col, PW, PH, x, by, w - 6, bh, 12); int iw = (int)(24 * f); ui_rrect(col, PW, PH, x + (w - 6) / 2 - iw / 2, by + bh - 3, iw > 1 ? iw : 1, 3, 1, UI_ACCENT_HI); }
            char t[16]; snprintf(t, sizeof t, "%.12s", d->label[0] ? d->label : "Window");
            ui_rrect_g(col, PW, PH, x + 8, by + (bh - 24) / 2, 24, 24, 8, ui_shade(ui_app_color(d->label), 18), ui_shade(ui_app_color(d->label), -20), 255);
            ui_icon(col, PW, PH, x + 20, by + bh / 2, ui_app_glyph(d->label), 11, 0xffffffu);
            ui_text(col, PW, PH, x + 40, by + bh / 2 + 4, t, f > 0.5f ? UI_BOLD : UI_REG, 12, d->minimized ? 0x7a829cu : ui_mix(0xb4bbd0u, UI_TEXT, fadeto(f)));
            if (d->minimized) ui_disc(col, PW, PH, x + (w - 6) / 2, by + bh - 4, 2, 0x7a829cu);
            ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
        }
    }
    for (int i = 0; i < PW * PH; i++) {                                 /* glass: premultiplied ARGB at GLASS/255 opacity */
        unsigned a = (mask[i] & 255) * GLASS / 255;
        unsigned r = ((col[i] >> 16) & 255) * GLASS / 255, g = ((col[i] >> 8) & 255) * GLASS / 255, b = (col[i] & 255) * GLASS / 255;
        px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

/* ---- the Start menu ---- */
#define M_W 340
#define M_HEAD 52
#define M_ROW 40
#define M_FOOT 60
#define ROWS (napps)
#define CHIP_W 30
static int mfd = -1, mh, mshm = -1, hover = -1;
static struct spring mh_y, mh_a, mh_foot;      /* the menu's hover highlight glides between rows; the footer buttons fade */
static int menu_busy(void) { return mfd >= 0 && (spring_busy(&mh_y) || spring_busy(&mh_a) || spring_busy(&mh_foot)); }
static void menu_hover(int hv) {
    if (hv == hover) return;
    if (hv >= 0 && hv < napps) { if (hover < 0 || hover >= napps) spring_init(&mh_y, (float)(hv * M_ROW), SP_HOVER); spring_to(&mh_y, (float)(hv * M_ROW)); spring_to(&mh_a, 1); spring_to(&mh_foot, 0); }
    else if (hv >= napps) { spring_to(&mh_a, 0); spring_to(&mh_foot, 1); }
    else { spring_to(&mh_a, 0); spring_to(&mh_foot, 0); }
    hover = hv;
}
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
    if (mh_a.p > 0.02f) ui_rrect_g(mcol, M_W, mh, 8, M_HEAD + (int)(mh_y.p + 0.5f) + 1, M_W - 16, M_ROW - 2, 10, 0x2a3248u, 0x2a3248u, (unsigned)(mh_a.p > 1 ? 255 : mh_a.p * 255));
    for (int r = 0; r < ROWS; r++) {
        int y = M_HEAD + r * M_ROW;
        ui_rrect_g(mcol, M_W, mh, 16, y + 6, 28, 28, 8, ui_shade(ui_app_color(apps[r].label), 18), ui_shade(ui_app_color(apps[r].label), -18), 255);
        ui_icon(mcol, M_W, mh, 30, y + 20, ui_app_glyph(apps[r].label), 13, 0xffffffu);
        ui_text(mcol, M_W, mh, 56, y + 25, apps[r].label, UI_REG, 14, UI_TEXT);
        draw_chip(chip_x(0), y + 7, find(bar, nbar, apps[r].label) >= 0, 0xf0c9);
        draw_chip(chip_x(1), y + 7, find(dsk, ndsk, apps[r].label) >= 0, 0xf108);
    }
    int fy = mh - M_FOOT + 10;
    for (int i = 0; i < 3; i++) {
        int x, w; foot_btn_rect(i, &x, &w);
        int on = hover == ROWS + i && mh_foot.p > 0.5f;
        ui_card(mcol, M_W, mh, x, fy, w, 38, 11, on ? (i == 2 ? 0x4a2630u : 0x2a3248u) : 0x1b2130u, on && i == 2 ? 0xb9505eu : 0x34405aU);
        uint32_t ic = i == 0 ? 0xf08b : i == 1 ? IC_REFRESH : IC_POWER;
        const char *lab = i == 0 ? "Quit" : i == 1 ? "Restart" : "Shut down";
        uint32_t c = i == 2 ? 0xff8f9cu : UI_TEXT;
        int tw = ui_text_width(UI_REG, 12, lab);
        ui_icon(mcol, M_W, mh, x + (w - tw - 20) / 2 + 7, fy + 19, ic, 13, c);
        ui_text(mcol, M_W, mh, x + (w - tw - 20) / 2 + 22, fy + 24, lab, UI_REG, 12, c);
    }
    ui_rrect_ga(mcol, M_W, mh, 2, 2, M_W - 4, 70, 16, 0xffffffu, 0xffffffu, 26, 0);                          /* sheen across the top */
    for (int i = 0; i < M_W * mh; i++) {                                                                     /* glass: slightly see-through */
        unsigned a = (mmask[i] & 255) * 244 / 255, r = ((mcol[i] >> 16) & 255) * 244 / 255, g = ((mcol[i] >> 8) & 255) * 244 / 255, b = (mcol[i] & 255) * 244 / 255;
        mpx[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
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
    spring_init(&mh_y, 0, SP_HOVER); spring_init(&mh_a, 0, SP_HOVER); spring_init(&mh_foot, 0, SP_HOVER);
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
    int shown = -1, redraw = 0, log_pending = 0;
    long long last_ms = mo_now_ms();
    spring_init(&start_hov, 0, SP_HOVER); spring_init(&start_dip, 0, SP_SNAPPY);
    dock_sync();
    for (;;) {
        time_t now = time(0);
        int minute = (int)(now / 60);
        if (minute != shown) { shown = minute; update_clock(); redraw = 1; }        /* the clock changes once a minute */
        long long nowm = mo_now_ms();
        float dt = (float)(nowm - last_ms) / 1000.0f;
        last_ms = nowm;
        int animating = dock_busy();
        if (animating) dock_step(dt);
        if (animating || redraw) { redraw = 0; draw_bar(); commit_all(s); }
        if (menu_busy()) { spring_step(&mh_y, dt); spring_step(&mh_a, dt); spring_step(&mh_foot, dt); draw_menu(); menu_commit(); }
        if (log_pending && !dock_busy()) {                                          /* the layout, for the desktop tests, once everything has settled */
            log_pending = 0;
            printf("panel tasks:");
            for (int j = 0; j < nitems; j++) if (items[j].kind == K_WIN && items[j].present) printf(" %s%s%s=%d-%d", items[j].label, items[j].focused ? "*" : "", items[j].minimized ? "~" : "", items[j].x0, items[j].x1);
            printf("\n");
            fflush(stdout);
        }
        struct pollfd pf[2] = { { s, POLLIN, 0 }, { mfd, POLLIN, 0 } };
        if (poll(pf, 2, dock_busy() || menu_busy() ? MO_FRAME_MS : 15000) <= 0) continue;
        if (mfd >= 0 && pf[1].revents) {                      /* the menu surface: pointer motion, a click in it, or the compositor closed it */
            if (read(mfd, &m, sizeof m) != (ssize_t)sizeof m || m.op == WL_CLOSE) { menu_close(); continue; }
            if (m.op != WL_POINTER) continue;
            int x = (int)m.a, y = (int)m.b;
            int r = y >= M_HEAD && y < M_HEAD + ROWS * M_ROW ? (y - M_HEAD) / M_ROW : -1;
            int fb = -1;
            if (y >= mh - M_FOOT + 10 && y < mh - M_FOOT + 48) for (int i = 0; i < 3; i++) { int bx, bw; foot_btn_rect(i, &bx, &bw); if (x >= bx && x < bx + bw) fb = i; }
            int hv = r >= 0 ? r : fb >= 0 ? ROWS + fb : -1;
            if (m.c == 0) { menu_hover(hv); continue; }
            if (m.c == 2) { menu_hover(-1); continue; }
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
                dock_sync();
                redraw = 1;
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
        if (m.op == WL_WINBEGIN) { ntb_expect = (int)m.a; ntb_new = 0; if (ntb_expect == 0) { ntb = 0; dock_sync(); log_pending = 1; } continue; }
        if (m.op == WL_WIN) {
            if (ntb_new < MAXT) {
                tbs_new[ntb_new].id = m.a & 0x3fffffffu; tbs_new[ntb_new].focused = (m.a >> 31) & 1; tbs_new[ntb_new].minimized = (m.a >> 30) & 1;
                memcpy(tbs_new[ntb_new].title, &m.b, 12); tbs_new[ntb_new].title[12] = 0;
                ntb_new++;
            }
            if (--ntb_expect <= 0) { memcpy(tbs, tbs_new, sizeof tbs); ntb = ntb_new; dock_sync(); log_pending = 1; }
            continue;
        }
        if (m.op == WL_POINTER && (m.c == 0 || m.c == 2)) {              /* motion over the dock: the item under the pointer rises */
            int over = -1, over_start = 0;
            if (m.c == 0) {
                int x = (int)m.a, y = (int)m.b;
                if (y >= DOCK_Y && y < DOCK_Y + DOCK_H) {
                    if (x >= start_x0 && x < start_x1) over_start = 1;
                    for (int j = 0; j < nitems; j++) if (items[j].kind != K_SEP && items[j].present && x >= items[j].x0 && x < items[j].x1) over = j;
                }
            }
            ui_cursor(s, over_start || over >= 0 ? 2 : 0);
            spring_to(&start_hov, over_start ? 1.0f : 0.0f);
            for (int j = 0; j < nitems; j++) spring_to(&items[j].hov, j == over ? 1.0f : 0.0f);
            continue;
        }
        if (m.op == WL_POINTER && m.c == 1) {
            if ((int)m.a >= start_x0 && (int)m.a < start_x1) {
                start_dip.p = 1; spring_to(&start_dip, 0);
                if (mfd >= 0) menu_close();
                else if (ms_now() - closed_ms > 400) menu_open();    /* (a click outside the menu has just closed it) */
                continue;
            }
            for (int j = 0; j < nitems; j++) {
                struct dk *d = &items[j];
                if (d->kind == K_SEP || !d->present || (int)m.a < d->x0 || (int)m.a >= d->x1) continue;
                d->dip.p = 1; spring_to(&d->dip, 0);
                if (d->kind == K_LAUNCH) { int i = find(bar, nbar, d->label); if (i >= 0) launch(bar[i].cmd); }
                else { struct wl_msg r = { WL_RAISE, (uint32_t)d->key, d->focused && !d->minimized ? 1u : 0u, 0, 0 }; (void)!write(s, &r, sizeof r); }
                break;
            }
        }
    }
    return 0;
}
