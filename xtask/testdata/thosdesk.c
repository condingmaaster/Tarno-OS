// SPDX-License-Identifier: GPL-2.0-or-later
// THOS desktop stage 3: a userspace compositor. Owns /dev/fb0, speaks thoswl.h to clients over
// an AF_UNIX socket, keeps a z-ordered window list, composes damaged rectangles into a back
// buffer and blits them, draws a software cursor, raises/focuses on click, drags by the
// titlebar, closes on the red button. Quits on a WL_QUIT message.
#define _GNU_SOURCE
#include <errno.h>
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
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "thoswl.h"
#include "thosgfx.h"

#define MAXWIN 16
#define BORDER 1
#define TITLE 30
#define BTN 20
#define SH 16           /* reach of the soft window shadow */
#define SH_OFF 6        /* the shadow sits a little below the window */
#define CORNER 10       /* window corner radius */

struct bitfield { uint32_t offset, length, msb_right; };
struct var_screeninfo { uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bpp, grayscale;
    struct bitfield red, green, blue, transp; uint32_t rest[24]; };

struct wlsurf;
struct win { int fd, id, x, y, w, h; uint32_t *px; char title[17]; int deco; unsigned hwnd; struct wlsurf *wl; int is_wl; int fade; int alpha; int maxed, rx, ry, rw, rh, cfg_w, cfg_h; int csd, gx, gy, gw, gh, popup, menu, taskbar; };
static int wsfd = -1;
static struct win wins[MAXWIN];          /* [0] = bottom ... [n-1] = top (focused) */
static int nwin, next_id = 1;
static int W, H, pitch;
static uint8_t *fbmap;
static uint32_t *bb;                     /* back buffer, W*H */
static uint32_t rs, gs, bs;
static int mx, my, quit;
static int winlist_dirty;               /* the task list of the panel needs sending */
static int last_icon = -1;
static unsigned long long last_icon_ms;
static int drag, dx0, dy0;                       /* a window move in progress (titlebar, or asked for by the client) */
static int rs_id, rs_dir, rs_x0, rs_y0, rs_w0, rs_h0;   /* an edge/corner resize in progress */

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
/* everything a window can touch on screen: its frame plus the soft shadow around it */
static struct rect outer_of(struct win *w) {
    struct rect f = frame_of(w);
    if (!w->deco) return f;
    return (struct rect){ f.x0 - SH, f.y0 - SH + SH_OFF, f.x1 + SH, f.y1 + SH + SH_OFF };
}
static struct rect close_btn(struct win *w) { if (!w->deco) return rect_xywh(0, 0, 0, 0); return rect_xywh(w->x + w->w + BORDER - BTN - 6, w->y - TITLE + (TITLE - BTN) / 2, BTN, BTN); }

/* ---- theme: everything below is integer-only and table driven, to stay cheap on old hardware ---- */
static uint8_t cov_tab[CORNER][CORNER];   /* coverage of a rounded corner, [row][col] counted from the outer corner */
static uint8_t sh_tab[SH];                /* shadow opacity by distance from the window */
static uint32_t *wall;                    /* the wallpaper, rendered once */
static int hover_win = -1;                /* id of the window whose close button the pointer is on */

/* channel-order independent: scale all three colour bytes by k/256 */
static inline uint32_t scale_px(uint32_t p, unsigned k) {
    uint32_t rb = ((p & 0x00FF00FFu) * k >> 8) & 0x00FF00FFu, g = ((p & 0x0000FF00u) * k >> 8) & 0x0000FF00u;
    return rb | g;
}
/* d + (s - d) * a/256, a in 0..256 */
static inline uint32_t lerp_px(uint32_t d, uint32_t s, unsigned a) {
    uint32_t rb = (((d & 0x00FF00FFu) * (256 - a) + (s & 0x00FF00FFu) * a) >> 8) & 0x00FF00FFu;
    uint32_t g = (((d & 0x0000FF00u) * (256 - a) + (s & 0x0000FF00u) * a) >> 8) & 0x0000FF00u;
    return rb | g;
}

static uint32_t rgb(int r, int g, int b);
static void theme_init(void) {
    for (int j = 0; j < CORNER; j++)
        for (int i = 0; i < CORNER; i++) {
            int in_s = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    int X = (i * 4 + sx) * 2 + 1 - CORNER * 8, Y = (j * 4 + sy) * 2 + 1 - CORNER * 8;   /* sample centre relative to the circle centre, in 1/8 px */
                    if (X * X + Y * Y <= CORNER * 8 * CORNER * 8) in_s++;
                }
            cov_tab[j][i] = (uint8_t)(in_s * 255 / 16);
        }
    for (int d = 0; d < SH; d++) sh_tab[d] = (uint8_t)(150 * (SH - d) * (SH - d) / (SH * SH));
    wall = malloc((size_t)W * H * 4);
    if (!wall) return;
    int cx = W * 70 / 100, cy = H * 30 / 100;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int t = (x * 256 / W + y * 256 / H) / 2;                    /* diagonal gradient */
            int dx = x - cx, dy = y - cy, glow = 46 - (dx * dx + dy * dy) / 5000;
            if (glow < 0) glow = 0;
            int r = 16 + t * 26 / 256 + glow * 6 / 10, g = 22 + t * 44 / 256 + glow * 9 / 10, b = 54 + t * 84 / 256 + glow;
            int n = ((x * 7 + y * 13) & 3) - 1;                          /* a little dither against banding */
            wall[(size_t)y * W + x] = rgb(r + n < 0 ? 0 : r + n, g + n < 0 ? 0 : g + n, b + n < 0 ? 0 : b + n > 255 ? 255 : b + n);
        }
}

