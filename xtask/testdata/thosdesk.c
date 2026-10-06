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
#include "thosui.h"
#include "thosmotion.h"

#define MAXWIN 16
#define BORDER 1
#define TITLE 38
#define BTN 28
#define SH 34           /* reach of the soft window shadow */
#define SH_OFF 12        /* the shadow sits a little below the window */
#define CORNER 12       /* window corner radius */

struct bitfield { uint32_t offset, length, msb_right; };
struct var_screeninfo { uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bpp, grayscale;
    struct bitfield red, green, blue, transp; uint32_t rest[24]; };

struct rect { int x0, y0, x1, y1; };     /* half-open */
struct wlsurf;
struct win { int fd, id, x, y, w, h; uint32_t *px; char title[17]; int deco; unsigned hwnd; struct wlsurf *wl; int is_wl; int fade; int alpha; int maxed, rx, ry, rw, rh, cfg_w, cfg_h; int csd, gx, gy, gw, gh, popup, menu, taskbar;
    int anim, ready; long long anim_t0; float ax, ay; struct spring sc, foc, clo, mxs; struct tween opq; uint32_t *snap; struct rect snap_r; };
static int wsfd = -1;
static struct win wins[MAXWIN];          /* [0] = bottom ... [n-1] = top (focused) */
static int nwin, next_id = 1;
static int W, H, pitch;
static uint8_t *fbmap;
static uint32_t *bb;                     /* back buffer, W*H */
static uint32_t rs, gs, bs;
static int mx, my, quit;
static int winlist_dirty;               /* the task list of the panel needs sending */
static int hover_client;                 /* id of the legacy window that last got pointer motion */
static int last_icon = -1;
static unsigned long long last_icon_ms;
static int drag, dx0, dy0;                       /* a window move in progress (titlebar, or asked for by the client) */
static int rs_id, rs_dir, rs_x0, rs_y0, rs_w0, rs_h0;   /* an edge/corner resize in progress */

static uint32_t rgb(int r, int g, int b) { return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b; }            /* canonical 0x00RRGGBB, like the clients' pixels */
static int fb_swap;                                                                                               /* the framebuffer wants another channel order */
static inline uint32_t fbpx(uint32_t c) { return ((c >> 16 & 255) << rs) | ((c >> 8 & 255) << gs) | ((c & 255) << bs); }
static void blit_row(uint32_t *dst, const uint32_t *src, int n) {
    if (!fb_swap) { memcpy(dst, src, (size_t)n * 4); return; }
    for (int i = 0; i < n; i++) dst[i] = fbpx(src[i]);
}

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
static struct rect max_btn(struct win *w) { if (!w->deco || !w->wl || w->csd) return rect_xywh(0, 0, 0, 0); return rect_xywh(w->x + w->w + BORDER - 2 * BTN - 12, w->y - TITLE + (TITLE - BTN) / 2, BTN, BTN); }
static struct rect close_btn(struct win *w) { if (!w->deco) return rect_xywh(0, 0, 0, 0); return rect_xywh(w->x + w->w + BORDER - BTN - 8, w->y - TITLE + (TITLE - BTN) / 2, BTN, BTN); }

