// SPDX-License-Identifier: GPL-2.0-or-later
// THOS UI toolkit for the desktop clients: anti-aliased text (FreeType, Lato + Font Awesome icons), rounded shapes with
// soft edges, vertical gradients and drop shadows, all into a 32-bit 0x00RRGGBB (or premultiplied 0xAARRGGBB) buffer.
// Everything is integer or tiny-float work with a glyph cache, so it stays cheap on the 2010 laptop.
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <poll.h>
#include "thosmotion.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- design tokens (one place for the whole desktop) ---- */
#define UI_BG        0x0e1118u   /* deepest ground */
#define UI_SURFACE   0x171b26u   /* windows, panels */
#define UI_RAISED    0x1f2432u   /* titlebars, cards */
#define UI_HOVER     0x2a3144u
#define UI_LINE      0x2d3448u
#define UI_TEXT      0xe8ebf4u
#define UI_MUTED     0x8e97aeu
#define UI_ACCENT    0x5b8defu
#define UI_ACCENT_HI 0x7ba3ffu
#define UI_GOOD      0x4cc38au
#define UI_WARN      0xf0b24bu
#define UI_BAD       0xe5534bu

enum { UI_REG = 0, UI_BOLD = 1, UI_ICON = 2, UI_MONO = 3 };
static int ui_clip[4] = { 0, 0, 0x7fffffff, 0x7fffffff };   /* x0, y0, x1, y1 (half-open): nothing outside is touched */
static void ui_set_clip(int x0, int y0, int x1, int y1) { ui_clip[0] = x0; ui_clip[1] = y0; ui_clip[2] = x1; ui_clip[3] = y1; }
static FT_Library ui_ft;
static FT_Face ui_face[4];
static int ui_ready;

/* Font Awesome 4 code points used around the desktop */
#define IC_FOLDER 0xf07b
#define IC_FILE 0xf15b
#define IC_TERMINAL 0xf120
#define IC_COG 0xf013
#define IC_DOWNLOAD 0xf019
#define IC_IMAGE 0xf03e
#define IC_PUZZLE 0xf12e
#define IC_CLOSE 0xf00d
#define IC_POWER 0xf011
#define IC_REFRESH 0xf021
#define IC_SEARCH 0xf002
#define IC_PIN 0xf08d
#define IC_DESKTOP 0xf108
#define IC_WIN 0xf17a
#define IC_INFO 0xf05a
#define IC_EDIT 0xf040
#define IC_ARCHIVE 0xf1c6
#define IC_BOX 0xf187
#define IC_UP 0xf148
#define IC_CHEV 0xf054
#define IC_HOME 0xf015
#define IC_GLOBE 0xac01

static int ui_init(void) {
    if (ui_ready) return 0;
    if (FT_Init_FreeType(&ui_ft)) return -1;
    const char *f[4] = { "/usr/share/fonts/thos/Lato-Regular.ttf", "/usr/share/fonts/thos/Lato-Bold.ttf", "/usr/share/fonts/thos/icons.ttf", "/usr/share/fonts/thos/Hack-Regular.ttf" };
    for (int i = 0; i < 4; i++) if (FT_New_Face(ui_ft, f[i], 0, &ui_face[i])) { ui_face[i] = 0; if (i < 2) return -1; }
    ui_ready = 1;
    return 0;
}

