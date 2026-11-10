// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
int main(void) {
    int local;
    void *h = malloc(100), *big = malloc(1 << 20);
    void *m = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("asl %p %p %p %p %p\n", (void *)main, h, big, m, (void *)&local);
    return 0;
}
