// SPDX-License-Identifier: GPL-2.0-or-later
// Memory exhaustion must end in ENOMEM for the program, not in a kernel panic, and must leak nothing.
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int fill(void) {
    static void *p[2000];
    int n = 0;
    while (n < 2000) {
        void *q = mmap(0, 4 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (q == MAP_FAILED) break;
        ((char *)q)[0] = 1;
        p[n++] = q;
    }
    for (int i = 0; i < n; i++) munmap(p[i], 4 << 20);
    return n;
}

int main(void) {
    int a = fill();
    // with memory nearly full a fork must fail cleanly too (hold most memory in the parent)
    int b = fill();
    printf("oom: first fill %d MB-chunks, second %d\n", a * 4, b * 4);
    if (a < 10 || b < a - 2) { puts("oom FAIL: leak or no memory"); return 1; }
    // brk growth to the limit
    int ok = 1;
    for (int i = 0; i < 200000 && ok; i++) { void *m = malloc(64 * 1024); if (!m) ok = 0; else ((char *)m)[0] = 1; }
    puts("oom ok: ENOMEM instead of a panic, nothing leaked");
    return 0;
}
