// SPDX-License-Identifier: GPL-2.0-or-later
// Tiny text drawing for THOS desktop clients: the console's 8x16 PSF1 font, loaded from /font.psf.
// (The font is Terminus under the OFL — see kernel/font/README.md.)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char *gfx_font;   /* 256 glyphs x 16 bytes */

static int gfx_load_font(void) {
    FILE *f = fopen("/font.psf", "rb");
    if (!f) return -1;
    unsigned char hdr[4];
    if (fread(hdr, 1, 4, f) != 4 || hdr[0] != 0x36 || hdr[1] != 0x04 || hdr[3] != 16) { fclose(f); return -1; }
    gfx_font = malloc(256 * 16);
    size_t n = fread(gfx_font, 1, 256 * 16, f);
    fclose(f);
    return n == 256 * 16 ? 0 : -1;
}

/* One glyph into a 32-bit buffer, clipped to [0,w)x[0,h). bg == 0xFFFFFFFF keeps the old pixels. */
static void gfx_char(uint32_t *buf, int w, int h, int x, int y, unsigned char c, uint32_t fg, uint32_t bg) {
    if (!gfx_font) return;
    for (int j = 0; j < 16; j++) {
        if (y + j < 0 || y + j >= h) continue;
        unsigned char row = gfx_font[c * 16 + j];
        for (int i = 0; i < 8; i++) {
            if (x + i < 0 || x + i >= w) continue;
            if (row & (0x80 >> i)) buf[(size_t)(y + j) * w + x + i] = fg;
            else if (bg != 0xFFFFFFFFu) buf[(size_t)(y + j) * w + x + i] = bg;
        }
    }
}

static void gfx_str(uint32_t *buf, int w, int h, int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    for (; *s; s++, x += 8) gfx_char(buf, w, h, x, y, (unsigned char)*s, fg, bg);
}
