// SPDX-License-Identifier: GPL-2.0-or-later
// Test client for thosdesk: a window filled with one colour. Usage: thoswin R G B W H
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

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "quit")) {
        int q = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un qa = { .sun_family = AF_UNIX };
        memcpy(qa.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
        struct wl_msg m = { WL_QUIT, 0, 0, 0, 0 };
        if (connect(q, (void *)&qa, 2 + WL_SOCK_LEN) != 0) return 1;
        (void)!write(q, &m, sizeof m);
        return 0;
    }
    if (argc < 6) return 2;
    int r = atoi(argv[1]), g = atoi(argv[2]), b = atoi(argv[3]), w = atoi(argv[4]), h = atoi(argv[5]);
    int key = 0x5000 + getpid();
    int id = shmget(key, (size_t)w * h * 4, IPC_CREAT | 0600);
    uint32_t *px = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (px == (void *)-1) { puts("win FAIL: shm"); return 1; }
    // the pixel format is BGRX on this framebuffer; the compositor copies words verbatim, so the
    // client asks for the layout via argv order (R G B as given, packed little-endian as 0x00RRGGBB)
    uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    for (int i = 0; i < w * h; i++) px[i] = c;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (connect(s, (void *)&a, 2 + WL_SOCK_LEN) != 0) { puts("win FAIL: connect"); return 1; }
    struct wl_msg m = { WL_CREATE, (uint32_t)w, (uint32_t)h, (uint32_t)key, 0 };
    (void)!write(s, &m, sizeof m);
    if (read(s, &m, sizeof m) != (ssize_t)sizeof m || m.op != WL_CREATED) { puts("win FAIL: no CREATED"); return 1; }
    struct wl_msg cm = { WL_COMMIT, 0, 0, (uint32_t)w, (uint32_t)h };
    (void)!write(s, &cm, sizeof cm);
    printf("win %u ready\n", m.a);
    fflush(stdout);
    while (read(s, &m, sizeof m) == (ssize_t)sizeof m)
        if (m.op == WL_CLOSE) break;
    return 0;
}
