// SPDX-License-Identifier: GPL-2.0-or-later
// thosbl [percent]: show or set the screen brightness through the THOS-native backlight() call (ACPI video extension).
#include <stdio.h>
#include <stdlib.h>

#define THOS_BASE 0x54480000L
static long thos2(long n, long a, long b) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(n), "D"(a), "S"(b) : "rcx", "r11", "memory");
    return r;
}
int main(int argc, char **argv) {
    if (argc > 1) {
        long r = thos2(THOS_BASE + 2, 1, atol(argv[1]));
        if (r < 0) { puts("no controllable backlight"); return 1; }
    }
    long p = thos2(THOS_BASE + 2, 0, 0);
    if (p < 0) { puts("no controllable backlight"); return 1; }
    printf("brightness %ld\n", p);
    return 0;
}
