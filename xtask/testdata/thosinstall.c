// SPDX-License-Identifier: GPL-2.0-or-later
// thosinstall: copy THOS from the stick it booted from (/dev/sdb) onto the internal disk (/dev/sda). Runs as root
// (thoselevate). Layout: the MBR, Limine's gap and the boot partition are copied sector for sector; the root ext2 partition
// is copied block by block, but only the blocks its allocation bitmaps say are in use (the rest of a big stick is empty).
//   thosinstall --erase /dev/sda       (the flag is the confirmation the thos-install script collected from the user)
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SEC 512ULL
static int src, dst;
static uint64_t copied_mb;

static int rd(uint64_t off, void *buf, size_t len) { size_t d = 0; while (d < len) { ssize_t n = pread(src, (char *)buf + d, len - d, (off_t)(off + d)); if (n <= 0) return -1; d += (size_t)n; } return 0; }
static int wr(uint64_t off, const void *buf, size_t len) { size_t d = 0; while (d < len) { ssize_t n = pwrite(dst, (const char *)buf + d, len - d, (off_t)(off + d)); if (n <= 0) { fprintf(stderr, "write at %llu failed: %s\n", (unsigned long long)(off + d), strerror(errno)); return -1; } d += (size_t)n; } return 0; }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void progress(int pct) { printf("progress %d\n", pct); fflush(stdout); }

static uint8_t *buf;
#define BUFSZ (1u << 20)
static int copy_range(uint64_t off, uint64_t len) {                       /* raw copy of [off, off+len) */
    while (len) {
        size_t n = len > BUFSZ ? BUFSZ : (size_t)len;
        if (rd(off, buf, n) || wr(off, buf, n)) return -1;
        off += n; len -= n;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], "--erase") || strcmp(argv[2], "/dev/sda")) { fputs("usage: thosinstall --erase /dev/sda\n", stderr); return 2; }
    dup2(1, 2);                                              /* the elevated process may have no stderr: report everything on stdout */
    buf = malloc(BUFSZ);
    src = open("/dev/sdb", O_RDONLY);
    dst = open("/dev/sda", O_RDWR);
    if (!buf || src < 0 || dst < 0) { fprintf(stderr, "cannot open the disks (stick /dev/sdb: %d, internal /dev/sda: %d): %s\n", src, dst, strerror(errno)); return 1; }
    uint8_t mbr[512];
    if (rd(0, mbr, 512) || mbr[510] != 0x55 || mbr[511] != 0xAA) { fputs("the stick has no MBR partition table\n", stderr); return 1; }
    uint64_t pstart[2], pcount[2];
    for (int i = 0; i < 2; i++) { const uint8_t *e = mbr + 0x1BE + 16 * i; pstart[i] = le32(e + 8); pcount[i] = le32(e + 12); }
    if (!pstart[0] || !pstart[1] || pstart[1] < pstart[0] + pcount[0]) { fputs("unexpected stick layout (need boot partition 1 and root partition 2)\n", stderr); return 1; }
    off_t dsz = lseek(dst, 0, SEEK_END);
    if ((uint64_t)dsz < (pstart[1] + pcount[1]) * SEC) { fprintf(stderr, "the internal disk (%llu MiB) is smaller than the system (%llu MiB)\n", (unsigned long long)dsz >> 20, (unsigned long long)((pstart[1] + pcount[1]) * SEC) >> 20); return 1; }
    printf("installing THOS to /dev/sda (%llu MiB): erasing its partition table\n", (unsigned long long)dsz >> 20);
    uint8_t zero[512] = { 0 };
    if (wr(0, zero, 512)) return 1;                                      /* a half-copied disk must not look bootable */
    progress(1);
    /* the gap (Limine's second stage) and the boot partition, sector for sector, MBR last */
    if (copy_range(SEC, (pstart[0] + pcount[0]) * SEC - SEC)) return 1;
    progress(10);
    /* the root partition: only used blocks */
    uint64_t base = pstart[1] * SEC;
    uint8_t sb[1024];
    if (rd(base + 1024, sb, 1024) || le16(sb + 56) != 0xEF53) { fputs("the root partition is not ext2\n", stderr); return 1; }
    uint32_t blocks = le32(sb + 4), first = le32(sb + 20), bs = 1024u << le32(sb + 24), bpg = le32(sb + 32), ipg = le32(sb + 40);
    uint32_t isz = le32(sb + 76) ? le16(sb + 88) : 128, incompat = le32(sb + 96), dsc = (incompat & 0x80) ? le16(sb + 254) : 32;
    if (dsc < 32 || bs > BUFSZ) { fputs("unsupported ext2 layout\n", stderr); return 1; }
    uint32_t groups = (blocks - first + bpg - 1) / bpg, itb = (ipg * isz + bs - 1) / bs;
    uint32_t gdt_blocks = (groups * dsc + bs - 1) / bs;
    uint8_t *gd = malloc((size_t)gdt_blocks * bs), *bm = malloc(bs);
    if (!gd || !bm || rd(base + (uint64_t)(first + 1) * bs, gd, (size_t)gdt_blocks * bs)) { fputs("cannot read the group descriptors\n", stderr); return 1; }
    /* block 0 .. first (boot block, superblock) */
    if (copy_range(base, (uint64_t)(first + 1) * bs)) return 1;
    uint64_t used = 0;
    for (uint32_t g = 0; g < groups; g++) {
        const uint8_t *d = gd + (size_t)g * dsc;
        uint32_t bbm = le32(d), itab = le32(d + 8), flags = le16(d + 18), gfirst = first + g * bpg;
        uint32_t gblocks = (gfirst + bpg > blocks) ? blocks - gfirst : bpg;
        if (flags & 2) {                                                  /* BLOCK_UNINIT: only the group's own metadata is in use */
            uint32_t endb = itab + itb;
            if (endb > gfirst && endb <= gfirst + gblocks) { if (copy_range(base + (uint64_t)gfirst * bs, (uint64_t)(endb - gfirst) * bs)) return 1; used += endb - gfirst; }
        } else {
            if (rd(base + (uint64_t)bbm * bs, bm, bs)) { fputs("cannot read a block bitmap\n", stderr); return 1; }
            uint32_t i = 0;
            while (i < gblocks) {
                if (!(bm[i >> 3] & (1 << (i & 7)))) { i++; continue; }
                uint32_t j = i;
                while (j < gblocks && (bm[j >> 3] & (1 << (j & 7))) && (uint64_t)(j - i + 1) * bs <= BUFSZ) j++;
                if (copy_range(base + (uint64_t)(gfirst + i) * bs, (uint64_t)(j - i) * bs)) return 1;
                used += j - i;
                i = j;
            }
        }
        progress(10 + (int)((uint64_t)(g + 1) * 85 / groups));
    }
    /* last: the MBR, so the disk only becomes bootable once everything else is in place */
    if (wr(0, mbr, 512)) return 1;
    fsync(dst);
    progress(100);
    printf("install done: %llu MiB of system copied\n", (unsigned long long)used * bs >> 20);
    return 0;
}
