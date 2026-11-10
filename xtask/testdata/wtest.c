// SPDX-License-Identifier: GPL-2.0-or-later
// A broader mingw/msvcrt program: stdio files, qsort, strtol, snprintf, ctype, rand, time, getenv.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(void) {
    int v[6] = { 5, 3, 9, 1, 7, 2 };
    qsort(v, 6, sizeof v[0], cmp);
    char buf[64];
    snprintf(buf, sizeof buf, "%d-%d-%d", v[0], v[1], v[5]);
    printf("w1 %s %ld %s\n", buf, strtol("0x2a", 0, 16), toupper('q') == 'Q' ? "ctype" : "bad");
    FILE *f = fopen("C:\\wtest.txt", "w");
    if (!f) f = fopen("/tmp/wtest.txt", "w");
    if (f) { fprintf(f, "line-one\nline-two\n"); fclose(f); }
    f = fopen("C:\\wtest.txt", "r");
    if (!f) f = fopen("/tmp/wtest.txt", "r");
    char line[64] = "";
    if (f) { fgets(line, sizeof line, f); fgets(line, sizeof line, f); fclose(f); }
    printf("w2 %s", line);
    srand(1);
    int r = rand();
    printf("w3 rand-%s time-%s\n", r >= 0 ? "ok" : "bad", time(0) > 1700000000 ? "ok" : "bad");
    printf("w4 %5.2f|%-5s|%05d|%x\n", 3.14159, "ab", 42, 255);
    return 0;
}