/* ---- theme: everything below is integer-only and table driven, to stay cheap on old hardware ---- */
static uint8_t cov_tab[CORNER][CORNER];   /* coverage of a rounded corner, [row][col] counted from the outer corner */
static uint8_t sh_tab[SH];                /* shadow opacity by distance from the window */
static uint32_t *wall;                    /* the wallpaper, rendered once */
static int hover_max = -1;                /* id of the window whose maximize button the pointer is on */
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
    for (int d = 0; d < SH; d++) { int t = (SH - d) * 256 / SH; int sm = (t * t * (3 * 256 - 2 * t)) >> 16; sh_tab[d] = (uint8_t)(112 * sm / 256); }          /* smoothstep falloff */
    wall = malloc((size_t)W * H * 4);
    if (!wall) return;
    int cx = W * 72 / 100, cy = H * 28 / 100, dx2 = W * 18 / 100, dy2 = H * 82 / 100;          /* two soft glows over a deep slate ground */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int t = (x * 256 / W + y * 256 / H) / 2;
            int dx = x - cx, dy = y - cy, ex = x - dx2, ey = y - dy2, k1 = W * H / 9 + 1, k2 = W * H / 12 + 1;
            int g1 = (int)(78LL * k1 / (k1 + ((long long)dx * dx + (long long)dy * dy))), g2 = (int)(56LL * k2 / (k2 + ((long long)ex * ex + (long long)ey * ey)));
            if (g1 < 0) g1 = 0;
            if (g2 < 0) g2 = 0;
            int r = 13 + t * 10 / 256 + g1 * 4 / 10 + g2 * 5 / 10, g = 16 + t * 14 / 256 + g1 * 6 / 10 + g2 * 2 / 10, b = 28 + t * 30 / 256 + g1 * 11 / 10 + g2 * 8 / 10;
            int n = ((x * 7 + y * 13) & 3) - 1;                                                  /* a little dither against banding */
            { int d = x - y * 7 / 10 - W * 40 / 100, k = (W / 9) * (W / 9) + 1; int v = (int)(24LL * k / (k + (long long)d * d)); r += v * 5 / 10; g += v * 7 / 10; b += v * 12 / 10; }   /* a soft diagonal light band */
            r += n; g += n; b += n;
            wall[(size_t)y * W + x] = rgb(r < 0 ? 0 : r > 255 ? 255 : r, g < 0 ? 0 : g > 255 ? 255 : g, b < 0 ? 0 : b > 255 ? 255 : b);
        }
}

static void fill(struct rect r, uint32_t c) {
    r = clip(r);
    for (int y = r.y0; y < r.y1; y++)
        for (int x = r.x0; x < r.x1; x++) bb[(size_t)y * W + x] = c;
}

/* the pointer: an anti-aliased arrow, rendered once from a polygon (4x4 supersampling), with a dark outline */
#define CUR_W 18
#define CUR_H 26
static uint8_t cur_fill[CUR_H][CUR_W], cur_out[CUR_H][CUR_W];
static int in_poly(float x, float y) {
    static const float P[7][2] = { { 1.5f, 1.5f }, { 1.5f, 19.5f }, { 5.6f, 15.6f }, { 8.8f, 22.6f }, { 11.9f, 21.2f }, { 8.8f, 14.4f }, { 14.4f, 14.4f } };
    int c = 0;
    for (int i = 0, j = 6; i < 7; j = i++)
        if (((P[i][1] > y) != (P[j][1] > y)) && (x < (P[j][0] - P[i][0]) * (y - P[i][1]) / (P[j][1] - P[i][1]) + P[i][0])) c = !c;
    return c;
}
static float poly_dist(float x, float y) {          /* distance to the outline of the arrow, for the dark rim */
    static const float P[7][2] = { { 1.5f, 1.5f }, { 1.5f, 19.5f }, { 5.6f, 15.6f }, { 8.8f, 22.6f }, { 11.9f, 21.2f }, { 8.8f, 14.4f }, { 14.4f, 14.4f } };
    float best = 1e9f;
    for (int i = 0, j = 6; i < 7; j = i++) {
        float ax = P[j][0], ay = P[j][1], bx = P[i][0], by = P[i][1], dx = bx - ax, dy = by - ay;
        float t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float ex = ax + t * dx - x, ey = ay + t * dy - y, d = ex * ex + ey * ey;
        if (d < best) best = d;
    }
    return sqrtf(best);
}
static void cursor_init(void) {
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            int f = 0, o = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float x = i + (sx + 0.5f) / 4, y = j + (sy + 0.5f) / 4;
                    int inside = in_poly(x, y);
                    if (inside) { f++; o++; }
                    else if (poly_dist(x, y) < 1.15f) o++;
                }
            cur_fill[j][i] = (uint8_t)(f * 255 / 16);
            cur_out[j][i] = (uint8_t)(o * 255 / 16);
        }
}
static void draw_cursor(void) {
    for (int j = 0; j < CUR_H; j++) {
        if (my + j >= H) break;
        for (int i = 0; i < CUR_W; i++) {
            if (mx + i >= W || !cur_out[j][i]) continue;
            uint32_t base = bb[(size_t)(my + j) * W + mx + i];
            uint32_t c = ui_mix(base, 0x0a0c12u, cur_out[j][i] * 215u / 255);
            c = ui_mix(c, 0xffffffu, cur_fill[j][i]);
            *(uint32_t *)(fbmap + (size_t)(my + j) * pitch + (size_t)(mx + i) * 4) = fbpx(c);
        }
    }
}

