// SPDX-License-Identifier: GPL-2.0-or-later
// Shared bits of the small freestanding Windows DLLs that talk to the THOS kernel with plain system calls.
#include <stddef.h>
#include <stdint.h>
#define EXPORT __attribute__((dllexport))
#define WINAPI __attribute__((ms_abi))
typedef long long i64;
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef int BOOL;
typedef void *HANDLE;
static inline i64 sc6(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    i64 r;
    register i64 r10 __asm__("r10") = d;
    register i64 r8 __asm__("r8") = e;
    register i64 r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
#define sc3(n, a, b, c) sc6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)
extern void WINAPI SetLastError(u32);
static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void w2a(const u16 *w, char *out, size_t cap) {
    size_t o = 0;
    for (; *w && o + 4 < cap; w++) { u32 c = *w; if (c < 0x80) out[o++] = (char)c; else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 63)); } else { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); } }
    out[o] = 0;
}
static void dos_to_posix(char *out, const char *in, size_t cap) {
    size_t i = 0;
    if (in[0] == '\\' && in[1] == '?' && in[2] == '?' && in[3] == '\\') in += 4;         /* \??\ NT prefix */
    else if (in[0] == '\\' && in[1] == '\\' && in[2] == '?' && in[3] == '\\') in += 4;    /* \\?\ Win32 prefix */
    if (in[0] && in[1] == ':') in += 2;
    for (; *in && i + 1 < cap; in++, i++) out[i] = *in == '\\' ? '/' : *in;
    out[i] = 0;
}
