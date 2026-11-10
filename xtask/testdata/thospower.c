// SPDX-License-Identifier: GPL-2.0-or-later
// thospower [ignore|twice|now]: show or set what a press of the power button does (THOS-native power_button() call).
#include <stdio.h>
#include <string.h>

#define THOS_BASE 0x54480000L
static long thos2(long n, long a, long b) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return r;
}
static const char *names[] = { "ignore", "twice", "now" };
int main(int argc, char **argv) {
    if (argc > 1) {
        long v = -1;
        for (int i = 0; i < 3; i++) if (!strcmp(argv[1], names[i])) v = i;
        if (v < 0 || thos2(THOS_BASE + 4, 1, v) < 0) { puts("usage: thospower [ignore|twice|now]"); return 1; }
    }
    long p = thos2(THOS_BASE + 4, 0, 0);
    printf("power button: %s\n", p >= 0 && p < 3 ? names[p] : "?");
    return 0;
}
