// api-ms-win-core-synch-l1-2-0.dll: WaitOnAddress / WakeByAddress on top of futexes.
#include "wincommon.h"
typedef struct { i64 s, ns; } ts_t;
static int cmp_eq(const volatile void *a, const void *b, u64 n) {
    const volatile unsigned char *x = a; const unsigned char *y = b;
    for (u64 i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}
EXPORT BOOL WINAPI WaitOnAddress(volatile void *addr, void *undesired, u64 size, u32 ms) {
    ts_t end; sc3(228, 1, &end, 0);
    i64 deadline_ns = (i64)end.s * 1000000000LL + end.ns + (i64)ms * 1000000LL;
    while (cmp_eq(addr, undesired, size)) {
        ts_t now; sc3(228, 1, &now, 0);
        i64 left = deadline_ns - ((i64)now.s * 1000000000LL + now.ns);
        if (ms != 0xFFFFFFFFu && left <= 0) { SetLastError(1460); return 0; }                 /* ERROR_TIMEOUT */
        ts_t to; i64 slice = (ms == 0xFFFFFFFFu || left > 5000000) ? 5000000 : left;           /* re-check every 5 ms at most */
        to.s = slice / 1000000000LL; to.ns = slice % 1000000000LL;
        if (size == 4) sc6(202, (i64)addr, 128 /* FUTEX_WAIT_PRIVATE */, *(volatile u32 *)addr, (i64)&to, 0, 0);
        else sc3(35 /* nanosleep */, &to, 0, 0);
    }
    return 1;
}
EXPORT void WINAPI WakeByAddressSingle(void *addr) { sc6(202, (i64)addr, 129 /* FUTEX_WAKE_PRIVATE */, 1, 0, 0, 0); }
EXPORT void WINAPI WakeByAddressAll(void *addr) { sc6(202, (i64)addr, 129, 0x7fffffff, 0, 0, 0); }
__attribute__((dllexport)) int __stdcall DllMain(void *h, unsigned reason, void *res) { (void)h; (void)reason; (void)res; return 1; }
