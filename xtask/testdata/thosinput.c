// SPDX-License-Identifier: GPL-2.0-or-later
// thosinput: synthetic mouse and keyboard input through /dev/inject (root), for remote control over SSH and for tests.
//   thosinput move X Y            put the pointer at screen position X,Y (absolute)
//   thosinput click [X Y] [right] click (at X,Y if given)
//   thosinput drag X1 Y1 X2 Y2    press at X1,Y1, move, release at X2,Y2
//   thosinput scroll N            wheel (positive up)
//   thosinput key ctrl+l          one key (names: enter esc tab backspace space up down left right home end pgup pgdn del f1..f12, letters, digits; prefixes ctrl+ alt+ shift+)
//   thosinput type "text"         type text (German keyboard layout)
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

static int fd;
static void put(const char *fmt, int a, int b) { char l[64]; int n = snprintf(l, sizeof l, fmt, a, b); if (write(fd, l, n) != n) { perror("thosinput: write /dev/inject"); exit(1); } }
/* absolute: first drive the pointer into the top-left corner, wait until the desktop has consumed (and clamped) that, then move to X,Y.
   The desktop adds up the packets it finds waiting before it clamps to the screen, so a slow machine would cancel the reset if it were not waited for. */
static void move_to(int x, int y) { put("m %d %d\n", -4000, -4000); usleep(700000); put("m %d %d\n", x, y); usleep(200000); }
static void click(int x, int y, int btn) { if (x >= 0) move_to(x, y); usleep(60000); put("b %d\n", btn, 0); usleep(60000); put("b %d\n", 0, 0); }
static void tap(int mods, int code) { put("k %d %d\n", mods, code); usleep(40000); put("u\n", 0, 0); usleep(30000); }

/* HID usage for a character on the German layout: returns usage, sets *mods (shift 0x02, altgr 0x40) */
static int hid_char(int c, int *mods) {
    *mods = 0;
    if (c >= 'a' && c <= 'z') { if (c == 'z') return 0x1c; if (c == 'y') return 0x1d; return 0x04 + (c - 'a'); }
    if (c >= 'A' && c <= 'Z') { int code = hid_char(tolower(c), mods); *mods = 0x02; return code; }
    if (c >= '1' && c <= '9') return 0x1e + (c - '1');
    if (c == '0') return 0x27;
    switch (c) {
    case ' ': return 0x2c; case '\n': return 0x28; case '\t': return 0x2b;
    case '.': return 0x37; case ',': return 0x36; case '-': return 0x38; case '_': *mods = 0x02; return 0x38;
    case ':': *mods = 0x02; return 0x37; case ';': *mods = 0x02; return 0x36;
    case '/': *mods = 0x02; return 0x24; case '?': *mods = 0x02; return 0x2d; case '!': *mods = 0x02; return 0x1e;
    case '"': *mods = 0x02; return 0x1f; case '(': *mods = 0x02; return 0x25; case ')': *mods = 0x02; return 0x26;
    case '=': *mods = 0x02; return 0x27; case '&': *mods = 0x02; return 0x23; case '+': return 0x30; case '#': return 0x32;
    case '@': *mods = 0x40; return 0x14; case '\'': *mods = 0x02; return 0x32;
    }
    return 0;
}
static int named(const char *n) {
    static const struct { const char *n; int c; } t[] = { {"enter",0x28},{"return",0x28},{"esc",0x29},{"tab",0x2b},{"backspace",0x2a},{"space",0x2c},{"right",0x4f},{"left",0x50},{"down",0x51},{"up",0x52},
        {"home",0x4a},{"end",0x4d},{"pgup",0x4b},{"pgdn",0x4e},{"del",0x4c},{"delete",0x4c},{"f1",0x3a},{"f2",0x3b},{"f3",0x3c},{"f4",0x3d},{"f5",0x3e},{"f6",0x3f},{"f7",0x40},{"f8",0x41},{"f9",0x42},{"f10",0x43},{"f11",0x44},{"f12",0x45} };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) if (!strcmp(n, t[i].n)) return t[i].c;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fputs("usage: thosinput move|click|drag|scroll|key|type ...\n", stderr); return 2; }
    fd = open("/dev/inject", O_WRONLY);
    if (fd < 0) { fputs("thosinput: cannot open /dev/inject (root only)\n", stderr); return 1; }
    const char *c = argv[1];
    if (!strcmp(c, "move") && argc >= 4) move_to(atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(c, "click")) {
        int x = -1, y = -1, btn = 1;
        if (argc >= 4 && isdigit((unsigned char)argv[2][0])) { x = atoi(argv[2]); y = atoi(argv[3]); }
        if (!strcmp(argv[argc - 1], "right")) btn = 2;
        click(x, y, btn);
    } else if (!strcmp(c, "drag") && argc >= 6) {
        move_to(atoi(argv[2]), atoi(argv[3])); usleep(60000); put("b %d\n", 1, 0); usleep(80000);
        put("m %d %d\n", (atoi(argv[4]) - atoi(argv[2])) / 2, (atoi(argv[5]) - atoi(argv[3])) / 2); usleep(60000);
        put("m %d %d\n", atoi(argv[4]) - atoi(argv[2]) - (atoi(argv[4]) - atoi(argv[2])) / 2, atoi(argv[5]) - atoi(argv[3]) - (atoi(argv[5]) - atoi(argv[3])) / 2); usleep(80000);
        put("b %d\n", 0, 0);
    } else if (!strcmp(c, "scroll") && argc >= 3) put("w %d\n", atoi(argv[2]), 0);
    else if (!strcmp(c, "key") && argc >= 3) {
        int mods = 0; char *k = argv[2];
        for (;;) {
            if (!strncmp(k, "ctrl+", 5)) { mods |= 0x01; k += 5; } else if (!strncmp(k, "shift+", 6)) { mods |= 0x02; k += 6; } else if (!strncmp(k, "alt+", 4)) { mods |= 0x04; k += 4; } else break;
        }
        int code = named(k), m2 = 0;
        if (!code && k[0] && !k[1]) { code = hid_char((unsigned char)k[0], &m2); mods |= m2; }
        if (!code) { fprintf(stderr, "thosinput: unknown key %s\n", argv[2]); return 1; }
        tap(mods, code);
    } else if (!strcmp(c, "type") && argc >= 3) {
        for (const char *p = argv[2]; *p; p++) {
            int mods, code = hid_char((unsigned char)*p, &mods);
            if (!code) continue;
            tap(mods, code);
        }
    } else { fputs("thosinput: bad arguments\n", stderr); return 2; }
    return 0;
}
