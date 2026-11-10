// userenv.dll: just the profile directory.
#include "wincommon.h"
EXPORT BOOL WINAPI GetUserProfileDirectoryW(HANDLE token, u16 *buf, u32 *size) {
    (void)token; static const char *p = "C:\\Users\\thos"; u32 n = (u32)slen(p);
    if (!size || !buf || *size < n + 1) { if (size) *size = n + 1; SetLastError(122); return 0; }
    for (u32 i = 0; i <= n; i++) buf[i] = (u16)p[i];
    *size = n + 1; return 1;
}
__attribute__((dllexport)) int __stdcall DllMain(void *h, unsigned reason, void *res) { (void)h; (void)reason; (void)res; return 1; }
