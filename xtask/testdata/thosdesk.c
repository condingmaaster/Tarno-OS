// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop stage 3: a userspace compositor. Owns /dev/fb0, speaks thoswl.h to clients over
// an AF_UNIX socket, keeps a z-ordered window list, composes damaged rectangles into a back
// buffer and blits them, draws a software cursor, raises/focuses on click, drags by the
// titlebar, closes on the red button. Quits on a WL_QUIT message.
#define _GNU_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"

#define MAXWIN 16
#define BORDER 4
#define TITLE 24
#define BTN 16

struct bitfield { uint32_t offset, length, msb_right; };
struct var_screeninfo { uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bpp, grayscale;
    struct bitfield red, green, blue, transp; uint32_t rest[24]; };

struct win { int fd, id, x, y, w, h; uint32_t *px; char title[17]; int deco; unsigned hwnd; };
static int wsfd = -1;
static struct win wins[MAXWIN];          /* [0] = bottom ... [n-1] = top (focused) */
static int nwin, next_id = 1;
static int W, H, pitch;
static uint8_t *fbmap;
static uint32_t *bb;                     /* back buffer, W*H */
static uint32_t rs, gs, bs;
static int mx, my, quit;

static uint32_t rgb(int r, int g, int b) { return ((uint32_t)r << rs) | ((uint32_t)g << gs) | ((uint32_t)b << bs); }

struct rect { int x0, y0, x1, y1; };     /* half-open */
static int in(struct rect r, int x, int y);
static struct rect rect_xywh(int x, int y, int w, int h) { return (struct rect){ x, y, x + w, y + h }; }
static struct rect clip(struct rect r) {
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > W) r.x1 = W;
    if (r.y1 > H) r.y1 = H;
    return r;
}
static struct rect isect(struct rect a, struct rect b) {
    struct rect r = { a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1 };
    return r;
}
static int empty(struct rect r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }
static struct rect frame_of(struct win *w) { if (!w->deco) return rect_xywh(w->x, w->y, w->w, w->h); return rect_xywh(w->x - BORDER, w->y - TITLE, w->w + 2 * BORDER, w->h + TITLE + BORDER); }
static struct rect content_of(struct win *w) { return rect_xywh(w->x, w->y, w->w, w->h); }
static struct rect close_btn(struct win *w) { if (!w->deco) return rect_xywh(0, 0, 0, 0); return rect_xywh(w->x + w->w + BORDER - BTN - 4, w->y - TITLE + 4, BTN, BTN); }

static void fill(struct rect r, uint32_t c) {
    r = clip(r);
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) bb[(size_t)y * W + x] = c;
}

static void draw_cursor(void) {
    struct rect r = clip(rect_xywh(mx, my, 10, 14));
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) {
            int edge = x == mx || y == my || x == mx + 9 || y == my + 13;
            *(uint32_t *)(fbmap + (size_t)y * pitch + (size_t)x * 4) = edge ? rgb(0, 0, 0) : rgb(255, 255, 255);
        }
}

/* Repaint everything inside `r` (screen coordinates), bottom to top, then blit it. */
static void compose(struct rect r) {
    r = clip(r);
    if (empty(r)) return;
    /* wallpaper: a soft vertical gradient */
    for (int y = r.y0; y < r.y1; y++) {
        uint32_t c = rgb(30 + 14 * y / H, 50 + 20 * y / H, 90 + 22 * y / H);
        for (int x = r.x0; x < r.x1; x++) bb[(size_t)y * W + x] = c;
    }
    for (int i = 0; i < nwin; i++) {
        struct win *w = &wins[i];
        int top = i == nwin - 1;
        struct rect f = isect(frame_of(w), r);
        if (!empty(f)) {
            if (w->deco) fill(f, top ? rgb(40, 120, 220) : rgb(100, 100, 110));
            struct rect cb = isect(close_btn(w), r);
            if (!empty(cb)) fill(cb, rgb(210, 50, 50));
            /* title text, clipped to the damage rectangle */
            for (int k = 0; w->title[k] && gfx_font; k++) {
                int gx = w->x + 4 + 8 * k, gy = w->y - TITLE + 4;
                for (int j = 0; j < 16; j++) {
                    unsigned char row = gfx_font[(unsigned char)w->title[k] * 16 + j];
                    for (int i = 0; i < 8; i++)
                        if ((row & (0x80 >> i)) && in(r, gx + i, gy + j)) bb[(size_t)(gy + j) * W + gx + i] = rgb(255, 255, 255);
                }
            }
        }
        struct rect c = isect(content_of(w), r);
        for (int y = c.y0; y < c.y1; y++)
            memcpy(&bb[(size_t)y * W + c.x0], &w->px[(size_t)(y - w->y) * w->w + (c.x0 - w->x)], (size_t)(c.x1 - c.x0) * 4);
    }
    for (int y = r.y0; y < r.y1; y++)
        memcpy(fbmap + (size_t)y * pitch + (size_t)r.x0 * 4, &bb[(size_t)y * W + r.x0], (size_t)(r.x1 - r.x0) * 4);
    draw_cursor();
}