static void fill(struct rect r, uint32_t c) {
    r = clip(r);
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) bb[(size_t)y * W + x] = c;
}

static const char *cursor_art[19] = {
    "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ", "#....#      ", "#.....#     ", "#......#    ", "#.......#   ",
    "#........#  ", "#.....#####", "#..#..#     ", "#.# #..#    ", "##  #..#    ", "#    #..#   ", "     #..#   ", "      #..#  ", "      #..#  ", "       ##   ",
};
#define CUR_W 12
#define CUR_H 19
static void draw_cursor(void) {
    for (int j = 0; j < CUR_H; j++) {
        if (my + j >= H) break;
        for (int i = 0; i < CUR_W; i++) {
            char c = cursor_art[j][i];
            if (c == ' ' || mx + i >= W) continue;
            *(uint32_t *)(fbmap + (size_t)(my + j) * pitch + (size_t)(mx + i) * 4) = c == '#' ? rgb(0, 0, 0) : rgb(255, 255, 255);
        }
    }
}

static struct rect shadow_src(struct win *w) { struct rect f = frame_of(w); return (struct rect){ f.x0 + 2, f.y0 + SH_OFF, f.x1 - 2, f.y1 + SH_OFF - 2 }; }
static void draw_shadow(struct win *w, int top, struct rect r) {
    struct rect o = isect(outer_of(w), r), f = frame_of(w), S = shadow_src(w);
    if (empty(o)) return;
    unsigned gain = top ? 256 : 160;
    for (int y = o.y0; y < o.y1; y++) {
        int dy = S.y0 - y > 0 ? S.y0 - y : (y - (S.y1 - 1) > 0 ? y - (S.y1 - 1) : 0);
        for (int x = o.x0; x < o.x1; x++) {
            if (x >= f.x0 + CORNER && x < f.x1 - CORNER && y >= f.y0 + CORNER && y < f.y1 - CORNER) { x = f.x1 - CORNER - 1; continue; }   /* hidden by the window itself */
            int dx = S.x0 - x > 0 ? S.x0 - x : (x - (S.x1 - 1) > 0 ? x - (S.x1 - 1) : 0);
            int d = dx > dy ? dx + dy / 2 : dy + dx / 2;
            if (d >= SH) continue;
            unsigned a = sh_tab[d] * gain >> 8;
            uint32_t *p = &bb[(size_t)y * W + x];
            *p = scale_px(*p, 256 - a);
        }
    }
}

/* one titlebar row's colour: a vertical gradient, brighter when focused */
static uint32_t title_color(int row, int top) {
    int r0 = top ? 72 : 52, g0 = top ? 78 : 55, b0 = top ? 98 : 68, r1 = top ? 48 : 40, g1 = top ? 52 : 42, b1 = top ? 68 : 52;
    return rgb(r0 + (r1 - r0) * row / TITLE, g0 + (g1 - g0) * row / TITLE, b0 + (b1 - b0) * row / TITLE);
}

/* ---- desktop icons: "Label|command" lines in /desktop.conf, painted over the wallpaper ---- */
static unsigned long long now_ms(void);
static void compose(struct rect r);
#define MAXICON 16
#define ICON_X 20
#define ICON_Y 20
#define ICON_STEP 96
static struct { char label[24]; char cmd[160]; } icons[MAXICON];
static int nicons;
static const char *icons_path(void) { return access("/tmp/desktop.conf", F_OK) == 0 ? "/tmp/desktop.conf" : "/desktop.conf"; }
static time_t icons_mtime;
static unsigned long long icons_checked;
static struct rect icon_cell(int j) { return rect_xywh(ICON_X, ICON_Y + j * ICON_STEP, 80, 86); }
static int icon_at(int x, int y) {
    for (int j = 0; j < nicons; j++) if (in(icon_cell(j), x, y)) return j;
    return -1;
}
static void put_glyph_str(const char *t, int gx, int gy, uint32_t c, unsigned op, struct rect r);
static void draw_icons(struct rect r) {
    static const uint8_t pal[6][3] = { { 70, 130, 200 }, { 200, 100, 80 }, { 90, 170, 110 }, { 170, 120, 200 }, { 210, 160, 70 }, { 80, 170, 180 } };
    for (int j = 0; j < nicons; j++) {
        struct rect cell = icon_cell(j);
        if (empty(isect(cell, r))) continue;
        unsigned h = 0;
        for (const char *p = icons[j].label; *p; p++) h = h * 31 + (unsigned char)*p;
        const uint8_t *pc = pal[h % 6];
        int bx = cell.x0 + 12, by = cell.y0, bs = 56;
        for (int y = 0; y < bs; y++)
            for (int x = 0; x < bs; x++) {
                int px = bx + x, py = by + y;
                if (!in(r, px, py)) continue;
                int cx = x < 8 ? 8 - x : (bs - 1 - x < 8 ? 8 - (bs - 1 - x) : 0), cy = y < 8 ? 8 - y : (bs - 1 - y < 8 ? 8 - (bs - 1 - y) : 0);
                if (cx * cx + cy * cy > 64) continue;
                int k = 40 + (bs - y) * 60 / bs;           /* lighter at the top */
                uint32_t col = rgb(pc[0] * k / 100 > 255 ? 255 : pc[0] * k / 100, pc[1] * k / 100 > 255 ? 255 : pc[1] * k / 100, pc[2] * k / 100 > 255 ? 255 : pc[2] * k / 100);
                bb[(size_t)py * W + px] = col;
            }
        /* the initial, scaled 3x */
        unsigned char ch = (unsigned char)icons[j].label[0];
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        for (int gy = 0; gy < 16 && gfx_font; gy++)
            for (int gx = 0; gx < 8; gx++)
                if (gfx_font[ch * 16 + gy] & (0x80 >> gx))
                    for (int sy = 0; sy < 3; sy++)
                        for (int sx = 0; sx < 3; sx++) {
                            int px = bx + 16 + gx * 3 + sx, py = by + 4 + gy * 3 + sy;
                            if (in(r, px, py)) bb[(size_t)py * W + px] = rgb(255, 255, 255);
                        }
        char lab[11];
        snprintf(lab, sizeof lab, "%s", icons[j].label);
        int lx = cell.x0 + (80 - 8 * (int)strlen(lab)) / 2, ly = cell.y0 + 62;
        put_glyph_str(lab, lx + 1, ly + 1, rgb(0, 0, 0), 256, r);
        put_glyph_str(lab, lx, ly, rgb(255, 255, 255), 256, r);
    }
}
static void load_icons(void) {
    nicons = 0;
    FILE *f = fopen(icons_path(), "r");
    for (char line[200]; f && fgets(line, sizeof line, f) && nicons < MAXICON;) {
        char *bar = strchr(line, '|');
        if (!bar) continue;
        *bar = 0;
        bar[1 + strcspn(bar + 1, "\n")] = 0;
        snprintf(icons[nicons].label, sizeof icons[0].label, "%s", line);
        snprintf(icons[nicons].cmd, sizeof icons[0].cmd, "%s", bar + 1);
        nicons++;
    }
    if (f) fclose(f);
}
/* re-read the file when the start menu has rewritten it (checked twice a second at most) */
static void icons_poll(void) {
    unsigned long long t = now_ms();
    if (t - icons_checked < 500) return;
    icons_checked = t;
    struct stat st;
    time_t m = stat(icons_path(), &st) == 0 ? st.st_mtime + (st.st_size << 20) : 0;
    if (m == icons_mtime) return;
    icons_mtime = m;
    load_icons();
    compose(rect_xywh(0, 0, 120, H));
}
static void launch_cmd(const char *cmd) {
    if (fork() != 0) return;
    if (!*cmd) execl("/thosterm", "thosterm", "-l", (char *)0);
    setenv("XDG_RUNTIME_DIR", "/run", 1); setenv("HOME", "/tmp", 1); setenv("LC_ALL", "C.UTF-8", 1);
    setenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf", 1); setenv("GDK_BACKEND", "wayland", 1);
    execl("/busybox", "sh", "-c", cmd, (char *)0);
    _exit(127);
}

