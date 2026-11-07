// SPDX-License-Identifier: GPL-2.0-or-later
// Host differential test: optimized shapes must preserve every legacy raster pixel.
// gcc -O2 thosui-raster-test.c $(pkg-config --cflags --libs freetype2) -lm -o /tmp/thosui-raster-test
#include <assert.h>
#include <time.h>
#include "thosui.h"

/* Frozen scalar renderer, the oracle for gradients, clipping and antialiased corners. */
static void reference_rrect(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, int r, uint32_t c0, uint32_t c1, unsigned a0, unsigned a1) {
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
#define BW 113
#define BH 79
static uint32_t expected[BW * BH], actual[BW * BH];
static uint32_t seed = 0x54484f53;
static unsigned rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
int main(void) {
    for (int test = 0; test < 10000; test++) {
        for (int i = 0; i < BW * BH; i++) expected[i] = actual[i] = rnd();
        int x = (int)(rnd() % 173) - 60, y = (int)(rnd() % 139) - 60;
        int w = rnd() % 160, h = rnd() % 130, r = rnd() % 90;
        unsigned a0 = rnd() % 256, a1 = rnd() % 256;
        if (test % 3 == 0) a0 = a1 = 255;
        if (test % 7 == 0) a0 = a1 = 0;
        uint32_t c0 = rnd(), c1 = rnd();
        if (test % 5 == 0) c1 = c0;
        int cx0 = rnd() % BW, cy0 = rnd() % BH;
        int cx1 = cx0 + rnd() % (BW - cx0 + 1), cy1 = cy0 + rnd() % (BH - cy0 + 1);
        if (test % 2 == 0) ui_set_clip(cx0, cy0, cx1, cy1);
        else ui_set_clip(0, 0, BW, BH);
        reference_rrect(expected, BW, BH, x, y, w, h, r, c0, c1, a0, a1);
        ui_rrect_ga(actual, BW, BH, x, y, w, h, r, c0, c1, a0, a1);
        if (memcmp(expected, actual, sizeof actual)) {
            fprintf(stderr, "Raster mismatch in case %d: rect %d,%d %dx%d radius %d opacity %u/%u\n", test, x, y, w, h, r, a0, a1);
            return 1;
        }
    }
    puts("PASS: 10000 randomized shapes match the original renderer pixel for pixel");
    enum { W = 780, H = 500, N = 500 };
    uint32_t *buf = calloc(W * H, sizeof *buf);
    if (!buf) return 1;
    ui_set_clip(0, 0, W, H);
    clock_t begin = clock();
    for (int i = 0; i < N; i++) reference_rrect(buf, W, H, 0, 0, W, H, 14, UI_SURFACE, UI_RAISED, 255, 255);
    double before = (double)(clock() - begin) / CLOCKS_PER_SEC;
    volatile uint32_t sink = buf[W * H / 2];
    begin = clock();
    for (int i = 0; i < N; i++) ui_rrect_ga(buf, W, H, 0, 0, W, H, 14, UI_SURFACE, UI_RAISED, 255, 255);
    double after = (double)(clock() - begin) / CLOCKS_PER_SEC;
    sink ^= buf[W * H / 2]; (void)sink;
    printf("Host shape microbenchmark (not desktop FPS): scalar %.3fs optimized %.3fs\n", before, after);
    free(buf);
    return 0;
}