static struct rect uni(struct rect a, struct rect b) {
    struct rect r = { a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1 };
    return r;
}

static void raise_win(int i) {
    struct win t = wins[i];
    memmove(&wins[i], &wins[i + 1], (size_t)(nwin - i - 1) * sizeof t);
    wins[nwin - 1] = t;
}

static void ws_send(unsigned kind, unsigned hwnd, unsigned a, unsigned b, unsigned c) {
    unsigned rec[8] = { kind, hwnd, a, b, c, 0, 0, 0 };
    (void)!write(wsfd, rec, sizeof rec);
}

static void remove_win(int i) {
    struct rect f = frame_of(&wins[i]);
    if (wins[i].hwnd) ws_send(14, wins[i].hwnd, 0, 0, 0);
    else { close(wins[i].fd); if (wins[i].px) shmdt(wins[i].px); }
    memmove(&wins[i], &wins[i + 1], (size_t)(nwin - i - 1) * sizeof wins[0]);
    nwin--;
    compose(f);
}

static int in(struct rect r, int x, int y) {
    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

static struct rect uni(struct rect a, struct rect b);
static void on_client(int i) {
    struct wl_msg m;
    ssize_t n = read(wins[i].fd, &m, sizeof m);
    if (n != (ssize_t)sizeof m) { remove_win(i); return; }
    if (m.op == WL_TITLE) {
        memcpy(wins[i].title, &m.a, 16);
        wins[i].title[16] = 0;
        compose(frame_of(&wins[i]));
    } else if (m.op == WL_POS) {
        struct rect old = frame_of(&wins[i]);
        wins[i].x = (int)m.a; wins[i].y = (int)m.b;
        compose(uni(old, frame_of(&wins[i])));
    } else if (m.op == WL_COMMIT) {
        struct win *w = &wins[i];
        compose(isect(content_of(w), rect_xywh(w->x + m.a, w->y + m.b, m.c, m.d)));
    }
}

static void on_winsys(unsigned *e) {
    if (e[0] == 1 && nwin < MAXWIN) {
        long va = syscall(16 /* SYS_ioctl: glibc's ioctl() returns int and would cut the address */, wsfd, 0x7701, (unsigned long)e[1]);
        if (va <= 0) return;
        struct win *w = &wins[nwin++];
        int k = next_id - 1;
        *w = (struct win){ -1, next_id++, 100 + 80 * (k % 6), 80 + 60 * (k % 6), (int)e[4], (int)e[5], (uint32_t *)va };
        w->deco = 1; w->hwnd = e[1];
        memcpy(w->title, "win32", 6);
        printf("desk: win32 window %u created %dx%d\n", e[1], w->w, w->h);
        fflush(stdout);
        compose(frame_of(w));
    } else if (e[0] == 2) {
        for (int i = 0; i < nwin; i++) if (wins[i].hwnd == e[1]) { struct rect f = frame_of(&wins[i]); memmove(&wins[i], &wins[i + 1], (size_t)(nwin - i - 1) * sizeof wins[0]); nwin--; compose(f); break; }
    } else if (e[0] == 3) {
        for (int i = 0; i < nwin; i++) if (wins[i].hwnd == e[1]) {
            struct win *w = &wins[i];
            compose(isect(content_of(w), rect_xywh(w->x + e[2], w->y + e[3], e[4], e[5])));
            break;
        }
    }
}

static void on_new_client(int ls) {
    int c = accept(ls, 0, 0);
    if (c < 0) return;
    struct wl_msg m;
    ssize_t first = read(c, &m, sizeof m);
    if (first == (ssize_t)sizeof m && m.op == WL_HELLO) {
        struct wl_msg h = { WL_CREATED, 0, (uint32_t)W, (uint32_t)H, 0 };
        (void)!write(c, &h, sizeof h);
        first = read(c, &m, sizeof m);
    }
    if (first == (ssize_t)sizeof m && m.op == WL_QUIT) { quit = 1; close(c); return; }
    if (first != (ssize_t)sizeof m || nwin >= MAXWIN || m.op != WL_CREATE || m.a == 0 || m.b == 0 || m.a > 2000 || m.b > 2000) { close(c); return; }
    int id = shmget(m.c, (size_t)m.a * m.b * 4, 0);
    void *p = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (p == (void *)-1) { close(c); return; }
    struct win *w = &wins[nwin++];
    int k = next_id - 1;
    *w = (struct win){ c, next_id++, 100 + 80 * (k % 6), 80 + 60 * (k % 6), (int)m.a, (int)m.b, p };
    w->deco = !(m.d & 1);
    if (!w->deco) { w->x = 0; w->y = 0; }
    struct wl_msg r = { WL_CREATED, (uint32_t)w->id, (uint32_t)W, (uint32_t)H, 0 };
    (void)!write(c, &r, sizeof r);
    printf("desk: window %d created %dx%d at %d,%d\n", w->id, w->w, w->h, w->x, w->y);
    fflush(stdout);
    compose(frame_of(w));
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && !strcmp(argv[argc - 1], "-v");
    int session = (argc > 1 && !strcmp(argv[1], "-s")) || strstr(argv[0], "desktop") != 0;
    gfx_load_font();
    signal(SIGCHLD, SIG_IGN);
    int fb = open("/dev/fb0", O_RDWR), mice = open("/dev/input/mice", O_RDONLY);
    int kbd = open("/dev/input/kbd", O_RDONLY);
    wsfd = open("/dev/winsys", O_RDWR);
    if (fb < 0 || mice < 0) { printf("desk FAIL: open fb=%d mice=%d\n", fb, mice); return 1; }
    struct var_screeninfo v; memset(&v, 0, sizeof v);
    char fix[80];
    if (ioctl(fb, 0x4600, &v) != 0 || v.bpp != 32 || ioctl(fb, 0x4602, fix) != 0) { puts("desk FAIL: fbdev ioctls"); return 1; }
    W = v.xres; H = v.yres; rs = v.red.offset; gs = v.green.offset; bs = v.blue.offset;
    pitch = *(uint32_t *)(fix + 48);
    fbmap = mmap(0, (size_t)pitch * H, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    bb = calloc((size_t)W * H, 4);
    if (fbmap == MAP_FAILED || !bb) { puts("desk FAIL: mmap"); return 1; }

    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (bind(ls, (void *)&a, 2 + WL_SOCK_LEN) != 0 || listen(ls, 8) != 0) { puts("desk FAIL: socket"); return 1; }

    mx = W / 2; my = H / 2;
    compose(rect_xywh(0, 0, W, H));
    printf("desk: ready %dx%d\n", W, H);
    fflush(stdout);
    if (session) {      /* the usual session: panel, then a terminal */
        if (fork() == 0) { execl("/thospanel", "thospanel", (char *)0); _exit(127); }
        usleep(500000);
        if (fork() == 0) { execl("/thosterm", "thosterm", verbose ? "-l" : (char *)0, (char *)0); _exit(127); }
    }

    int drag = 0, dx0 = 0, dy0 = 0, prev_btn = 0;
    int idle = 0;
    for (; idle < 1200 && !quit;) {
        struct pollfd p[4 + MAXWIN] = { { kbd, POLLIN, 0 }, { ls, POLLIN, 0 }, { mice, POLLIN, 0 }, { wsfd, POLLIN, 0 } };
        for (int i = 0; i < nwin; i++) p[4 + i] = (struct pollfd){ wins[i].hwnd ? -1 : wins[i].fd, POLLIN, 0 };
        int pr = poll(p, 4 + nwin, 100);
        if (pr < 0) { printf("desk: poll %d\n", pr); break; }
        if (pr == 0) { idle++; continue; }
        idle = 0;
        if (p[0].revents & POLLIN) {
            unsigned char ev[8];
            int t = nwin - 1;
            while (t >= 0 && !wins[t].deco) t--;
            ssize_t kn = read(kbd, ev, 8);
            if (kn == 8 && ev[0] && ev[1] == 0x14 && (ev[2] & 0x11) && (ev[2] & 0x44)) { quit = 1; kn = 0; }   /* Ctrl+Alt+Q: leave the desktop */
            if (kn == 8 && ev[0] && (ev[2] & 0x44) && !(ev[2] & 0x40)) {
                /* Left-Alt shortcuts (AltGr is not Alt): Enter = new terminal, Tab = cycle, F4 = close */
                if (ev[1] == 0x28) {
                    if (fork() == 0) { execl("/thosterm", "thosterm", "-l", (char *)0); _exit(127); }
                } else if (ev[1] == 0x2B && nwin > 1) {
                    int b = 0;
                    while (b < nwin && !wins[b].deco) b++;
                    if (b < nwin && b != nwin - 1) { raise_win(b); compose(frame_of(&wins[nwin - 1])); }
                } else if (ev[1] == 0x3D && t >= 0) {
                    struct wl_msg c = { WL_CLOSE, 0, 0, 0, 0 };
                    if (wins[t].hwnd) ws_send(12, wins[t].hwnd, 0, 0, 0);
                    else (void)!write(wins[t].fd, &c, sizeof c);
                    remove_win(t);
                }
                kn = 0;
            }
            if (kn == 8 && t >= 0) {
                unsigned cp = ev[3] ? ev[3] : (ev[4] | (ev[5] << 8) | (ev[6] << 16));   /* ASCII, else the code point */
                struct wl_msg k = { WL_KEY, ev[1], ev[0], cp, ev[2] };
                if (wins[t].hwnd) { if (ev[0] && ev[3]) ws_send(11, wins[t].hwnd, ev[3], 0, 0); }
                else (void)!write(wins[t].fd, &k, sizeof k);
            }
        }
        if (p[3].revents & POLLIN) {
            unsigned e[8];
            if (read(wsfd, e, sizeof e) == (ssize_t)sizeof e) on_winsys(e);
        }
        for (int i = nwin - 1; i >= 0; i--)
            if (!wins[i].hwnd && (p[4 + i].revents & (POLLIN | POLLHUP | POLLERR))) on_client(i);
        if (p[1].revents & POLLIN) on_new_client(ls);
        if (p[2].revents & POLLIN) {
            unsigned char pkt[3];
            { ssize_t rn = read(mice, pkt, 3); if (rn != 3) { printf("desk: mouse read %zd\n", rn); break; } }
            int ddx = pkt[1] - ((pkt[0] & 0x10) ? 256 : 0), ddy = pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);
            struct rect oldc = rect_xywh(mx, my, 10, 14);
            mx += ddx; my -= ddy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx > W - 1) mx = W - 1;
            if (my > H - 1) my = H - 1;
            int btn = pkt[0] & 1;
            if (btn && !prev_btn) {
                for (int i = nwin - 1; i >= 0; i--) {
                    struct win *w = &wins[i];
                    if (!in(frame_of(w), mx, my)) continue;
                    if (in(close_btn(w), mx, my)) {
                        struct wl_msg c = { WL_CLOSE, 0, 0, 0, 0 };
                        if (w->hwnd) ws_send(12, w->hwnd, 0, 0, 0);
                        else (void)!write(w->fd, &c, sizeof c);
                        remove_win(i);
                    } else {
                        if (i != nwin - 1) raise_win(i);
                        w = &wins[nwin - 1];
                        if (in(content_of(w), mx, my) && w->hwnd) ws_send(10, w->hwnd, 0x201, mx - w->x, my - w->y);
                        else if (in(content_of(w), mx, my)) {
                            struct wl_msg pm = { WL_POINTER, (uint32_t)(mx - w->x), (uint32_t)(my - w->y), 1, 0 };
                            (void)!write(w->fd, &pm, sizeof pm);
                        }
                        if (w->deco && my < w->y) { drag = 1; dx0 = mx - w->x; dy0 = my - w->y; }
                        compose(frame_of(w));
                    }
                    break;
                }
            }
            if (!btn) drag = 0;
            prev_btn = btn;
            if (drag && nwin) {
                struct win *w = &wins[nwin - 1];
                struct rect old = frame_of(w);
                w->x = mx - dx0; w->y = my - dy0;
                compose(uni(old, frame_of(w)));
            } else {
                compose(oldc);
            }
            draw_cursor();
        }
    }
    printf("desk: loop end idle=%d quit=%d\n", idle, quit);
    printf("desk ok: %d window(s) at exit\n", nwin);
    return 0;
}
