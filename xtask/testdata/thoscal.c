// SPDX-License-Identifier: GPL-2.0-or-later
// THOS calendar (a legacy-protocol desktop client): the popover above the clock. A glass month view, today in the accent colour,
// arrows (or the keyboard) change the month; the desktop closes it when you click anywhere else.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"
#include "thosui.h"

#define CW 300
#define CH 330
static uint32_t *px, *col, *mask;
static int sock, hover_day = -1, hover_nav = -1, shown_y, shown_m, today_y, today_m, today_d;
static struct spring hov_a;
static const char *mn[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *wd[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
#define CELL 38
#define GX 12
#define GY 98

static int days_in(int y, int m) { static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 1 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : d[m]; }
static int first_wd(int y, int m) {                              /* 0 = Monday */
    struct tm t = { .tm_year = y - 1900, .tm_mon = m, .tm_mday = 1, .tm_hour = 12 };
    mktime(&t);
    return (t.tm_wday + 6) % 7;
}
static void render(void) {
    for (int i = 0; i < CW * CH; i++) col[i] = mask[i] = 0;
    ui_rrect(mask, CW, CH, 0, 0, CW, CH, 20, 0xffffffu);
    ui_rrect(col, CW, CH, 0, 0, CW, CH, 20, 0x39425eu);
    ui_rrect_g(col, CW, CH, 1, 1, CW - 2, CH - 2, 19, 0x212839u, 0x161b28u, 255);
    char title[40]; snprintf(title, sizeof title, "%s %d", mn[shown_m], shown_y);
    ui_text(col, CW, CH, 20, 40, title, UI_BOLD, 19, UI_TEXT);
    for (int k = 0; k < 2; k++) {                                /* previous / next month chips */
        int cx = CW - 78 + k * 36;
        ui_rrect(col, CW, CH, cx, 20, 30, 30, 10, hover_nav == k ? 0x34405fu : 0x1b2130u);
        ui_icon(col, CW, CH, cx + 15, 35, k ? IC_CHEV : 0xf053, 11, hover_nav == k ? UI_TEXT : UI_MUTED);
    }
    for (int i = 0; i < 7; i++) ui_text(col, CW, CH, GX + i * CELL + CELL / 2 - ui_text_width(UI_BOLD, 11, wd[i]) / 2, 84, wd[i], UI_BOLD, 11, i >= 5 ? 0x7a829c : UI_MUTED);
    int first = first_wd(shown_y, shown_m), n = days_in(shown_y, shown_m);
    int pm = shown_m ? shown_m - 1 : 11, py = shown_m ? shown_y : shown_y - 1, pn = days_in(py, pm);
    if (hov_a.p > 0.02f && hover_day >= 0) {
        int r = hover_day / 7, c = hover_day % 7;
        ui_rrect_g(col, CW, CH, GX + c * CELL + 2, GY + r * CELL + 2, CELL - 4, CELL - 4, 12, 0x2e3858u, 0x2e3858u, (unsigned)((hov_a.p > 1 ? 1 : hov_a.p) * 255));
    }
    for (int cell = 0; cell < 42; cell++) {
        int r = cell / 7, c = cell % 7, d = cell - first + 1, other = 0;
        if (d < 1) { d += pn; other = 1; } else if (d > n) { d -= n; other = 1; }
        int cx = GX + c * CELL + CELL / 2, cy = GY + r * CELL + CELL / 2;
        int is_today = !other && shown_y == today_y && shown_m == today_m && d == today_d;
        if (is_today) ui_rrect_g(col, CW, CH, cx - 16, cy - 16, 32, 32, 16, UI_ACCENT_HI, UI_ACCENT_LO, 255);
        char t[8]; snprintf(t, sizeof t, "%d", d);
        ui_text(col, CW, CH, cx - ui_text_width(is_today ? UI_BOLD : UI_REG, 13, t) / 2, cy + 5, t, is_today ? UI_BOLD : UI_REG, 13, is_today ? 0xffffffu : other ? 0x5b6580 : c >= 5 ? 0xb0b7cc : UI_TEXT);
    }
    ui_rrect_ga(col, CW, CH, 2, 2, CW - 4, 60, 18, 0xffffffu, 0xffffffu, 24, 0);
    for (int i = 0; i < CW * CH; i++) {
        unsigned a = (mask[i] & 255) * 244 / 255, r = ((col[i] >> 16) & 255) * 244 / 255, g = ((col[i] >> 8) & 255) * 244 / 255, b = (col[i] & 255) * 244 / 255;
        px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    struct wl_msg c = { WL_COMMIT, 0, 0, CW, CH };
    (void)!write(sock, &c, sizeof c);
}
static void go(int dm) { shown_m += dm; if (shown_m < 0) { shown_m = 11; shown_y--; } if (shown_m > 11) { shown_m = 0; shown_y++; } }
static long long ms_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

int main(void) {
    /* a click on the clock while the calendar is open closes it (the desktop does that) and must not open it again straight away */
    FILE *lf = fopen("/run/thoscal.closed", "r");
    if (lf) { long long t0 = 0; if (fscanf(lf, "%lld", &t0) == 1 && ms_now() - t0 < 600) { fclose(lf); return 0; } fclose(lf); }
    time_t now = time(0);
    struct tm lt; localtime_r(&now, &lt);
    today_y = shown_y = lt.tm_year + 1900; today_m = shown_m = lt.tm_mon; today_d = lt.tm_mday;
    int key = 0x5a00 + (getpid() & 0x1ff);
    int id = shmget(key, CW * CH * 4, IPC_CREAT | 0600);
    px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    col = malloc((size_t)CW * CH * 4); mask = malloc((size_t)CW * CH * 4);
    if (px == (void *)-1 || !col || !mask || gfx_load_font() != 0 || ui_init() != 0) { puts("cal FAIL: shm/font"); return 1; }
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(sock, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("cal FAIL: connect"); return 1; }
    spring_init(&hov_a, 0, SP_HOVER);
    struct wl_msg m = { WL_CREATE, CW, CH, (uint32_t)key, 3 | 8 };                 /* undecorated, closes on an outside click, alpha */
    (void)!write(sock, &m, sizeof m);
    if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("cal FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);
    struct wl_msg pos = { WL_POS, m.b - CW - 12, m.c - 64 - CH - 6, 0, 0 };
    (void)!write(sock, &pos, sizeof pos);
    render();
    printf("cal ready\n");
    fflush(stdout);
    long long clk = mo_now_ms();
    for (;;) {
        int busy = spring_busy(&hov_a);
        int pr = ui_wait(sock, busy);
        long long nowt = mo_now_ms();
        float dt = (float)(nowt - clk) / 1000.0f;
        clk = nowt;
        if (busy) { spring_step(&hov_a, dt); render(); }
        if (pr <= 0) continue;
        if (read(sock, &m, sizeof m) != (ssize_t)sizeof m || m.op == WL_CLOSE) break;
        if (m.op == WL_THEME) { ui_theme_load(); render(); continue; }
        if (m.op != WL_POINTER) continue;
        int x = (int)m.a, y = (int)m.b;
        int nav = y >= 20 && y < 50 ? (x >= CW - 78 && x < CW - 48 ? 0 : (x >= CW - 42 && x < CW - 12 ? 1 : -1)) : -1;
        int day = y >= GY && x >= GX && x < GX + 7 * CELL && y < GY + 6 * CELL ? ((y - GY) / CELL) * 7 + (x - GX) / CELL : -1;
        if (m.c == 1) { if (nav >= 0) go(nav ? 1 : -1); render(); continue; }
        if (m.c == 2) { nav = -1; day = -1; }
        if (nav != hover_nav || day != hover_day) {
            if (day >= 0 && hover_day < 0) spring_init(&hov_a, 0, SP_HOVER);
            hover_nav = nav; hover_day = day;
            spring_to(&hov_a, day >= 0 ? 1.0f : 0.0f);
            render();
        }
    }
    FILE *cf = fopen("/run/thoscal.closed", "w");
    if (cf) { fprintf(cf, "%lld\n", ms_now()); fclose(cf); }
    return 0;
}
