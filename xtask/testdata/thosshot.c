// SPDX-License-Identifier: GPL-2.0-or-later
// thosshot [file.png]: a screenshot of the whole screen (the framebuffer, i.e. what the desktop composed), as a PNG.
// Without a file name it writes /tmp/shot-<seconds>.png and prints the name. The size comes from the framebuffer
// itself (FBIOGET_VSCREENINFO), the same numbers /proc/screen and the Wayland wl_output report.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static uint32_t crc_tab[256];
static uint32_t crc_upd(uint32_t c, const unsigned char *p, size_t n) {
    if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; crc_tab[i] = k; }
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 255] ^ (c >> 8);
    return c;
}
static void be32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len) {
    unsigned char h[8]; be32(h, len); memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    uint32_t c = crc_upd(0xFFFFFFFFu, (const unsigned char *)type, 4);
    if (len) { fwrite(data, 1, len, f); c = crc_upd(c, data, len); }
    unsigned char t[4]; be32(t, c ^ 0xFFFFFFFFu); fwrite(t, 1, 4, f);
}

int main(int argc, char **argv) {
    int fb = open("/dev/fb0", O_RDONLY);
    if (fb < 0) { fputs("thosshot: no framebuffer\n", stderr); return 1; }
    unsigned char var[160], fix[80];
    if (ioctl(fb, 0x4600, var) != 0 || ioctl(fb, 0x4602, fix) != 0) { fputs("thosshot: cannot read the screen geometry\n", stderr); return 1; }
    uint32_t W, H, bpp, ro, go, bo, pitch;
    memcpy(&W, var, 4); memcpy(&H, var + 4, 4); memcpy(&bpp, var + 24, 4);
    memcpy(&ro, var + 32, 4); memcpy(&go, var + 44, 4); memcpy(&bo, var + 56, 4); memcpy(&pitch, fix + 48, 4);
    if (bpp != 32 || !W || !H || pitch < W * 4) { fputs("thosshot: unsupported screen format\n", stderr); return 1; }
    unsigned char *shot = malloc((size_t)pitch * H);
    if (!shot) return 1;
    for (size_t got = 0, want = (size_t)pitch * H; got < want;) {
        ssize_t n = read(fb, shot + got, want - got);   /* sequential read: the framebuffer node has no pread */
        if (n <= 0) { fputs("thosshot: reading the framebuffer failed\n", stderr); return 1; }
        got += (size_t)n;
    }
    char path[64];
    if (argc > 1) snprintf(path, sizeof path, "%s", argv[1]); else snprintf(path, sizeof path, "/tmp/shot-%ld.png", (long)time(0));
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "thosshot: cannot write %s\n", path); return 1; }
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13] = { 0 }; be32(ihdr, W); be32(ihdr + 4, H); ihdr[8] = 8; ihdr[9] = 2;
    chunk(f, "IHDR", ihdr, 13);
    size_t raw = (size_t)H * ((size_t)W * 3 + 1), nblk = (raw + 65534) / 65535, idat = 2 + raw + 5 * nblk + 4;
    unsigned char hdr[8]; be32(hdr, (uint32_t)idat); memcpy(hdr + 4, "IDAT", 4);
    fwrite(hdr, 1, 8, f);
    uint32_t c = crc_upd(0xFFFFFFFFu, (const unsigned char *)"IDAT", 4);
    unsigned char z[2] = { 0x78, 0x01 }; fwrite(z, 1, 2, f); c = crc_upd(c, z, 2);
    static unsigned char blk[65535 + 5];
    uint32_t a = 1, b = 0; size_t done = 0, row = 0, col = 0;               /* col 0: the filter byte of a row, then 3 bytes per pixel */
    while (done < raw) {
        size_t want = raw - done < 65535 ? raw - done : 65535;
        for (size_t i = 0; i < want; i++) {
            unsigned char v;
            if (col == 0) { v = 0; col = 1; }
            else {
                const unsigned char *px = shot + row * pitch + ((col - 1) / 3) * 4;
                int ch = (int)((col - 1) % 3);
                v = px[(ch == 0 ? ro : ch == 1 ? go : bo) / 8];
                if (++col - 1 == (size_t)W * 3) { col = 0; row++; }
            }
            blk[5 + i] = v; a = (a + v) % 65521; b = (b + a) % 65521;
        }
        blk[0] = done + want == raw; blk[1] = want; blk[2] = want >> 8; blk[3] = ~want; blk[4] = ~want >> 8;
        fwrite(blk, 1, want + 5, f); c = crc_upd(c, blk, want + 5);
        done += want;
    }
    unsigned char ad[4]; be32(ad, (b << 16) | a); fwrite(ad, 1, 4, f); c = crc_upd(c, ad, 4);
    unsigned char t[4]; be32(t, c ^ 0xFFFFFFFFu); fwrite(t, 1, 4, f);
    chunk(f, "IEND", 0, 0);
    fclose(f);
    printf("%s (%ux%u)\n", path, W, H);
    return 0;
}