/* ---- glyph cache ---- */
struct ui_glyph { int face, size; uint32_t cp; int w, h, left, top, adv; uint8_t *bmp; int used; };
#define UI_CACHE 1024
static struct ui_glyph ui_cache[UI_CACHE];
static struct ui_glyph *ui_glyph(int face, int size, uint32_t cp) {
    if (!ui_face[face]) return 0;
    unsigned h = (cp * 2654435761u + (unsigned)size * 40503u + (unsigned)face * 977u) % UI_CACHE;
    for (int probe = 0; probe < 32; probe++, h = (h + 1) % UI_CACHE) {
        struct ui_glyph *g = &ui_cache[h];
        if (g->used && g->face == face && g->size == size && g->cp == cp) return g;
        if (!g->used) {
            FT_Set_Pixel_Sizes(ui_face[face], 0, (FT_UInt)size);
            if (FT_Load_Char(ui_face[face], cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) return 0;
            FT_GlyphSlot s = ui_face[face]->glyph;
            g->face = face; g->size = size; g->cp = cp; g->used = 1;
            g->w = (int)s->bitmap.width; g->h = (int)s->bitmap.rows; g->left = s->bitmap_left; g->top = s->bitmap_top; g->adv = (int)(s->advance.x >> 6);
            g->bmp = malloc((size_t)g->w * g->h + 1);
            if (g->bmp) for (int y = 0; y < g->h; y++) memcpy(g->bmp + (size_t)y * g->w, s->bitmap.buffer + (size_t)y * s->bitmap.pitch, (size_t)g->w);
            return g;
        }
    }
    return 0;
}
static uint32_t ui_next(const char **p) {                              /* the next UTF-8 code point */
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = *s++;
    if (c >= 0xF0 && s[0] && s[1] && s[2]) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
    else if (c >= 0xE0 && s[0] && s[1]) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
    else if (c >= 0xC0 && s[0]) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
    *p = (const char *)s;
    return c;
}
static inline uint32_t ui_mix(uint32_t d, uint32_t s, unsigned a) {      /* d + (s - d) * a / 255 */
    if (a >= 255) return s & 0x00FFFFFFu;
    if (a == 0) return d;
    unsigned dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255, sr = (s >> 16) & 255, sg = (s >> 8) & 255, sb = s & 255;
    return ((dr + ((sr - (int)dr) * (int)a) / 255) << 16) | ((dg + ((sg - (int)dg) * (int)a) / 255) << 8) | (db + ((sb - (int)db) * (int)a) / 255);
}
static int ui_text_width(int face, int size, const char *s) {
    int w = 0;
    while (*s) { struct ui_glyph *g = ui_glyph(face, size, ui_next(&s)); if (g) w += g->adv; }
    return w;
}
/* text with its baseline at y; returns the advance; `alpha` scales the colour (0..255) */
static int ui_text_a(uint32_t *buf, int bw, int bh, int x, int y, const char *s, int face, int size, uint32_t color, unsigned alpha) {
    int x0 = x;
    while (*s) {
        struct ui_glyph *g = ui_glyph(face, size, ui_next(&s));
        if (!g) continue;
        for (int j = 0; j < g->h; j++) {
            int py = y - g->top + j;
            if (py < 0 || py >= bh || py < ui_clip[1] || py >= ui_clip[3]) continue;
            for (int i = 0; i < g->w; i++) {
                int px = x + g->left + i;
                if (px < 0 || px >= bw || px < ui_clip[0] || px >= ui_clip[2]) continue;
                unsigned a = g->bmp[j * g->w + i] * alpha / 255;
                if (a) buf[(size_t)py * bw + px] = ui_mix(buf[(size_t)py * bw + px], color, a);
            }
        }
        x += g->adv;
    }
    return x - x0;
}
static int ui_text(uint32_t *buf, int bw, int bh, int x, int y, const char *s, int face, int size, uint32_t color) { return ui_text_a(buf, bw, bh, x, y, s, face, size, color, 255); }
/* an icon glyph centred on (cx, cy) */
static void ui_icon(uint32_t *buf, int bw, int bh, int cx, int cy, uint32_t cp, int size, uint32_t color) {
    struct ui_glyph *g = ui_glyph(UI_ICON, size, cp);
    if (!g) return;
    int x = cx - g->w / 2 - g->left, y = cy + g->h / 2 + (g->top - g->h);
    char u[5]; int n = 0;
    if (cp < 0x80) u[n++] = (char)cp; else if (cp < 0x800) { u[n++] = (char)(0xC0 | (cp >> 6)); u[n++] = (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { u[n++] = (char)(0xE0 | (cp >> 12)); u[n++] = (char)(0x80 | ((cp >> 6) & 63)); u[n++] = (char)(0x80 | (cp & 63)); }
    u[n] = 0;
    ui_text(buf, bw, bh, x, y, u, UI_ICON, size, color);
}

/* ---- shapes ---- */
/* coverage (0..255) of the pixel at (px,py) for a rounded rectangle x,y,w,h with radius r */
static inline unsigned ui_cov(int px, int py, int x, int y, int w, int h, int r) {
    if (px < x || py < y || px >= x + w || py >= y + h) return 0;
    int dx = 0, dy = 0;
    if (px < x + r) dx = x + r - 1 - px; else if (px >= x + w - r) dx = px - (x + w - r);
    if (py < y + r) dy = y + r - 1 - py; else if (py >= y + h - r) dy = py - (y + h - r);
    if (!dx || !dy) { if (dx < 1 || dy < 1) { if (dx == 0 && dy == 0) {} } }
    if (dx <= 0 || dy <= 0) return 255;
    /* distance from the corner circle centre: pixel centre at (dx+0.5, dy+0.5) against radius r */
    float d = sqrtf((float)((dx + 1 - 0.5f) * (dx + 1 - 0.5f) + (dy + 1 - 0.5f) * (dy + 1 - 0.5f)));
    float c = (float)r + 0.5f - d;
    if (c >= 1.0f) return 255;
    if (c <= 0.0f) return 0;
    return (unsigned)(c * 255.0f);
}
/* a rounded rectangle, filled with a vertical gradient c0 -> c1 whose opacity also runs from a0 (top) to a1 (bottom), 0..255 */
static void ui_rrect_ga(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1, unsigned a0, unsigned a1) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= bh || py < ui_clip[1] || py >= ui_clip[3]) continue;
        unsigned t = h > 1 ? (unsigned)(j * 255 / (h - 1)) : 0;
        uint32_t col = ui_mix(c0, c1, t);
        unsigned alpha = (unsigned)((int)a0 + ((int)a1 - (int)a0) * (int)t / 255);
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if (px < 0 || px >= bw || px < ui_clip[0] || px >= ui_clip[2]) continue;
            unsigned a = (r > 0 && ((i < r || i >= w - r) && (j < r || j >= h - r))) ? ui_cov(px, py, x, y, w, h, r) : 255;
            a = a * alpha / 255;
            if (a) buf[(size_t)py * bw + px] = ui_mix(buf[(size_t)py * bw + px], col, a);
        }
    }
}
static void ui_rrect_g(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1, unsigned alpha) { ui_rrect_ga(buf, bw, bh, x, y, w, h, r, c0, c1, alpha, alpha); }
/* the glossy sheen over the upper half of a rounded shape */
static void ui_gloss(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, int r) {
    ui_rrect_ga(buf, bw, bh, x + 1, y + 1, w - 2, h / 2, r > 1 ? r - 1 : 0, 0xffffffu, 0xffffffu, 70, 6);
}
static void ui_rrect(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, int r, uint32_t c) { ui_rrect_g(buf, bw, bh, x, y, w, h, r, c, c, 255); }
/* a rounded rectangle with a 1 px border */
static void ui_card(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, int r, uint32_t fill, uint32_t border) {
    ui_rrect(buf, bw, bh, x, y, w, h, r, border);
    ui_rrect(buf, bw, bh, x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, fill);
}
/* a filled disc */
static void ui_disc(uint32_t *buf, int bw, int bh, int cx, int cy, int rad, uint32_t c) { ui_rrect(buf, bw, bh, cx - rad, cy - rad, rad * 2, rad * 2, rad, c); }

