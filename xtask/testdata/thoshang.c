// SPDX-License-Identifier: GPL-2.0-or-later
// thoshang: (root) spin in the kernel for ever, to test the lock-up detector: it reports after 3 s and resets the machine after a minute.
#define THOS_BASE 0x54480000L
int main(void) { long r; __asm__ volatile("syscall" : "=a"(r) : "0"(THOS_BASE + 6), "D"(0L), "S"(0L) : "rcx", "r11", "memory"); return (int)r; }
