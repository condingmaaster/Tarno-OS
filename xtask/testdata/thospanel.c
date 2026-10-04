// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop panel: an undecorated bar along the bottom with a "Terminal" launcher button.
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
static int PW = 1024;
#define MAXB 8
static struct { char label[24]; char cmd[160]; int x0, x1; } btn[MAXB];
static int nbtn;

/* a rounded button, painted into the bar */
static void draw_button(uint32_t *px, int x0, int x1, const char *label) {
    for (int y = 4; y < PH - 4; y++)
        for (int x = x0; x < x1; x++) {
            int cx = x - x0 < 5 ? 5 - (x - x0) : (x1 - 1 - x < 5 ? 5 - (x1 - 1 - x) : 0), cy = y - 4 < 5 ? 5 - (y - 4) : (PH - 5 - y < 5 ? 5 - (PH - 5 - y) : 0);
            if (cx * cx + cy * cy > 25) continue;
            px[y * PW + x] = y < PH / 2 ? 0x00467bb8 : 0x00386aa5;
        }
    gfx_str(px, PW, PH, x0 + 10, 8, label, 0x00ffffff, 0xFFFFFFFFu);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(s, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("panel FAIL: connect"); return 1; }
    struct wl_msg m = { WL_HELLO, 0, 0, 0, 0 };
    (void)!write(s, &m, sizeof m);
    if (read(s, &m, sizeof m) == (ssize_t)sizeof m && m.op == WL_CREATED && m.b >= 320 && m.b <= 4096) PW = (int)m.b;   /* span the screen */
    int key = 0x5000 + getpid();
    int id = shmget(key, PW * PH * 4, IPC_CREAT | 0600);
    uint32_t *px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0) { puts("panel FAIL: shm/font"); return 1; }
    for (int i = 0; i < PW * PH; i++) px[i] = 0x00282832;
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);          /* Central Europe, no zone database needed */
    strcpy(btn[0].label, "Terminal"); strcpy(btn[0].cmd, "");
    nbtn = 1;
    FILE *cf = fopen("/panel.conf", "r");                    /* optional: one "Label|command" per line */
    for (char line[200]; cf && fgets(line, sizeof line, cf) && nbtn < MAXB;) {
        char *bar = strchr(line, '|');
        if (!bar) continue;
        *bar = 0; line[strcspn(bar + 1, "\n")] = 0;
        char *cmd = bar + 1; cmd[strcspn(cmd, "\n")] = 0;
        snprintf(btn[nbtn].label, sizeof btn[nbtn].label, "%s", line);
        snprintf(btn[nbtn].cmd, sizeof btn[nbtn].cmd, "%s", cmd);
        nbtn++;
    }
    if (cf) fclose(cf);
    for (int i = 0, x = 8; i < nbtn; i++) {
        btn[i].x0 = x; btn[i].x1 = x + 20 + 8 * (int)strlen(btn[i].label);
        draw_button(px, btn[i].x0, btn[i].x1, btn[i].label);
        x = btn[i].x1 + 8;
    }
    m = (struct wl_msg){ WL_CREATE, PW, PH, (uint32_t)key, 1 };
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
        /* clock: HH:MM (UTC) at the right edge, redrawn when the minute changes */
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
        struct pollfd pf = { s, POLLIN, 0 };
        if (poll(&pf, 1, 15000) <= 0) continue;
        if (read(s, &m, sizeof m) != (ssize_t)sizeof m) break;
        if (m.op == WL_CLOSE) break;
        if (m.op == WL_POINTER) {
            for (int i = 0; i < nbtn; i++) {
                if ((int)m.a < btn[i].x0 || (int)m.a >= btn[i].x1) continue;
                if (fork() == 0) {
                    close(s);
                    if (i == 0) execl("/thosterm", "thosterm", "-l", (char *)0);
                    setenv("XDG_RUNTIME_DIR", "/run", 1); setenv("HOME", "/tmp", 1); setenv("LC_ALL", "C.UTF-8", 1);
                    setenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf", 1); setenv("GDK_BACKEND", "wayland", 1);
                    execl("/busybox", "sh", "-c", btn[i].cmd, (char *)0);
                    _exit(127);
                }
                break;
            }
        }
    }
    return 0;
}