/* the glyph and tile colour a program gets on the desktop, in the Start menu and the software centre */
static uint32_t ui_app_glyph(const char *label) {
    static const struct { const char *k; uint32_t g; } m[] = { { "Terminal", IC_TERMINAL }, { "Files", IC_FOLDER }, { "Software", IC_DOWNLOAD }, { "Text", IC_EDIT }, { "Foot", IC_TERMINAL },
        { "Win32", IC_WIN }, { "System", IC_INFO }, { "Web", 0xf0ac }, { "Image", IC_IMAGE }, { "GTK", IC_PUZZLE }, { "Pkg", IC_BOX } };
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) if (!strncasecmp(label, m[i].k, strlen(m[i].k))) return m[i].g;
    return IC_PUZZLE;
}
static uint32_t ui_app_color(const char *label) {
    static const uint32_t pal[] = { 0x4f7cff, 0x8b5cf6, 0x14b8a6, 0xf59e0b, 0xec4899, 0x22c55e, 0x38bdf8 };
    unsigned h = 0;
    for (const char *p = label; *p; p++) h = h * 31 + (unsigned char)*p;
    return pal[h % (sizeof pal / sizeof pal[0])];
}
static uint32_t ui_shade(uint32_t c, int pct) {                     /* pct < 0 darker, > 0 lighter */
    unsigned ch[3] = { (c >> 16) & 255, (c >> 8) & 255, c & 255 };
    for (int i = 0; i < 3; i++) { int v = pct >= 0 ? (int)ch[i] + (255 - (int)ch[i]) * pct / 100 : (int)ch[i] * (100 + pct) / 100; ch[i] = (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v); }
    return (ch[0] << 16) | (ch[1] << 8) | ch[2];
}
/* an app tile: a rounded gradient square with the program's glyph */
static void ui_tile(uint32_t *buf, int bw, int bh, int x, int y, int size, const char *label) {
    uint32_t c = ui_app_color(label);
    ui_rrect_g(buf, bw, bh, x, y + 2, size, size, size / 4, 0x000000u, 0x000000u, 70);              /* soft contact shadow */
    ui_rrect_g(buf, bw, bh, x, y, size, size, size / 4, ui_shade(c, 22), ui_shade(c, -18), 255);
    ui_gloss(buf, bw, bh, x, y, size, size, size / 4);
    ui_icon(buf, bw, bh, x + size / 2, y + size / 2, ui_app_glyph(label), size * 5 / 12, 0xffffffu);
}

