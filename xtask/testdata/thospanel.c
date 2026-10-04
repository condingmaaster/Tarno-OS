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
    for (int y = 4; y < PH - 4; y++) for (int x = 8; x < 104; x++) px[y * PW + x] = 0x003a6ea5;
    gfx_str(px, PW, PH, 16, 8, "Terminal", 0x00ffffff, 0xFFFFFFFFu);
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
        int minute = (int)(now / 60);
        if (minute != shown) {
            shown = minute;
            char txt[8];
            int hh = (int)(now / 3600 % 24), mm = (int)(now / 60 % 60);
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
        if (m.op == WL_POINTER && m.a >= 8 && m.a < 104) {
            if (fork() == 0) {
                close(s);
                execl("/thosterm", "thosterm", "-l", (char *)0);
                _exit(127);
            }
        }
    }
    return 0;
}