static struct rect shadow_src(struct win *w) { struct rect f = frame_of(w); return (struct rect){ f.x0 + 2, f.y0 + SH_OFF, f.x1 - 2, f.y1 + SH_OFF - 2 }; }
static void draw_shadow(struct win *w, int top, struct rect r) {
    struct rect o = isect(outer_of(w), r), f = frame_of(w), S = shadow_src(w);
    if (empty(o)) return;
    unsigned gain = 190 + (unsigned)(66.0f * (w->foc.p < 0 ? 0 : w->foc.p > 1 ? 1 : w->foc.p));
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
static uint32_t title_color(int row, unsigned f) {                     /* f: focus 0..255, the titlebar brightens as the window takes focus */
    return ui_mix(rgb(27, 31, 43), ui_mix(0x333b58u, 0x1e2336u, (unsigned)(row * 255 / TITLE)), f);
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
static struct spring icon_hov[MAXICON], icon_sc[MAXICON];     /* hover lift, and a bounce when the program starts */
static int icon_sel = -1, icon_springs_ready;
static void icons_springs_init(void) { if (icon_springs_ready) return; icon_springs_ready = 1; for (int j = 0; j < MAXICON; j++) { spring_init(&icon_hov[j], 0, SP_HOVER); spring_init(&icon_sc[j], 1, SP_DOCK); } }
static int icons_busy(void) { for (int j = 0; j < nicons; j++) if (spring_busy(&icon_hov[j]) || spring_busy(&icon_sc[j])) return 1; return 0; }
static void icons_step(float dt) { for (int j = 0; j < nicons; j++) { spring_step(&icon_hov[j], dt); spring_step(&icon_sc[j], dt); } }
static void draw_icons(struct rect r) {
    icons_springs_init();
    for (int j = 0; j < nicons; j++) {
        struct rect cell = icon_cell(j);
        if (empty(isect(cell, r))) continue;
        ui_set_clip(r.x0, r.y0, r.x1, r.y1);
        float h = icon_hov[j].p < 0 ? 0 : icon_hov[j].p > 1 ? 1 : icon_hov[j].p, sc = icon_sc[j].p;
        int sel = j == icon_sel;
        unsigned glow = (unsigned)(h * 34) + (sel ? 38 : 0);
        if (glow) ui_rrect_g(bb, W, H, cell.x0 + 1, cell.y0 - 8, 78, 96, 14, 0xffffffu, 0xffffffu, glow);      /* a soft light plate behind it */
        int size = (int)(56 * sc + 0.5f), cx = cell.x0 + 40, ty = cell.y0 + (56 - size) / 2 - (int)(3 * h + 0.5f);
        ui_tile(bb, W, H, cx - size / 2, ty, size, icons[j].label);
        char lab[24]; snprintf(lab, sizeof lab, "%.14s", icons[j].label);
        int lw = ui_text_width(UI_REG, 12, lab), lx = cell.x0 + (80 - lw) / 2, ly = cell.y0 + 74;
        if (sel) ui_rrect_g(bb, W, H, lx - 6, ly - 13, lw + 12, 19, 8, UI_ACCENT, UI_ACCENT, 200);
        else ui_text_a(bb, W, H, lx + 1, ly + 1, lab, UI_REG, 12, 0x000000u, 200);
        ui_text(bb, W, H, lx, ly, lab, UI_REG, 12, 0xf1f3fau);
        ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
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
    unsigned op = 256;
    unsigned fz = (unsigned)(255.0f * (w->foc.p < 0 ? 0 : w->foc.p > 1 ? 1 : w->foc.p));
    uint32_t border = ui_mix(rgb(44, 50, 68), rgb(66, 76, 108), fz);
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
                if (y < w->y) src = title_color(y - f.y0, fz);
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
    /* title (left, Lato) and the close button; the clip keeps this inside the repaint rectangle */
    ui_set_clip(v.x0, v.y0, v.x1, v.y1);
    uint32_t *B = bb;
    int tw = w->w - 90;
    char tt[80]; snprintf(tt, sizeof tt, "%s", w->title);
    while (tt[0] && ui_text_width(UI_REG, 13, tt) > tw && strlen(tt) > 3) tt[strlen(tt) - 1] = 0;
    ui_text_a(B, W, H, w->x + 16, w->y - TITLE + 24, tt, fz > 127 ? UI_BOLD : UI_REG, 13, ui_mix(UI_MUTED, UI_TEXT, fz), op);
    struct rect cb = close_btn(w);
    int ccx = (cb.x0 + cb.x1) / 2, ccy = (cb.y0 + cb.y1) / 2, hot = hover_win == w->id;
    float ch = w->clo.p < 0 ? 0 : w->clo.p > 1 ? 1 : w->clo.p;
    (void)hot;
    if (ch > 0.02f) ui_rrect_g(B, W, H, ccx - BTN / 2, ccy - BTN / 2, BTN, BTN, BTN / 2, UI_BAD, UI_BAD, (unsigned)(ch * 255));
    struct rect mb = max_btn(w);
    if (mb.x1 > mb.x0) {
        int mcx = (mb.x0 + mb.x1) / 2, mcy = (mb.y0 + mb.y1) / 2;
        float mh = w->mxs.p < 0 ? 0 : w->mxs.p > 1 ? 1 : w->mxs.p;
        if (mh > 0.02f) ui_rrect_g(B, W, H, mcx - BTN / 2, mcy - BTN / 2, BTN, BTN, BTN / 2, 0x3a4258u, 0x3a4258u, (unsigned)(mh * 255));
        ui_icon(B, W, H, mcx, mcy, w->maxed ? 0xf066 : 0xf065, 11, ui_mix(ui_mix(0x6c748cu, 0xb4bbd0u, fz), 0xffffffu, (unsigned)(mh * 255)));
    }
    ui_icon(B, W, H, ccx, ccy, IC_CLOSE, 12, ui_mix(ui_mix(0x6c748cu, 0xb4bbd0u, fz), 0xffffffu, (unsigned)(ch * 255)));
    ui_set_clip(0, 0, 0x7fffffff, 0x7fffffff);
}

/* Repaint everything inside `r` (screen coordinates), bottom to top, then blit it. */
/* ---- window animations ----
   A window that opens or closes is first captured as a premultiplied-ARGB snapshot (its frame, content and shadow, drawn once over black
   and once over white; the difference gives the alpha), then that snapshot is scaled and faded each frame. Closing windows live on as
   ghosts after the window itself is gone. */
static struct rect inflate_r(struct rect r, int d) { return (struct rect){ r.x0 - d, r.y0 - d, r.x1 + d, r.y1 + d }; }
static void snap_build(struct win *w) {
    free(w->snap); w->snap = 0;
    struct rect r = clip(outer_of(w));
    if (empty(r)) return;
    int sw = r.x1 - r.x0, sh = r.y1 - r.y0;
    uint32_t *out = malloc((size_t)sw * sh * 4), *blk = malloc((size_t)sw * sh * 4);
    if (!out || !blk) { free(out); free(blk); return; }
    float keep = w->foc.p, keepsc = w->sc.p;
    w->foc.p = 1;                                                   /* captured in its focused look */
    for (int pass = 0; pass < 2; pass++) {
        uint32_t bgc = pass ? 0x00ffffffu : 0u;
        for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) bb[(size_t)y * W + x] = bgc;
        if (w->deco) draw_shadow(w, 1, r);
        draw_window(w, 1, r);
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++) {
                uint32_t p = bb[(size_t)(r.y0 + y) * W + r.x0 + x];
                if (!pass) blk[(size_t)y * sw + x] = p;
                else {
                    uint32_t b0 = blk[(size_t)y * sw + x];
                    int dr = (int)((p >> 16) & 255) - (int)((b0 >> 16) & 255), dg = (int)((p >> 8) & 255) - (int)((b0 >> 8) & 255), db = (int)(p & 255) - (int)(b0 & 255);
                    int a = 255 - (dr + dg + db) / 3;
                    if (a < 0) a = 0;
                    if (a > 255) a = 255;
                    out[(size_t)y * sw + x] = ((uint32_t)a << 24) | (b0 & 0x00ffffffu);          /* colour over black is already premultiplied */
                }
            }
    }
    free(blk);
    w->foc.p = keep; w->sc.p = keepsc;
    w->snap = out; w->snap_r = r;
}
/* draw a snapshot scaled about (ax, ay) (fractions of its size) and faded; only inside `r` */
static void draw_snap_at(const uint32_t *snap, struct rect o, float ax, float ay, float scale, float opac, struct rect r) {
    if (!snap) return;
    if (scale < 0.05f) scale = 0.05f;
    if (opac <= 0) return;
    if (opac > 1) opac = 1;
    int sw = o.x1 - o.x0, sh = o.y1 - o.y0;
    float cx = o.x0 + ax * sw, cy = o.y0 + ay * sh, inv = 1.0f / scale;
    struct rect d = isect(clip((struct rect){ (int)floorf(cx + (o.x0 - cx) * scale), (int)floorf(cy + (o.y0 - cy) * scale), (int)ceilf(cx + (o.x1 - cx) * scale), (int)ceilf(cy + (o.y1 - cy) * scale) }), r);
    if (empty(d)) return;
    unsigned op = (unsigned)(opac * 256.0f);
    for (int y = d.y0; y < d.y1; y++) {
        int sy = (int)((y + 0.5f - cy) * inv + cy - o.y0);
        if (sy < 0 || sy >= sh) continue;
        const uint32_t *row = snap + (size_t)sy * sw;
        uint32_t *dst = &bb[(size_t)y * W];
        float fx = (d.x0 + 0.5f - cx) * inv + cx - o.x0;
        for (int x = d.x0; x < d.x1; x++, fx += inv) {
            int sx = (int)fx;
            if (sx < 0 || sx >= sw) continue;
            uint32_t p = row[sx];
            unsigned a = (p >> 24) * op >> 8;
            if (!a) continue;
            uint32_t c = scale_px(p & 0x00ffffffu, op);
            dst[x] = (c & 0x00ffffffu) + scale_px(dst[x], 256 - a - (a >> 7));
        }
    }
}
struct ghost { uint32_t *snap; struct rect r; float ax, ay; struct spring sc; struct tween opq; int active; };
#define MAXGHOST 8
static struct ghost ghosts[MAXGHOST];
static void draw_ghosts(struct rect r) {
    for (int i = 0; i < MAXGHOST; i++) if (ghosts[i].active) draw_snap_at(ghosts[i].snap, ghosts[i].r, ghosts[i].ax, ghosts[i].ay, ghosts[i].sc.p, tween_value(&ghosts[i].opq, ease_in), r);
}
/* the window is about to be removed: keep a fading copy of it */
static void ghost_from(struct win *w) {
    if (w->anim == 1 || (!w->deco && !w->menu && !w->popup) || w->taskbar) return;
    int slot = -1;
    for (int i = 0; i < MAXGHOST; i++) if (!ghosts[i].active) { slot = i; break; }
    if (slot < 0) return;
    float s0 = w->anim == 2 ? w->sc.p : 1.0f;
    if (w->anim != 2 || !w->snap) snap_build(w);
    if (!w->snap) return;
    struct ghost *g = &ghosts[slot];
    g->snap = w->snap; w->snap = 0; g->r = w->snap_r; g->ax = w->ax; g->ay = w->ay; g->active = 1;
    int quick = w->menu || w->popup;
    spring_init(&g->sc, s0, SP_SNAPPY);
    spring_to(&g->sc, quick ? 0.97f : 0.94f);
    tween_start(&g->opq, 1.0f, 0.0f, quick ? 110 : MO_FAST + 20);
}
static void win_anim_start(struct win *w) {                           /* called when a window is created: it stays hidden until its first content arrived */
    w->anim = 1; w->anim_t0 = mo_now_ms(); w->ax = 0.5f; w->ay = 0.5f;
    if (w->menu) { w->ax = 0.12f; w->ay = 1.0f; }
    if (w->popup) { w->ax = 0.3f; w->ay = 0.0f; }
    spring_init(&w->sc, 0.9f, SP_POP);
    spring_init(&w->foc, 0, SP_SNAPPY);
}

static void compose(struct rect r) {
    r = clip(r);
    if (empty(r)) return;
    for (int y = r.y0; y < r.y1; y++) memcpy(&bb[(size_t)y * W + r.x0], &wall[(size_t)y * W + r.x0], (size_t)(r.x1 - r.x0) * 4);
    draw_icons(r);
    for (int i = 0; i < nwin; i++) {
        struct win *w = &wins[i];
        int top = i == nwin - 1;
        if (w->anim == 1) continue;                                     /* not shown yet */
        if (w->anim == 2) { draw_snap_at(w->snap, w->snap_r, w->ax, w->ay, w->sc.p, tween_value(&w->opq, ease_out), r); continue; }
        if (w->deco) draw_shadow(w, top, r);
        draw_window(w, top, r);
    }
    draw_ghosts(r);
    for (int y = r.y0; y < r.y1; y++)
        blit_row((uint32_t *)(fbmap + (size_t)y * pitch + (size_t)r.x0 * 4), &bb[(size_t)y * W + r.x0], r.x1 - r.x0);
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
    ghost_from(&wins[i]);                                    /* a closing window fades out as a ghost */
    free(wins[i].snap);
    wins[i].snap = 0;
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
        w->ready = 1;
        if (w->anim == 1) return;                                       /* appears with its pop-in */
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
        win_anim_start(w);
        w->ready = 1;
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

static void place_window(struct win *w);
#include "thoswls.h"

/* ---- the animation clock: window pop-in, focus changes, closing ghosts ---- */
static long long anim_last;
static int anim_pending(void) {
    if (icons_busy()) return 1;
    for (int i = 0; i < nwin; i++) if (wins[i].mxs.k != 0 && (spring_busy(&wins[i].mxs) || wins[i].mxs.t != (hover_max == wins[i].id ? 1.0f : 0.0f))) return 1;
    for (int i = 0; i < nwin; i++) if (wins[i].clo.k != 0 && (spring_busy(&wins[i].clo) || wins[i].clo.t != (hover_win == wins[i].id ? 1.0f : 0.0f))) return 1;
    for (int i = 0; i < nwin; i++) if (wins[i].anim || (wins[i].foc.k != 0 && (spring_busy(&wins[i].foc) || wins[i].foc.t != (i == nwin - 1 ? 1.0f : 0.0f)))) return 1;
    for (int i = 0; i < MAXGHOST; i++) if (ghosts[i].active) return 1;
    return 0;
}
static void anim_tick(void) {
    long long t = mo_now_ms();
    if (t - anim_last < MO_FRAME_MS) return;
    float dt = (float)(t - anim_last) / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;
    anim_last = t;
    for (int i = 0; i < nwin; i++) {
        struct win *w = &wins[i];
        float tgt = i == nwin - 1 ? 1.0f : 0.0f;
        if (w->foc.k == 0) spring_init(&w->foc, tgt, SP_SNAPPY);        /* windows that never popped in (the panel) just follow the focus */
        if (w->foc.t != tgt) spring_to(&w->foc, tgt);
        if (w->mxs.k == 0) spring_init(&w->mxs, 0, SP_HOVER);
        spring_to(&w->mxs, hover_max == w->id ? 1.0f : 0.0f);
        if (spring_busy(&w->mxs)) { spring_step(&w->mxs, dt); if (w->anim == 0) compose(inflate_r(max_btn(w), 3)); }
        if (w->clo.k == 0) spring_init(&w->clo, 0, SP_HOVER);
        spring_to(&w->clo, hover_win == w->id ? 1.0f : 0.0f);
        if (spring_busy(&w->clo)) { spring_step(&w->clo, dt); if (w->anim == 0) compose(inflate_r(close_btn(w), 3)); }
        if (spring_busy(&w->foc)) { spring_step(&w->foc, dt); if (w->anim != 2 && w->anim != 1) compose(inflate_r(outer_of(w), 2)); }
        if (w->anim == 1) {
            if ((w->ready && t - w->anim_t0 >= 30) || t - w->anim_t0 > 350) {
                w->foc.p = w->foc.t;                                   /* the snapshot is taken in its final look */
                snap_build(w);
                if (!w->snap) { w->anim = 0; compose(outer_of(w)); continue; }
                w->anim = 2;
                spring_to(&w->sc, 1.0f);
                tween_start(&w->opq, 0.0f, 1.0f, MO_BASE);
                compose(inflate_r(w->snap_r, 14));
            }
        } else if (w->anim == 2) {
            spring_step(&w->sc, dt);
            struct rect rr = inflate_r(w->snap_r, 14);
            if (!spring_busy(&w->sc) && !tween_busy(&w->opq)) { w->anim = 0; free(w->snap); w->snap = 0; }
            compose(rr);
        }
    }
    if (icons_busy()) { icons_step(dt); for (int j = 0; j < nicons; j++) compose(inflate_r(icon_cell(j), 10)); }
    for (int i = 0; i < MAXGHOST; i++) {
        struct ghost *g = &ghosts[i];
        if (!g->active) continue;
        spring_step(&g->sc, dt);
        struct rect rr = inflate_r(g->r, 14);
        if (!tween_busy(&g->opq)) { g->active = 0; free(g->snap); g->snap = 0; }
        compose(rr);
    }
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
static int work_bottom(void);
/* a new window opens near the middle of the free area (above the dock), each one a little offset from the last, never off screen */
static int cascade_n;
static void place_window(struct win *w) {
    { static int legacy = -1; if (legacy < 0) { const char *e = getenv("THOS_PLACE"); legacy = e && e[0] == 'l'; } if (legacy) return; }   /* THOS_PLACE=legacy: the fixed cascade the geometry-based tests were written for */
    int off = ((cascade_n++ % 6) - 2) * 30, wb = work_bottom();
    if (w->csd) {
        int x = (W - w->gw) / 2 + off, y = (wb - w->gh) / 2 + off / 2;
        if (x < 8) x = 8;
        if (x + w->gw > W - 8) x = W - 8 - w->gw;
        if (y < 8) y = 8;
        if (y + w->gh > wb - 10) y = wb - 10 - w->gh;
        w->x = x - w->gx; w->y = y - w->gy;
        return;
    }
    int top = w->deco ? TITLE : 0, ow = w->w + (w->deco ? 2 * BORDER : 0), oh = w->h + (w->deco ? TITLE + BORDER : 0);
    int x = (W - ow) / 2 + off, y = (wb - oh) / 2 + off / 2;
    if (x < 6) x = 6;
    if (x + ow > W - 6) x = W - 6 - ow;
    if (y < 8) y = 8;
    if (y + oh > wb - 10) y = wb - 10 - oh;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    w->x = x + (w->deco ? BORDER : 0); w->y = y + top;
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
    int nh = -1, nm = -1;
    for (int i = nwin - 1; i >= 0; i--) {
        if (!in(frame_of(&wins[i]), mx, my)) continue;
        if (in(close_btn(&wins[i]), mx, my)) nh = wins[i].id;
        if (in(max_btn(&wins[i]), mx, my)) nm = wins[i].id;
        break;
    }
    hover_max = nm;
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
    w->taskbar = (m.d & 4) != 0;   /* the panel: told about the open windows */
    w->alpha = (m.d & 8) != 0;     /* premultiplied ARGB pixels (the floating dock, the Start menu) */      /* a start-menu surface: closed by a click anywhere outside it */
    if (!w->deco) { w->x = 0; w->y = 0; }
    else place_window(w);
    struct wl_msg r = { WL_CREATED, (uint32_t)w->id, (uint32_t)W, (uint32_t)H, 0 };
    (void)!write(c, &r, sizeof r);
    printf("desk: window %d created %dx%d at %d,%d\n", w->id, w->w, w->h, w->x, w->y);
    fflush(stdout);
    if (w->deco || w->menu) win_anim_start(w);
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
    fb_swap = !(rs == 16 && gs == 8 && bs == 0);
    ui_init();
    pitch = *(uint32_t *)(fix + 48);
    fbmap = mmap(0, (size_t)pitch * H, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    bb = calloc((size_t)W * H, 4);
    if (fbmap == MAP_FAILED || !bb) { puts("desk FAIL: mmap"); return 1; }
    theme_init();
    cursor_init();
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
            if (read(wheel, &z, 1) == 1) {
                wls_wheel(z);
                struct win *hc = hover_client ? win_by_id(hover_client) : 0;
                if (hc && hc->fd >= 0) { struct wl_msg wm = { WL_POINTER, (uint32_t)(mx - hc->x), (uint32_t)(my - hc->y), 3, (uint32_t)(int32_t)z }; (void)!write(hc->fd, &wm, sizeof wm); }   /* legacy clients: c = 3, d = signed clicks */
            }
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
            if (ddx || ddy) {                                /* legacy clients see the pointer move over their content (c = 0), and leave it (c = 2) */
                int over = -1;
                for (int i = nwin - 1; i >= 0 && over < 0; i--) if (in(frame_of(&wins[i]), mx, my)) over = i;
                int over_id = over >= 0 && !wins[over].is_wl && !wins[over].hwnd && wins[over].fd >= 0 && in(content_of(&wins[over]), mx, my) && !drag && !rs_id ? wins[over].id : 0;
                if (hover_client && hover_client != over_id) {
                    struct win *o = win_by_id(hover_client);
                    if (o && o->fd >= 0) { struct wl_msg lm = { WL_POINTER, 0, 0, 2, 0 }; (void)!write(o->fd, &lm, sizeof lm); }
                }
                hover_client = over_id;
                if (over_id) {
                    struct wl_msg pm = { WL_POINTER, (uint32_t)(mx - wins[over].x), (uint32_t)(my - wins[over].y), 0, 0 };
                    (void)!write(wins[over].fd, &pm, sizeof pm);
                }
            }
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
                int old_sel = icon_sel;
                icon_sel = ic;
                if (old_sel != icon_sel) { if (old_sel >= 0) compose(inflate_r(icon_cell(old_sel), 6)); if (icon_sel >= 0) compose(inflate_r(icon_cell(icon_sel), 6)); }
                if (ic >= 0) {
                    if (ic == last_icon && tn - last_icon_ms < 500) { launch_cmd(icons[ic].cmd); last_icon = -1; icon_sc[ic].p = 1.0f; icon_sc[ic].v = 3.2f; }       /* a bounce: it pops out and settles */
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
                    if (in(max_btn(w), mx, my)) { toggle_maximize(w); click_id = 0; }
                    else if (in(close_btn(w), mx, my)) {
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
            icons_springs_init();
            { int over = 0; for (int i = 0; i < nwin; i++) if (in(frame_of(&wins[i]), mx, my)) over = 1; int hi = over || drag || rs_id ? -1 : icon_at(mx, my); for (int j = 0; j < nicons; j++) spring_to(&icon_hov[j], j == hi ? 1.0f : 0.0f); }
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
