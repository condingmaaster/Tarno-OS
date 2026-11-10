// SPDX-License-Identifier: GPL-2.0
/* integer square root (Newton), same result as Linux lib/math/int_sqrt.c */
#include <linux/kernel.h>
u32 int_sqrt(unsigned long x)
{
	unsigned long b, m, y = 0;
	if (x <= 1) return x;
	m = 1UL << (((sizeof(long) * 8) - 2) & ~1UL);
	while (m != 0) {
		b = y + m;
		y >>= 1;
		if (x >= b) { x -= b; y += m; }
		m >>= 2;
	}
	return y;
}
