// SPDX-License-Identifier: GPL-2.0-or-later
// thos-fatput <fat-device> <path-in-the-volume> <local-file>: replace one file in a FAT32 volume (the boot partition: boot/thos-kernel).
// Order of writes keeps the old file valid until the very end: the new data goes into freshly allocated clusters, then the directory entry
// is switched to the new chain in one sector write, and only then is the old chain freed. A power cut leaves either the old or the new file.
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fd;
static uint32_t bps, spc, rsvd, nfats, fatsz, root_cl, total_clusters;
static uint64_t fat_off, data_off;
static uint32_t *fat;                       /* the first FAT in memory */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static int rd(uint64_t off, void *b, size_t n) { return pread(fd, b, n, (off_t)off) == (ssize_t)n ? 0 : -1; }
static int wr(uint64_t off, const void *b, size_t n) { return pwrite(fd, b, n, (off_t)off) == (ssize_t)n ? 0 : -1; }
static uint64_t cl_off(uint32_t c) { return data_off + (uint64_t)(c - 2) * spc * bps; }
static uint32_t fat_get(uint32_t c) { return fat[c] & 0x0FFFFFFF; }

static int fat_flush_range(uint32_t from, uint32_t to) {            /* write the FAT sectors that hold entries [from, to] to every FAT copy */
    uint32_t s0 = from * 4 / bps, s1 = to * 4 / bps;
    for (uint32_t f = 0; f < nfats; f++)
        if (wr(fat_off + (uint64_t)f * fatsz * bps + (uint64_t)s0 * bps, (uint8_t *)fat + (uint64_t)s0 * bps, (size_t)(s1 - s0 + 1) * bps)) return -1;
    return 0;
}

struct ent { uint64_t sector_off; int idx; uint8_t raw[32]; };

