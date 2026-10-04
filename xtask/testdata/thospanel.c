// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop panel: an undecorated bar along the bottom with a "Terminal" launcher button.
#define _GNU_SOURCE
#include <signal.h>
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

#define PW 1024
#define PH 32

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    int key = 0x5000 + getpid();
    int id = shmget(key, PW * PH * 4, IPC_CREAT | 0600);
    uint32_t *px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1 || gfx_load_font() != 0) { puts("panel FAIL: shm/font"); return 1; }
    for (int i = 0; i < PW * PH; i++) px[i] = 0x00282832;
    for (int y = 4; y < PH - 4; y++) for (int x = 8; x < 104; x++) px[y * PW + x] = 0x003a6ea5;
    gfx_str(px, PW, PH, 16, 8, "Terminal", 0x00ffffff, 0xFFFFFFFFu);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(s, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("panel FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, PW, PH, (uint32_t)key, 1 };
    (void)!write(s, &m, sizeof m);
    if (read(s, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("panel FAIL: no CREATED"); return 1; }
    shmctl(id, IPC_RMID, 0);      /* the compositor has attached: the segment lives until both sides unmap it */
    struct wl_msg pos = { WL_POS, 0, m.c - PH, 0, 0 };
    (void)!write(s, &pos, sizeof pos);
    struct wl_msg cm = { WL_COMMIT, 0, 0, PW, PH };
    (void)!write(s, &cm, sizeof cm);
    printf("panel ready\n");
    fflush(stdout);
    while (read(s, &m, sizeof m) == (ssize_t)sizeof m) {
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