/* ---- ui_list: a scrolling list whose selection and hover highlights glide between rows, and which scrolls smoothly ----
   The list does not draw rows itself: the program asks ui_list_row_y() where a row is, draws it there, and draws the pills first
   (ui_list_pills). Every change of selection, hover or scroll position moves a spring, so the whole thing feels continuous. */
struct ui_list { int n, row_h, view_h, sel, hover; float tscroll; struct spring scroll, selp, hovp, hova; };
static void ui_list_init(struct ui_list *l, int row_h, int view_h) {
    memset(l, 0, sizeof *l);
    l->row_h = row_h; l->view_h = view_h; l->hover = -1;
    spring_init(&l->scroll, 0, SP_SOFT); spring_init(&l->selp, 0, SP_SNAPPY); spring_init(&l->hovp, 0, SP_HOVER); spring_init(&l->hova, 0, SP_HOVER);
}
static void ui_list_clamp(struct ui_list *l) {
    float max = (float)(l->n * l->row_h - l->view_h);
    if (max < 0) max = 0;
    if (l->tscroll > max) l->tscroll = max;
    if (l->tscroll < 0) l->tscroll = 0;
    spring_to(&l->scroll, l->tscroll);
}
static void ui_list_reset(struct ui_list *l, int n) {            /* new content: back to the top, selection on the first row, no gliding */
    l->n = n; l->sel = 0; l->hover = -1; l->tscroll = 0;
    spring_init(&l->scroll, 0, SP_SOFT); spring_init(&l->selp, 0, SP_SNAPPY); spring_init(&l->hova, 0, SP_HOVER);
    spring_to(&l->hova, 0);
}
static void ui_list_select(struct ui_list *l, int i) {
    if (l->n == 0) return;
    if (i < 0) i = 0;
    if (i >= l->n) i = l->n - 1;
    l->sel = i;
    spring_to(&l->selp, (float)(i * l->row_h));
    if (i * l->row_h < l->tscroll) l->tscroll = (float)(i * l->row_h);
    if ((i + 1) * l->row_h > l->tscroll + l->view_h) l->tscroll = (float)((i + 1) * l->row_h - l->view_h);
    ui_list_clamp(l);
}
static void ui_list_hover(struct ui_list *l, int i) {
    if (i < 0 || i >= l->n) { l->hover = -1; spring_to(&l->hova, 0); return; }
    if (l->hover < 0 && l->hova.p < 0.05f) { spring_init(&l->hovp, (float)(i * l->row_h), SP_HOVER); spring_to(&l->hova, 1); }   /* appears where the pointer is */
    l->hover = i;
    spring_to(&l->hovp, (float)(i * l->row_h));
    spring_to(&l->hova, 1);
}
static void ui_list_wheel(struct ui_list *l, int clicks) { l->tscroll -= (float)clicks * l->row_h * 1.6f; ui_list_clamp(l); }
static int ui_list_busy(const struct ui_list *l) { return spring_busy(&l->scroll) || spring_busy(&l->selp) || spring_busy(&l->hovp) || spring_busy(&l->hova); }
static void ui_list_step(struct ui_list *l, float dt) { spring_step(&l->scroll, dt); spring_step(&l->selp, dt); spring_step(&l->hovp, dt); spring_step(&l->hova, dt); }
static int ui_list_at(const struct ui_list *l, int y) { int i = (int)((y + l->scroll.p) / l->row_h); return y < 0 || i < 0 || i >= l->n ? -1 : i; }
static int ui_list_row_y(const struct ui_list *l, int i) { return (int)(i * l->row_h - l->scroll.p + 0.5f); }
/* the highlight pills, drawn at list-relative offset (x, y) with width w; call before drawing the rows */
static void ui_list_pills(uint32_t *buf, int bw, int bh, const struct ui_list *l, int x, int y, int w, uint32_t sel_color, uint32_t hover_color, int radius) {
    if (l->hova.p > 0.02f) ui_rrect_g(buf, bw, bh, x, y + (int)(l->hovp.p - l->scroll.p + 0.5f), w, l->row_h - 2, radius, hover_color, hover_color, (unsigned)(l->hova.p > 1 ? 255 : l->hova.p * 255));
    if (l->n) {
        int py = y + (int)(l->selp.p - l->scroll.p + 0.5f);
        ui_rrect(buf, bw, bh, x, py, w, l->row_h - 2, radius, sel_color);
        ui_rrect(buf, bw, bh, x, py + 7, 3, l->row_h - 16, 1, UI_ACCENT_HI);
    }
}
/* a thin scrollbar at x, spanning the list's view (visible only while scrolling) */
static void ui_scrollbar(uint32_t *buf, int bw, int bh, const struct ui_list *l, int x, int y) {
    int total = l->n * l->row_h;
    if (total <= l->view_h) return;
    float vis = (float)l->view_h / (float)total;
    float a = spring_busy(&l->scroll) ? 1.0f : 0.0f;
    static float shown;                                      /* a slow fade so the bar lingers a moment */
    shown += (a - shown) * (a > shown ? 0.5f : 0.06f);
    if (shown < 0.03f) return;
    int th = (int)(l->view_h * vis); if (th < 24) th = 24;
    int ty = y + (int)((l->view_h - th) * (l->scroll.p / (float)(total - l->view_h)));
    ui_rrect_g(buf, bw, bh, x, ty, 5, th, 2, 0x8e97aeu, 0x8e97aeu, (unsigned)(shown * 150));
}
/* the blocking part of a client's event loop: sleeps until the compositor sends something, but wakes every animation frame while
   anything is moving. Returns what poll returned (> 0: a message is waiting). */
static int ui_wait(int sock, int animating) {
    struct pollfd p = { sock, POLLIN, 0 };
    return poll(&p, 1, animating ? MO_FRAME_MS : -1);
}