static void put_glyph_str(const char *t, int gx, int gy, uint32_t c, unsigned op, struct rect r) {
    for (int k = 0; t[k] && gfx_font; k++, gx += 8)
        for (int j = 0; j < 16; j++) {
            unsigned char row = gfx_font[(unsigned char)t[k] * 16 + j];
            for (int i = 0; i < 8; i++)
                if ((row & (0x80 >> i)) && in(r, gx + i, gy + j)) { uint32_t *p = &bb[(size_t)(gy + j) * W + gx + i]; *p = lerp_px(*p, c, op); }
        }
}

static void draw_window(struct win *w, int top, struct rect r) {
    struct rect f = frame_of(w), v = isect(f, r);
    if (empty(v)) return;
    unsigned op = 256 - (unsigned)w->fade * 256 / 256;      /* fade-in: 0..256 */
    if (op > 256) op = 256;
    uint32_t border = top ? rgb(96, 104, 130) : rgb(70, 74, 90);
    for (int y = v.y0; y < v.y1; y++) {
        int row = y - f.y0, rows = f.y1 - f.y0;
        uint32_t *d = &bb[(size_t)y * W];
        /* fast path: an opaque content row away from the rounded corners is a copy plus two border pixels */
        if (!w->alpha && op == 256 && y >= w->y && y < w->y + w->h && (!w->deco || (row >= CORNER && rows - 1 - row >= CORNER))) {
            int cx0 = w->x > v.x0 ? w->x : v.x0, cx1 = w->x + w->w < v.x1 ? w->x + w->w : v.x1;
            if (cx1 > cx0) memcpy(&d[cx0], &w->px[(size_t)(y - w->y) * w->w + (cx0 - w->x)], (size_t)(cx1 - cx0) * 4);
            if (w->deco) {
                if (f.x0 >= v.x0 && f.x0 < v.x1) d[f.x0] = border;
                if (f.x1 - 1 >= v.x0 && f.x1 - 1 < v.x1) d[f.x1 - 1] = border;
            }
            continue;
        }
        for (int x = v.x0; x < v.x1; x++) {
            uint32_t src;
            int cov = 256;
            if (w->deco) {
                if (y < w->y) src = title_color(y - f.y0, top);
                else if (y >= w->y + w->h || x < w->x || x >= w->x + w->w) src = border;
                else goto content;
                if (y == f.y0 || x == f.x0 || x == f.x1 - 1 || y == f.y1 - 1) src = border;
                if (y == f.y0 + 1 && x > f.x0 && x < f.x1 - 1) src = lerp_px(src, rgb(255, 255, 255), 28);   /* a thin highlight on the top edge */
                goto shape;
            }
        content:
            {
                uint32_t c = w->px[(size_t)(y - w->y) * w->w + (x - w->x)];
                if (w->alpha) {
                    unsigned a = c >> 24;                     /* premultiplied: result = src + dst * (1 - a) */
                    uint32_t dd = scale_px(d[x], 256 - a - (a >> 7));
                    c = (c & 0x00FFFFFFu) + dd;
                    src = c;
                    if (a == 0) { continue; }
                } else src = c;
            }
        shape:
            if (w->deco) {
                int cx = x - f.x0 < CORNER ? x - f.x0 : (f.x1 - 1 - x < CORNER ? f.x1 - 1 - x : -1);
                int cy = row < CORNER ? row : (rows - 1 - row < CORNER ? rows - 1 - row : -1);
                if (cx >= 0 && cy >= 0) cov = cov_tab[cy][cx] + (cov_tab[cy][cx] >> 7);
            }
            unsigned a = (unsigned)cov * op >> 8;
            d[x] = a >= 256 ? src : (a == 0 ? d[x] : lerp_px(d[x], src, a));
        }
    }
    if (!w->deco) return;
    /* title, centred, with a 1 px shadow for legibility */
    int len = (int)strlen(w->title), gx = w->x + (w->w - 8 * len) / 2, gy = w->y - TITLE + (TITLE - 16) / 2;
    if (gx < w->x + 8) gx = w->x + 8;
    put_glyph_str(w->title, gx + 1, gy + 1, rgb(10, 12, 18), op, r);
    put_glyph_str(w->title, gx, gy, top ? rgb(238, 240, 247) : rgb(150, 155, 170), op, r);
    /* close button: a red disc, with a cross when the pointer is on it */
    struct rect cb = close_btn(w);
    int ccx = (cb.x0 + cb.x1) / 2, ccy = (cb.y0 + cb.y1) / 2, rad = 7, hot = hover_win == w->id;
    uint32_t disc = hot ? rgb(255, 98, 104) : (top ? rgb(226, 72, 78) : rgb(120, 84, 88));
    struct rect cv = isect(cb, r);
    for (int y = cv.y0; y < cv.y1; y++)
        for (int x = cv.x0; x < cv.x1; x++) {
            int dx = x - ccx, dy = y - ccy, d2 = dx * dx + dy * dy;
            int in2 = (rad - 1) * (rad - 1), out2 = (rad + 1) * (rad + 1);
            if (d2 >= out2) continue;
            unsigned a = d2 <= in2 ? 256 : (unsigned)(out2 - d2) * 256 / (unsigned)(out2 - in2);
            uint32_t c = disc;
            if (hot && (dx == dy || dx == -dy) && dx >= -3 && dx <= 3) c = rgb(255, 255, 255);
            uint32_t *p = &bb[(size_t)y * W + x];
            *p = lerp_px(*p, c, a * op >> 8);
        }
}

