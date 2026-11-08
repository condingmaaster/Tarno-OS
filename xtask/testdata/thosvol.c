// SPDX-License-Identifier: GPL-2.0-or-later
// thosvol [percent]: show or set the master volume through the THOS-native volume() call (HD Audio output amplifiers).
#include <stdio.h>
#include <stdlib.h>

#define THOS_BASE 0x54480000L
static long thos2(long n, long a, long b) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return r;
}
int main(int argc, char **argv) {
    if (argc > 1 && thos2(THOS_BASE + 3, 1, atol(argv[1])) < 0) { puts("no audio output"); return 1; }
    long p = thos2(THOS_BASE + 3, 0, 0);
    if (p < 0) { puts("no audio output"); return 1; }
    printf("volume %ld\n", p);
    return 0;
}
