// bcryptprimitives.dll: ProcessPrng, the random source the Rust standard library asks for.
#include "wincommon.h"
EXPORT BOOL WINAPI ProcessPrng(unsigned char *buf, u64 len) {
    while (len) { i64 n = sc3(318 /* getrandom */, buf, len > 256 ? 256 : len, 0); if (n <= 0) return 0; buf += n; len -= (u64)n; }
    return 1;
}
__attribute__((dllexport)) int __stdcall DllMain(void *h, unsigned reason, void *res) { (void)h; (void)reason; (void)res; return 1; }
