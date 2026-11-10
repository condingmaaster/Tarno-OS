// SPDX-License-Identifier: GPL-2.0-or-later
// Send a desktop notification ("toast") to thosdesk: a fresh connection whose first messages are WL_TOAST chunks.
//   text = "Title|Body" (up to ~90 chars), kind 0 info, 1 done, 2 warning, 3 error.
#ifndef THOSNOTIFY_H
#define THOSNOTIFY_H
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "thoswl.h"
static void thos_notify(int kind, const char *text) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (s < 0 || connect(s, (void *)&a, 2 + WL_SOCK_LEN) != 0) { if (s >= 0) close(s); return; }
    size_t n = strlen(text);
    if (n > 96) n = 96;
    for (size_t off = 0; off < n || off == 0; off += 12) {
        struct wl_msg m = { WL_TOAST, (uint32_t)(kind & 0xff) | (off + 12 >= n ? 0x100u : 0u), 0, 0, 0 };
        char chunk[12] = { 0 };
        memcpy(chunk, text + off, n - off < 12 ? n - off : 12);
        memcpy(&m.b, chunk, 12);
        if (write(s, &m, sizeof m) != (ssize_t)sizeof m) break;
        if (n == 0) break;
    }
    close(s);
}
#endif