/* find `name` (case-insensitive) in the directory starting at cluster `dir`; fills the 8.3 entry's position */
static int find_in_dir(uint32_t dir, const char *name, struct ent *out) {
    char lfn[300] = "";
    uint8_t *buf = malloc(spc * bps);
    for (uint32_t c = dir; c >= 2 && c < 0x0FFFFFF8; c = fat_get(c)) {
        if (rd(cl_off(c), buf, spc * bps)) { free(buf); return -1; }
        for (uint32_t i = 0; i < spc * bps / 32; i++) {
            uint8_t *e = buf + i * 32;
            if (e[0] == 0) { free(buf); return -1; }
            if (e[0] == 0xE5) { lfn[0] = 0; continue; }
            if (e[11] == 0x0F) {                                  /* a long-name part: 13 UTF-16 characters, collected in reverse order */
                int seq = e[0] & 0x1F;
                char part[14]; int k = 0;
                static const int pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                for (int j = 0; j < 13; j++) { uint16_t ch = le16(e + pos[j]); if (ch == 0 || ch == 0xFFFF) break; part[k++] = ch < 128 ? (char)ch : '?'; }
                part[k] = 0;
                if (e[0] & 0x40) memset(lfn, 0, sizeof lfn);               /* the last part (highest number) comes first: start a new name */
                size_t base = (size_t)(seq - 1) * 13;
                if (base + k < sizeof lfn) for (int j = 0; j < k; j++) lfn[base + j] = part[j];
                continue;
            }
            char sn[13]; int n = 0;
            for (int j = 0; j < 8 && e[j] != ' '; j++) sn[n++] = (char)tolower(e[j]);
            if (e[8] != ' ') { sn[n++] = '.'; for (int j = 8; j < 11 && e[j] != ' '; j++) sn[n++] = (char)tolower(e[j]); }
            sn[n] = 0;
            int match = (lfn[0] && !strcasecmp(lfn, name)) || !strcasecmp(sn, name);
            lfn[0] = 0;
            if (match && !(e[11] & 0x08)) {
                out->sector_off = cl_off(c) + (uint64_t)(i * 32 / bps) * bps;
                out->idx = (int)(i * 32 % bps / 32);
                memcpy(out->raw, e, 32);
                free(buf);
                return 0;
            }
        }
    }
    free(buf);
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 4) { fputs("usage: thos-fatput <fat-device> <path> <file>\n", stderr); return 2; }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror(argv[1]); return 1; }
    uint8_t bs[512];
    if (rd(0, bs, 512) || bs[510] != 0x55 || bs[511] != 0xAA || memcmp(bs + 82, "FAT32   ", 8)) { fputs("not a FAT32 volume\n", stderr); return 1; }
    bps = le16(bs + 11); spc = bs[13]; rsvd = le16(bs + 14); nfats = bs[16]; fatsz = le32(bs + 36); root_cl = le32(bs + 44);
    uint32_t tot = le32(bs + 32);
    fat_off = (uint64_t)rsvd * bps;
    data_off = fat_off + (uint64_t)nfats * fatsz * bps;
    total_clusters = (tot - rsvd - nfats * fatsz) / spc;
    fat = malloc((size_t)fatsz * bps);
    if (!fat || rd(fat_off, fat, (size_t)fatsz * bps)) { fputs("cannot read the FAT\n", stderr); return 1; }
    FILE *lf = fopen(argv[3], "rb");
    if (!lf) { perror(argv[3]); return 1; }
    fseek(lf, 0, SEEK_END);
    long size = ftell(lf);
    fseek(lf, 0, SEEK_SET);
    uint32_t cbytes = spc * bps, need = (uint32_t)((size + cbytes - 1) / cbytes);

    /* walk the path to the file's directory */
    char path[256]; snprintf(path, sizeof path, "%s", argv[2]);
    uint32_t dir = root_cl;
    struct ent e;
    char *save = 0, *part = strtok_r(path, "/", &save), *next;
    while (part) {
        next = strtok_r(0, "/", &save);
        if (find_in_dir(dir, part, &e)) { fprintf(stderr, "%s not found in the volume\n", part); return 1; }
        uint32_t cl = (uint32_t)le16(e.raw + 20) << 16 | le16(e.raw + 26);
        if (!next) break;
        if (!(e.raw[11] & 0x10)) { fprintf(stderr, "%s is not a directory\n", part); return 1; }
        dir = cl;
        part = next;
    }
    if (e.raw[11] & 0x10) { fputs("the target is a directory\n", stderr); return 1; }
    uint32_t old_first = (uint32_t)le16(e.raw + 20) << 16 | le16(e.raw + 26);

    /* allocate: a contiguous run if there is one, else any free clusters (new chain only; the old chain stays untouched until the switch) */
    uint32_t *chain = malloc(sizeof(uint32_t) * (need ? need : 1));
    uint32_t got = 0, run_start = 0, run_len = 0;
    for (uint32_t c = 2; c < total_clusters + 2 && got < need; c++) {
        if (fat_get(c) == 0) { if (run_len == 0) run_start = c; if (++run_len == need) { for (uint32_t k = 0; k < need; k++) chain[k] = run_start + k; got = need; } }
        else run_len = 0;
    }
    if (got < need) {                                                 /* fragmented: take whatever is free */
        got = 0;
        for (uint32_t c = 2; c < total_clusters + 2 && got < need; c++) if (fat_get(c) == 0) chain[got++] = c;
    }
    if (got < need) { fputs("not enough free space in the boot partition\n", stderr); return 1; }
    uint8_t *cb = malloc(cbytes);
    for (uint32_t k = 0; k < need; k++) {
        memset(cb, 0, cbytes);
        size_t n = fread(cb, 1, cbytes, lf);
        (void)n;
        if (wr(cl_off(chain[k]), cb, cbytes)) { perror("write"); return 1; }
    }
    for (uint32_t k = 0; k < need; k++) fat[chain[k]] = k + 1 < need ? chain[k + 1] : 0x0FFFFFFF;       /* the new chain is linked ... */
    for (uint32_t k = 0; k < need; k++) if (fat_flush_range(chain[k], chain[k])) { perror("fat"); return 1; }
    fsync(fd);
    /* ... then the directory entry points at it (one sector write: the switch) ... */
    uint8_t sec[4096];
    if (bps > sizeof sec || rd(e.sector_off, sec, bps)) { fputs("cannot read the directory\n", stderr); return 1; }
    put16(sec + e.idx * 32 + 20, (uint16_t)(chain[0] >> 16));
    put16(sec + e.idx * 32 + 26, (uint16_t)chain[0]);
    put32(sec + e.idx * 32 + 28, (uint32_t)size);
    if (wr(e.sector_off, sec, bps)) { perror("directory"); return 1; }
    fsync(fd);
    /* ... and only now the old chain is freed */
    uint32_t freed = 0;
    for (uint32_t c = old_first; c >= 2 && c < 0x0FFFFFF8 && freed < total_clusters; ) {
        uint32_t nx = fat_get(c);
        fat[c] = 0; fat_flush_range(c, c);
        c = nx; freed++;
    }
    fsync(fd);
    printf("replaced %s: %ld bytes in %u clusters (old chain of %u freed)\n", argv[2], size, need, freed);
    return 0;
}
