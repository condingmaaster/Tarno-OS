// SPDX-License-Identifier: GPL-2.0-or-later
// Keyboard test client for thosdesk: a window that shows what is typed. Esc ends it.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"

#define TW 400
#define TH 200

int main(void) {
    int key = 0x5000 + getpid();
    int id = shmget(key, TW * TH * 4, IPC_CREAT | 0600);
    uint32_t *px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0) { puts("text FAIL: shm/font"); return 1; }
    for (int i = 0; i < TW * TH; i++) px[i] = 0x00101010;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(s, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("text FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, TW, TH, (uint32_t)key, 0 };
    (void)!write(s, &m, sizeof m);
    if (read(s, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("text FAIL: no CREATED"); return 1; }
    struct wl_msg t = { WL_TITLE, 0, 0, 0, 0 };
    memcpy(&t.a, "typewriter", 10);
    (void)!write(s, &t, sizeof t);
    int cx = 8, cy = 8;
    struct wl_msg cm = { WL_COMMIT, 0, 0, TW, TH };
    (void)!write(s, &cm, sizeof cm);
    printf("text ready\n");
    fflush(stdout);
    while (read(s, &m, sizeof m) == (ssize_t)sizeof m) {
        if (m.op == WL_CLOSE) break;
        if (m.op != WL_KEY || !m.b) continue;      /* b = 1 on press */
        if (m.a == 0x29) break;                    /* Esc */
        if (m.c == '\n') { cx = 8; cy += 16; }
        else if (m.c >= 32 && m.c < 127 && cx + 8 < TW) {
            gfx_char(px, TW, TH, cx, cy, (unsigned char)m.c, 0x00FFFFFF, 0x00101010);
            cx += 8;
        } else continue;
        struct wl_msg d = { WL_COMMIT, 0, 0, TW, TH };
        (void)!write(s, &d, sizeof d);
    }
    printf("text done\n");
    return 0;
}
