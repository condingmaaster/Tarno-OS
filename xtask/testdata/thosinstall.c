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

/* thosinstall --disks: what is on each disk, one line per fact, for the installer to show:
     disk <dev> <size_mb> <table: mbr|gpt|none|unreadable> <boot: yes|no>
     part <dev> <n> <start_mb> <size_mb> <type_hex> <what>            (what: Windows(NTFS) | FAT | Linux(ext2/3/4) | Linux swap | THOS | unknown)
     free <dev> <largest_free_mb> */
static const char *classify(int fd, uint64_t off, uint8_t ptype, char *label, size_t cap) {
    uint8_t b[4096];
    label[0] = 0;
    if (ptype == 0x82) return "Linux swap";
    if (ptype == 0x05 || ptype == 0x0F || ptype == 0x85) return "extended partition";
    if (pread(fd, b, sizeof b, (off_t)off) != (ssize_t)sizeof b) return "unreadable";
    if (!memcmp(b + 3, "NTFS    ", 8)) return "Windows (NTFS)";
    if (!memcmp(b + 82, "FAT32   ", 8)) { snprintf(label, cap, "%.11s", (char *)b + 71); for (int i = (int)strlen(label) - 1; i >= 0 && label[i] == ' '; i--) label[i] = 0; return "FAT/exFAT"; }
    if (!memcmp(b + 54, "FAT16   ", 8) || !memcmp(b + 54, "FAT12   ", 8) || !memcmp(b + 3, "EXFAT   ", 8)) return "FAT/exFAT";
    uint8_t sb[1024];
    if (pread(fd, sb, sizeof sb, (off_t)(off + 1024)) == (ssize_t)sizeof sb && le16(sb + 56) == 0xEF53) {
        snprintf(label, cap, "%.16s", (char *)sb + 120);
        if (!strncmp(label, "THOSROOT", 8)) return "THOS";
        uint32_t incompat = le32(sb + 96), ro = le32(sb + 100);
        return (incompat & 0x40) || (ro & 0x40) ? "Linux (ext4)" : "Linux (ext2/3)";
    }
    if (!memcmp(b + 4086, "SWAPSPACE2", 10)) return "Linux swap";
    if (ptype == 0x83) return "Linux (unknown file system)";
    return "unknown";
}
static int list_disks(void) {
    const char *devs[] = { "/dev/sda", "/dev/sdb", "/dev/ram0" };
    char bootd[16] = "";
    FILE *bf = fopen("/proc/bootdisk", "r");
    if (bf) { if (fgets(bootd, sizeof bootd, bf)) {} fclose(bf); }
    for (int d = 0; d < 3; d++) {
        int fd = open(devs[d], O_RDONLY);
        if (fd < 0) continue;
        off_t size = lseek(fd, 0, SEEK_END);
        uint8_t mbr[512], gpt[512];
        const char *table = "none";
        int ok = pread(fd, mbr, 512, 0) == 512;
        if (!ok) { table = "unreadable"; fprintf(stderr, "cannot read the first sector of %s: %s\n", devs[d], strerror(errno)); }
        else if (mbr[510] == 0x55 && mbr[511] == 0xAA) table = "mbr";
        if (ok && pread(fd, gpt, 512, 512) == 512 && !memcmp(gpt, "EFI PART", 8)) table = "gpt";
        int thosboot = 0;                                          /* the stick THOS started from: it carries the FAT partition THOSBOOT (the memory-image start never sees it as the boot disk otherwise) */
        if (!strcmp(table, "mbr")) for (int i = 0; i < 4; i++) {
            const uint8_t *e = mbr + 0x1BE + 16 * i;
            char lb[20];
            if (e[4] && le32(e + 12) && !strcmp(classify(fd, (uint64_t)le32(e + 8) * SEC, e[4], lb, sizeof lb), "FAT/exFAT") && !strcmp(lb, "THOSBOOT")) thosboot = 1;
        }
        int isboot = thosboot || (d == 1 && !strncmp(bootd, "usb", 3)) || (d == 0 && !strncmp(bootd, "sata", 4)) || (d == 2 && !strncmp(bootd, "ram", 3));
        printf("disk %s %llu %s %s\n", devs[d], (unsigned long long)size >> 20, table, isboot ? "yes" : "no");
        uint64_t used_end = 2048, best = 0, total = (uint64_t)size / SEC;
        if (!strcmp(table, "mbr")) {
            struct { uint64_t s, n; } pp[4]; int np = 0;
            for (int i = 0; i < 4; i++) {
                const uint8_t *e = mbr + 0x1BE + 16 * i;
                uint64_t st = le32(e + 8), cnt = le32(e + 12);
                if (!e[4] || !cnt) continue;
                char label[20];
                const char *what = classify(fd, st * SEC, e[4], label, sizeof label);
                printf("part %s %d %llu %llu %02x %s%s%s%s\n", devs[d], i + 1, (unsigned long long)(st * SEC) >> 20, (unsigned long long)(cnt * SEC) >> 20, e[4], what, label[0] ? " '" : "", label, label[0] ? "'" : "");
                pp[np].s = st; pp[np++].n = cnt;
            }
            uint64_t cur = 2048;
            for (int i = 0; i < np; i++) { /* simple sweep over the sorted entries */
                for (int j = i; j < np; j++) if (pp[j].s < pp[i].s) { uint64_t ts = pp[i].s, tn = pp[i].n; pp[i].s = pp[j].s; pp[i].n = pp[j].n; pp[j].s = ts; pp[j].n = tn; }
                if (pp[i].s > cur && pp[i].s - cur > best) best = pp[i].s - cur;
                if (pp[i].s + pp[i].n > cur) cur = pp[i].s + pp[i].n;
            }
            if (total > cur && total - cur > best) best = total - cur;
            used_end = cur;
        } else if (!strcmp(table, "gpt")) { printf("part %s - 0 0 ee GPT partition table (UEFI style, not handled by this installer)\n", devs[d]); }
        else if (!strcmp(table, "none")) best = total - used_end;
        printf("free %s %llu\n", devs[d], (unsigned long long)(best * SEC) >> 20);
        close(fd);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--disks")) return list_disks();
    if (argc != 3 || strcmp(argv[1], "--erase") || strcmp(argv[2], "/dev/sda")) { fputs("usage: thosinstall --erase /dev/sda\n", stderr); return 2; }
    dup2(1, 2);                                              /* the elevated process may have no stderr: report everything on stdout */
    buf = malloc(BUFSZ);
    { char bd[16] = ""; FILE *bf = fopen("/proc/bootdisk", "r"); if (bf) { if (fgets(bd, sizeof bd, bf)) {} fclose(bf); }
      src = open(!strncmp(bd, "ram", 3) ? "/dev/ram0" : "/dev/sdb", O_RDONLY); }          /* from memory when the system runs from a loaded image */
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
