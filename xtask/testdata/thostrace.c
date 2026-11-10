// SPDX-License-Identifier: GPL-2.0-or-later
// thostrace PID [off]: log every system call of PID (and the children it forks from now on) to the kernel log, which is on the
// serial line and, with the netconsole, on the network. Root only. `thostrace -c NAME` finds the oldest process called NAME (via /proc).
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define THOS_BASE 0x54480000L
static long thos2(long n, long a, long b) { long r; __asm__ volatile("syscall" : "=a"(r) : "0"(n), "D"(a), "S"(b) : "rcx", "r11", "memory"); return r; }
int main(int argc, char **argv) {
    if (argc < 2) { fputs("usage: thostrace PID [off] | thostrace -c NAME [off]\n", stderr); return 2; }
    long pid = 0; int off = 0;
    if (!strcmp(argv[1], "-c") && argc >= 3) {
        DIR *d = opendir("/proc"); struct dirent *e;
        while (d && (e = readdir(d))) {
            long p = atol(e->d_name); if (p <= 0) continue;
            char path[64], line[256]; snprintf(path, sizeof path, "/proc/%ld/stat", p);
            FILE *f = fopen(path, "r"); if (!f) continue;
            if (fgets(line, sizeof line, f)) { char *a = strchr(line, '('); char *b = strrchr(line, ')'); if (a && b && (size_t)(b - a - 1) == strlen(argv[2]) && !strncmp(a + 1, argv[2], b - a - 1)) { if (!pid || p < pid) pid = p; } }
            fclose(f);
        }
        off = argc >= 4 && !strcmp(argv[3], "off");
    } else { pid = atol(argv[1]); off = argc >= 3 && !strcmp(argv[2], "off"); }
    if (pid <= 0) { fputs("thostrace: no such process\n", stderr); return 1; }
    long r = thos2(THOS_BASE + 5, pid, off ? 0 : 1);
    if (r < 0) { fprintf(stderr, "thostrace: failed (%ld)\n", r); return 1; }
    printf("tracing of %ld %s\n", pid, off ? "off" : "on");
    return 0;
}