/* Repaint everything inside `r` (screen coordinates), bottom to top, then blit it. */
static void compose(struct rect r) {
    r = clip(r);
    if (empty(r)) return;
    for (int y = r.y0; y < r.y1; y++) memcpy(&bb[(size_t)y * W + r.x0], &wall[(size_t)y * W + r.x0], (size_t)(r.x1 - r.x0) * 4);
    draw_icons(r);
    for (int i = 0; i < nwin; i++) {
        struct win *w = &wins[i];
        int top = i == nwin - 1;
        if (w->deco) draw_shadow(w, top, r);
        draw_window(w, top, r);
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
    winlist_dirty = 1;
}

static void ws_send(unsigned kind, unsigned hwnd, unsigned a, unsigned b, unsigned c) {
    unsigned rec[8] = { kind, hwnd, a, b, c, 0, 0, 0 };
    (void)!write(wsfd, rec, sizeof rec);
}

static void remove_win(int i) {
    struct rect f = outer_of(&wins[i]);
    if (wins[i].hwnd) ws_send(14, wins[i].hwnd, 0, 0, 0);
    else if (wins[i].is_wl) free(wins[i].px);
    else { close(wins[i].fd); if (wins[i].px) shmdt(wins[i].px); }
    memmove(&wins[i], &wins[i + 1], (size_t)(nwin - i - 1) * sizeof wins[0]);
    nwin--;
    compose(f);
    winlist_dirty = 1;
}

static int in(struct rect r, int x, int y) {
    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1; }

static struct rect uni(struct rect a, struct rect b);
/* The panel (a client created with CREATE d bit 2) learns about the open windows: BEGIN(count), then one WIN per window
   (a = id | focused << 31, b..d = 12 title characters), in creation order. */
static int is_listed(struct win *w) { return w->deco && !w->menu && !w->popup && !w->taskbar; }
static void send_winlist(void) {
    int p = -1, n = 0, top = -1, idx[MAXWIN];
    for (int i = 0; i < nwin; i++) { if (wins[i].taskbar) p = i; if (is_listed(&wins[i])) { idx[n++] = i; top = i; } }
    if (p < 0) return;
    for (int a = 1; a < n; a++)                       /* creation order (ids ascend), so the buttons keep their places */
        for (int b = a; b > 0 && wins[idx[b]].id < wins[idx[b - 1]].id; b--) { int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t; }
    struct wl_msg bm = { WL_WINBEGIN, (uint32_t)n, 0, 0, 0 };
    (void)!write(wins[p].fd, &bm, sizeof bm);
    for (int k = 0; k < n; k++) {
        int i = idx[k];
        struct wl_msg m = { WL_WIN, (uint32_t)wins[i].id | (i == top ? 0x80000000u : 0), 0, 0, 0 };
        char t[12] = { 0 };
        memcpy(t, wins[i].title, 12);
        memcpy(&m.b, t, 12);
        (void)!write(wins[p].fd, &m, sizeof m);
    }
}
static void raise_win(int i);
static struct win *win_by_id(int id);
struct wlsurf;
static void wls_focus_keyboard(struct wlsurf *s);
static void on_client(int i) {
    struct wl_msg m;
    ssize_t n = read(wins[i].fd, &m, sizeof m);
    if (n != (ssize_t)sizeof m) { remove_win(i); return; }
    if (m.op == WL_TITLE) {
        memcpy(wins[i].title, &m.a, 16);
        wins[i].title[16] = 0;
        winlist_dirty = 1;
        compose(frame_of(&wins[i]));
    } else if (m.op == WL_RAISE) {                      /* the panel's task list asks for a window */
        struct win *t = win_by_id((int)m.a);
        if (t && is_listed(t)) {
            int ot = wins[nwin - 1].id, ti = (int)(t - wins);
            if (ti != nwin - 1) raise_win(ti);
            struct win *o = win_by_id(ot);
            if (o) compose(outer_of(o));
            compose(outer_of(&wins[nwin - 1]));
            if (wins[nwin - 1].wl) wls_focus_keyboard(wins[nwin - 1].wl);
            winlist_dirty = 1;
        }
    } else if (m.op == WL_POS) {
        struct rect old = outer_of(&wins[i]);
        wins[i].x = (int)m.a; wins[i].y = (int)m.b;
        compose(uni(old, outer_of(&wins[i])));
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
        w->fade = 200;
        compose(outer_of(w));
    } else if (e[0] == 2) {
        for (int i = 0; i < nwin; i++) if (wins[i].hwnd == e[1]) { struct rect f = outer_of(&wins[i]); memmove(&wins[i], &wins[i + 1], (size_t)(nwin - i - 1) * sizeof wins[0]); nwin--; compose(f); break; }
    } else if (e[0] == 3) {
        for (int i = 0; i < nwin; i++) if (wins[i].hwnd == e[1]) {
            struct win *w = &wins[i];
            compose(isect(content_of(w), rect_xywh(w->x + e[2], w->y + e[3], e[4], e[5])));
            break;
        }
    }
}

#include "thoswls.h"

/* new windows fade in over a handful of frames */
static unsigned long long last_anim;
static int anim_pending(void) { for (int i = 0; i < nwin; i++) if (wins[i].fade) return 1; return 0; }
static void anim_tick(void) {
    unsigned long long t = now_ms();
    if (t - last_anim < 16) return;
    last_anim = t;
    for (int i = 0; i < nwin; i++)
        if (wins[i].fade) { wins[i].fade -= wins[i].fade > 36 ? 36 : wins[i].fade; compose(outer_of(&wins[i])); }
}

/* ---- resizing and maximizing (Wayland windows: the client is asked for the new size) ---- */
static unsigned long long rs_last, click_ms;
static int click_id;
static int resize_dir(struct win *w, int px, int py) {
    if (!w->wl || w->maxed) return 0;
    if (w->csd) return 0;                          /* the client draws its own borders and asks for resizes itself */
    if (!w->deco) return 0;
    struct rect f = frame_of(w);
    const int m = 6;
    int right = px >= f.x1 - m && px < f.x1 + m && py >= w->y && py < f.y1 + m;
    int bottom = py >= f.y1 - m && py < f.y1 + m && px >= f.x0 && px < f.x1 + m;
    return (right ? 1 : 0) | (bottom ? 2 : 0);
}
static int work_bottom(void) {          /* a panel along the bottom edge is not covered by maximized windows */
    int b = H;
    for (int i = 0; i < nwin; i++) if (!wins[i].deco && !wins[i].is_wl && !wins[i].menu && wins[i].y > H / 2 && wins[i].y < b) b = wins[i].y;
    return b;
}
static void toggle_maximize(struct win *w) {
    struct rect old = outer_of(w);
    if (!w->maxed) {
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
        int nw, nh;
        if (w->csd) { w->x = -w->gx; w->y = -w->gy; nw = W; nh = work_bottom(); }   /* the client's own frame fills the screen */
        else { w->x = BORDER; w->y = TITLE; nw = W - 2 * BORDER; nh = work_bottom() - TITLE - BORDER; }
        w->maxed = 1; w->cfg_w = nw; w->cfg_h = nh;
        wls_configure(w->wl, nw, nh, 1);
    } else {
        w->maxed = 0; w->x = w->rx; w->y = w->ry; w->cfg_w = w->rw; w->cfg_h = w->rh;
        wls_configure(w->wl, w->rw, w->rh, 0);
    }
    compose(uni(old, outer_of(w)));
}

/* requests from Wayland clients that draw their own decorations (xdg_toplevel.move / resize / set_maximized) */
static void desk_begin_move(int winid) {
    struct win *w = win_by_id(winid);
    if (!w || w->maxed) return;
    drag = 1; dx0 = mx - w->x; dy0 = my - w->y;
}
static void desk_begin_resize(int winid, int edges) {
    struct win *w = win_by_id(winid);
    int d = ((edges & 8) ? 1 : 0) | ((edges & 2) ? 2 : 0);          /* right / bottom edges only */
    if (!w || !d || w->maxed) return;
    rs_id = w->id; rs_dir = d; rs_x0 = mx; rs_y0 = my; rs_w0 = w->csd ? w->gw : w->w; rs_h0 = w->csd ? w->gh : w->h; rs_last = 0;
    w->cfg_w = rs_w0; w->cfg_h = rs_h0;
}
static void desk_set_maximized(int winid, int on) {
    struct win *w = win_by_id(winid);
    if (w && w->wl && (!w->maxed) == (on != 0)) toggle_maximize(w);
}
/* the close button under the pointer lights up */
static void update_hover(void) {
    int nh = -1;
    for (int i = nwin - 1; i >= 0; i--) {
        if (!in(frame_of(&wins[i]), mx, my)) continue;
        if (in(close_btn(&wins[i]), mx, my)) nh = wins[i].id;
        break;
    }
    if (nh == hover_win) return;
    int old = hover_win;
    hover_win = nh;
    struct win *a = old >= 0 ? win_by_id(old) : 0, *b = nh >= 0 ? win_by_id(nh) : 0;
    if (a) compose(close_btn(a));
    if (b) compose(close_btn(b));
}


/* ---- screenshot (Print key): the composed screen as a PNG in /tmp (stored deflate blocks, no compression) ---- */
static uint32_t crc_tab[256];
static uint32_t crc_upd(uint32_t c, const unsigned char *p, size_t n) {
    if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; crc_tab[i] = k; }
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}
static void be32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static void png_chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len) {
    unsigned char h[8]; be32(h, len); memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    uint32_t c = crc_upd(0xFFFFFFFFu, (const unsigned char *)type, 4);
    if (len) { fwrite(data, 1, len, f); c = crc_upd(c, data, len); }
    unsigned char t[4]; be32(t, c ^ 0xFFFFFFFFu); fwrite(t, 1, 4, f);
}
static void save_screenshot(void) {
    static int n;
    char path[64];
    snprintf(path, sizeof path, "/tmp/shot-%d.png", ++n);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("desk: screenshot failed (%s)\n", path); fflush(stdout); return; }
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13]; be32(ihdr, (uint32_t)W); be32(ihdr + 4, (uint32_t)H); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    size_t raw = (size_t)H * ((size_t)W * 3 + 1);
    size_t nblk = (raw + 65534) / 65535, idat_len = 2 + raw + 5 * nblk + 4;
    unsigned char hdr[8]; be32(hdr, (uint32_t)idat_len); memcpy(hdr + 4, "IDAT", 4);
    fwrite(hdr, 1, 8, f);
    uint32_t c = crc_upd(0xFFFFFFFFu, (const unsigned char *)"IDAT", 4);
    static unsigned char blk[65535 + 5];
    unsigned char z[2] = { 0x78, 0x01 };
    fwrite(z, 1, 2, f); c = crc_upd(c, z, 2);
    uint32_t a = 1, b = 0;
    size_t fill = 0, done = 0, row = 0, col = 0; int stage = 0;               /* stage 0: filter byte, then 3 bytes per pixel */
    while (done < raw) {
        size_t want = raw - done < 65535 ? raw - done : 65535;
        unsigned char *d = blk + 5;
        for (fill = 0; fill < want; fill++) {
            unsigned char v;
            if (col == 0) { v = 0; col = 1; stage = 0; }
            else {
                uint32_t p = bb[row * W + (col - 1) / 3];
                int ch = (int)((col - 1) % 3);
                v = ch == 0 ? (unsigned char)(p >> rs) : ch == 1 ? (unsigned char)(p >> gs) : (unsigned char)(p >> bs);
                col++;
                if (col - 1 == (size_t)W * 3) { col = 0; row++; }
            }
            d[fill] = v;
            a = (a + v) % 65521; b = (b + a) % 65521;
        }
        (void)stage;
        blk[0] = done + want == raw ? 1 : 0;
        blk[1] = (unsigned char)want; blk[2] = (unsigned char)(want >> 8); blk[3] = (unsigned char)~want; blk[4] = (unsigned char)(~want >> 8);
        fwrite(blk, 1, want + 5, f); c = crc_upd(c, blk, want + 5);
        done += want;
    }
    unsigned char ad[4]; be32(ad, (b << 16) | a);
    fwrite(ad, 1, 4, f); c = crc_upd(c, ad, 4);
    unsigned char t[4]; be32(t, c ^ 0xFFFFFFFFu); fwrite(t, 1, 4, f);
    png_chunk(f, "IEND", 0, 0);
    fclose(f);
    printf("desk: screenshot %s\n", path);
    fflush(stdout);
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
    if (first == (ssize_t)sizeof m && m.op == WL_QUIT) { puts("desk: quit requested"); fflush(stdout); quit = 1; close(c); return; }
    if (first != (ssize_t)sizeof m || nwin >= MAXWIN || m.op != WL_CREATE || m.a == 0 || m.b == 0 || m.a > 2000 || m.b > 2000) { close(c); return; }
    int id = shmget(m.c, (size_t)m.a * m.b * 4, 0);
    void *p = id >= 0 ? shmat(id, 0, 0) : (void *)-1;
    if (p == (void *)-1) { close(c); return; }
    struct win *w = &wins[nwin++];
    int k = next_id - 1;
    *w = (struct win){ c, next_id++, 100 + 80 * (k % 6), 80 + 60 * (k % 6), (int)m.a, (int)m.b, p };
    w->deco = !(m.d & 1);
    w->menu = (m.d & 2) != 0;
    w->taskbar = (m.d & 4) != 0;   /* the panel: told about the open windows */      /* a start-menu surface: closed by a click anywhere outside it */
    if (!w->deco) { w->x = 0; w->y = 0; }
    struct wl_msg r = { WL_CREATED, (uint32_t)w->id, (uint32_t)W, (uint32_t)H, 0 };
    (void)!write(c, &r, sizeof r);
    printf("desk: window %d created %dx%d at %d,%d\n", w->id, w->w, w->h, w->x, w->y);
    fflush(stdout);
    if (w->deco) w->fade = 200;
    compose(outer_of(w));
    winlist_dirty = 1;
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && !strcmp(argv[argc - 1], "-v");
    int session = (argc > 1 && !strcmp(argv[1], "-s")) || strstr(argv[0], "desktop") != 0;
    gfx_load_font();
    signal(SIGCHLD, SIG_IGN);
    int fb = open("/dev/fb0", O_RDWR), mice = open("/dev/input/mice", O_RDONLY);
    int kbd = open("/dev/input/kbd", O_RDONLY);
    int wheel = open("/dev/input/wheel", O_RDONLY);
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
    theme_init();
    if (!wall) { puts("desk FAIL: wallpaper"); return 1; }

    int ls = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    memcpy(a.sun_path, WL_SOCK_NAME, WL_SOCK_LEN);
    if (bind(ls, (void *)&a, 2 + WL_SOCK_LEN) != 0 || listen(ls, 8) != 0) { puts("desk FAIL: socket"); return 1; }

    mkdir("/run", 0755);
    wls_init();
    mx = W / 2; my = H / 2;
    compose(rect_xywh(0, 0, W, H));
    printf("desk: ready %dx%d\n", W, H);
    fflush(stdout);
    if (session) {      /* the usual session: panel, then a terminal */
        if (fork() == 0) { execl("/thospanel", "thospanel", (char *)0); _exit(127); }
        usleep(500000);
        if (fork() == 0) { execl("/thosterm", "thosterm", verbose ? "-l" : (char *)0, (char *)0); _exit(127); }
    }

    int prev_btn = 0;
    int idle = 0;
    unsigned long long last_act = now_ms();
    for (; idle < 1200 && !quit;) {
        struct pollfd p[4 + MAXWIN + 2 + WLS_MAXCL] = { { kbd, POLLIN, 0 }, { ls, POLLIN, 0 }, { mice, POLLIN, 0 }, { wsfd, POLLIN, 0 } };
        for (int i = 0; i < nwin; i++) p[4 + i] = (struct pollfd){ wins[i].hwnd ? -1 : wins[i].fd, POLLIN, 0 };
        int wbase = 4 + nwin, nwl = wls_pollfds(&p[wbase]);
        p[wbase + nwl] = (struct pollfd){ wheel, POLLIN, 0 };
        int pr = poll(p, wbase + nwl + 1, (wls_have_callbacks() || anim_pending()) ? 16 : 100);
        if (pr < 0) { printf("desk: poll %d\n", pr); break; }
        wls_tick();
        anim_tick();
        icons_poll();
        if (winlist_dirty) { winlist_dirty = 0; send_winlist(); }
        if (pr == 0) { if (now_ms() - last_act > 120000) idle = 1200; continue; }
        last_act = now_ms();
        if (nwl) wls_dispatch_poll(&p[wbase], nwl);
        if (wheel >= 0 && (p[wbase + nwl].revents & POLLIN)) {
            signed char z;
            if (read(wheel, &z, 1) == 1) wls_wheel(z);
        }
        if (p[0].revents == 0 && p[1].revents == 0 && p[2].revents == 0 && p[3].revents == 0) {
            int any = 0;
            for (int i = 0; i < nwin && 4 + i < wbase; i++) if (p[4 + i].revents) any = 1;
            if (!any) continue;
        }
        if (p[0].revents & POLLIN) {
            unsigned char ev[8];
            int t = nwin - 1;
            while (t >= 0 && !wins[t].deco && !wins[t].is_wl) t--;
            ssize_t kn = read(kbd, ev, 8);
            if (kn == 8 && ev[0] && ev[1] == 0x46) { save_screenshot(); kn = 0; }                      /* Print: screenshot to /tmp */
            if (kn == 8 && ev[0] && ev[1] == 0x14 && (ev[2] & 0x11) && (ev[2] & 0x44)) { quit = 1; kn = 0; }   /* Ctrl+Alt+Q: leave the desktop */
            if (kn == 8 && ev[0] && (ev[2] & 0x44) && !(ev[2] & 0x40)) {
                /* Left-Alt shortcuts (AltGr is not Alt): Enter = new terminal, Tab = cycle, F4 = close */
                if (ev[1] == 0x28) {
                    if (fork() == 0) { execl("/thosterm", "thosterm", "-l", (char *)0); _exit(127); }
                } else if (ev[1] == 0x2B && nwin > 1) {
                    int b = 0;
                    while (b < nwin && !wins[b].deco && !wins[b].is_wl) b++;
                    if (b < nwin && b != nwin - 1) { int ot = wins[nwin - 1].id; raise_win(b); struct win *o = win_by_id(ot); if (o) compose(outer_of(o)); compose(outer_of(&wins[nwin - 1])); }
                } else if (ev[1] == 0x52 && t >= 0 && wins[t].wl) {      /* Alt+Up: maximize / restore */
                    toggle_maximize(&wins[t]);
                } else if (ev[1] == 0x3D && t >= 0) {
                    struct wl_msg c = { WL_CLOSE, 0, 0, 0, 0 };
                    if (wins[t].wl) wls_request_close(wins[t].wl);
                    else {
                        if (wins[t].hwnd) ws_send(12, wins[t].hwnd, 0, 0, 0);
                        else (void)!write(wins[t].fd, &c, sizeof c);
                        remove_win(t);
                    }
                }
                kn = 0;
            }
            if (kn == 8 && t >= 0) {
                unsigned cp = ev[3] ? ev[3] : (ev[4] | (ev[5] << 8) | (ev[6] << 16));   /* ASCII, else the code point */
                struct wl_msg k = { WL_KEY, ev[1], ev[0], cp, ev[2] };
                if (wins[t].wl) wls_key(wins[t].wl, ev[1], ev[0], ev[2]);
                else if (wins[t].hwnd) { if (ev[0] && ev[3]) ws_send(11, wins[t].hwnd, ev[3], 0, 0); }
                else (void)!write(wins[t].fd, &k, sizeof k);
                if (!wins[t].wl && wls_kfocus) wls_focus_keyboard(0);
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
            struct rect oldc = rect_xywh(mx, my, CUR_W, CUR_H);
            mx += ddx; my -= ddy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx > W - 1) mx = W - 1;
            if (my > H - 1) my = H - 1;
            int btn = pkt[0] & 1;
            int started_resize = 0;
            if (btn && !prev_btn) {
                for (int i = nwin - 1; i >= 0 && !started_resize; i--) {
                    struct win *w = &wins[i];
                    int d = resize_dir(w, mx, my);
                    if (!d) { if (in(frame_of(w), mx, my)) break; continue; }
                    int ot = wins[nwin - 1].id;
                    if (i != nwin - 1) raise_win(i);
                    w = &wins[nwin - 1];
                    if (ot != w->id) { struct win *o = win_by_id(ot); if (o) compose(outer_of(o)); }
                    rs_id = w->id; rs_dir = d; rs_x0 = mx; rs_y0 = my; rs_w0 = w->w; rs_h0 = w->h; rs_last = 0;
                    w->cfg_w = w->w; w->cfg_h = w->h;
                    compose(outer_of(w));
                    started_resize = 1;
                }
            }
            if (btn && !prev_btn) {                     /* a click outside the start menu closes it */
                for (int i = nwin - 1; i >= 0; i--)
                    if (wins[i].menu && !in(frame_of(&wins[i]), mx, my)) {
                        struct wl_msg c = { WL_CLOSE, 0, 0, 0, 0 };
                        (void)!write(wins[i].fd, &c, sizeof c);
                        remove_win(i);
                    }
                int over = 0;
                for (int i = 0; i < nwin; i++) if (in(frame_of(&wins[i]), mx, my)) over = 1;
                int ic = over ? -1 : icon_at(mx, my);
                unsigned long long tn = now_ms();
                if (ic >= 0) {
                    if (ic == last_icon && tn - last_icon_ms < 500) { launch_cmd(icons[ic].cmd); last_icon = -1; }
                    else { last_icon = ic; last_icon_ms = tn; }
                }
            }
            if (btn && !prev_btn) {                     /* a click outside the open menus closes them */
                struct wlsurf *hit = 0;
                for (int i = nwin - 1; i >= 0; i--) if (in(frame_of(&wins[i]), mx, my)) { hit = wins[i].popup ? wins[i].wl : 0; break; }
                wls_dismiss_popups(hit);
            }
            if (btn && !prev_btn && !started_resize) {
                for (int i = nwin - 1; i >= 0; i--) {
                    struct win *w = &wins[i];
                    if (!in(frame_of(w), mx, my)) continue;
                    if (in(close_btn(w), mx, my)) {
                        struct wl_msg c = { WL_CLOSE, 0, 0, 0, 0 };
                        if (w->wl) wls_request_close(w->wl);
                        else {
                            if (w->hwnd) ws_send(12, w->hwnd, 0, 0, 0);
                            else (void)!write(w->fd, &c, sizeof c);
                            remove_win(i);
                        }
                    } else {
                        int ot = wins[nwin - 1].id;
                        if (i != nwin - 1) raise_win(i);
                        w = &wins[nwin - 1];
                        if (ot != w->id) { struct win *o = win_by_id(ot); if (o) compose(outer_of(o)); }   /* its titlebar loses the focus colour */
                        if (in(content_of(w), mx, my) && w->hwnd) ws_send(10, w->hwnd, 0x201, mx - w->x, my - w->y);
                        else if (in(content_of(w), mx, my)) {
                            struct wl_msg pm = { WL_POINTER, (uint32_t)(mx - w->x), (uint32_t)(my - w->y), 1, 0 };
                            (void)!write(w->fd, &pm, sizeof pm);
                        }
                        if (w->deco && my < w->y) {
                            unsigned long long tnow = now_ms();
                            if (w->wl && click_id == w->id && tnow - click_ms < 400) { toggle_maximize(w); click_id = 0; }
                            else { click_id = w->id; click_ms = tnow; if (!w->maxed) { drag = 1; dx0 = mx - w->x; dy0 = my - w->y; } }
                        }
                        compose(outer_of(w));
                    }
                    break;
                }
            }
            if (!btn) drag = 0;
            if (rs_id) {
                struct win *w = win_by_id(rs_id);
                if (!w || !w->wl) rs_id = 0;
                else {
                    int nw = rs_w0 + ((rs_dir & 1) ? mx - rs_x0 : 0), nh = rs_h0 + ((rs_dir & 2) ? my - rs_y0 : 0);
                    if (nw < 160) nw = 160;
                    if (nh < 100) nh = 100;
                    unsigned long long tnow = now_ms();
                    if ((nw != w->cfg_w || nh != w->cfg_h) && (tnow - rs_last >= 30 || !btn)) {
                        w->cfg_w = nw; w->cfg_h = nh; rs_last = tnow;
                        wls_configure(w->wl, nw, nh, 0);
                    }
                    if (!btn) rs_id = 0;
                }
            }
            int pb = prev_btn;
            prev_btn = btn;
            if (!rs_id && !started_resize) wls_pointer(mx, my, btn, pb);
            update_hover();
            if (drag && nwin) {
                struct win *w = &wins[nwin - 1];
                struct rect old = outer_of(w);
                w->x = mx - dx0; w->y = my - dy0;
                compose(uni(old, outer_of(w)));
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
