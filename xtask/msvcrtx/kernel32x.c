// SPDX-License-Identifier: GPL-2.0-or-later
// kernel32x.dll — more Win32 base API for THOS's NT personality, written in C on top of plain system
// calls. The PE loader asks this DLL before the kernel's built-in kernel32 (which knows ~30 calls) for
// everything a program imports from kernel32.dll. HANDLEs from the built-in CreateFileA are POSIX
// descriptors, so the file calls here work on them directly.
//
// Build: x86_64-w64-mingw32-gcc -O2 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -shared
#include <stddef.h>
#include <stdint.h>

#define EXPORT __attribute__((dllexport))
#define WINAPI __attribute__((ms_abi))
typedef long long i64;
typedef unsigned long long u64;
typedef unsigned int u32;
typedef int BOOL;
typedef void *HANDLE;
#define INVALID_HANDLE ((HANDLE)(i64)-1)

static inline i64 sc6(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    i64 r;
    register i64 r10 __asm__("r10") = d;
    register i64 r8 __asm__("r8") = e;
    register i64 r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
#define sc3(n, a, b, c) sc6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)
enum { S_READ = 0, S_WRITE = 1, S_OPEN = 2, S_CLOSE = 3, S_LSEEK = 8, S_MMAP = 9, S_MUNMAP = 11, S_GETPID = 39, S_RENAME = 82, S_MKDIR = 83, S_RMDIR = 84,
       S_UNLINK = 87, S_GETCWD = 79, S_CHDIR = 80, S_CLOCK_GETTIME = 228, S_GETDENTS64 = 217, S_NEWFSTATAT = 262, S_NANOSLEEP = 35, S_FSYNC = 74 };

extern void WINAPI SetLastError(u32);       /* the kernel's own last-error slot, so GetLastError keeps working */
static i64 ret(i64 r) { if (r < 0 && r > -4096) { SetLastError(r == -2 ? 2 : r == -17 ? 80 : r == -13 ? 5 : r == -21 ? 5 : 1); return -1; } return r; }

static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static void cpy(char *d, const char *s, size_t cap) { size_t i = 0; for (; s[i] && i + 1 < cap; i++) d[i] = s[i]; d[i] = 0; }
static void dos_path(char *out, const char *in, size_t cap) {
    size_t i = 0;
    if (in[0] && in[1] == ':') in += 2;
    for (; *in && i + 1 < cap; in++, i++) out[i] = *in == '\\' ? '/' : *in;
    out[i] = 0;
}

/* ---- time ---- */
typedef struct { i64 s, ns; } ts_t;
EXPORT u32 WINAPI GetTickCount(void) { ts_t t; sc3(S_CLOCK_GETTIME, 1, &t, 0); return (u32)(t.s * 1000 + t.ns / 1000000); }
EXPORT u64 WINAPI GetTickCount64(void) { ts_t t; sc3(S_CLOCK_GETTIME, 1, &t, 0); return (u64)(t.s * 1000 + t.ns / 1000000); }
EXPORT BOOL WINAPI QueryPerformanceCounter(i64 *c) { ts_t t; sc3(S_CLOCK_GETTIME, 1, &t, 0); *c = t.s * 1000000000LL + t.ns; return 1; }
EXPORT BOOL WINAPI QueryPerformanceFrequency(i64 *f) { *f = 1000000000LL; return 1; }
EXPORT void WINAPI GetSystemTimeAsFileTime(u64 *ft) { ts_t t; sc3(S_CLOCK_GETTIME, 0, &t, 0); *ft = ((u64)t.s + 11644473600ULL) * 10000000ULL + (u64)t.ns / 100; }
typedef struct { unsigned short y, mo, dow, d, h, mi, s, ms; } SYSTEMTIME;
static void civil(i64 days, int *y, int *m, int *d) {
    days += 719468;
    i64 era = (days >= 0 ? days : days - 146096) / 146097;
    unsigned doe = (unsigned)(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    i64 yy = (i64)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}
static void fill_st(SYSTEMTIME *st) {
    ts_t t; sc3(S_CLOCK_GETTIME, 0, &t, 0);
    i64 days = t.s / 86400, rem = t.s % 86400;
    int y, m, d; civil(days, &y, &m, &d);
    st->y = (unsigned short)y; st->mo = (unsigned short)m; st->d = (unsigned short)d;
    st->dow = (unsigned short)((days + 4) % 7);
    st->h = (unsigned short)(rem / 3600); st->mi = (unsigned short)(rem % 3600 / 60); st->s = (unsigned short)(rem % 60);
    st->ms = (unsigned short)(t.ns / 1000000);
}
EXPORT void WINAPI GetSystemTime(SYSTEMTIME *st) { fill_st(st); }
EXPORT void WINAPI GetLocalTime(SYSTEMTIME *st) { fill_st(st); }

/* ---- process / system ---- */
EXPORT u32 WINAPI GetCurrentProcessId(void) { return (u32)sc3(S_GETPID, 0, 0, 0); }
EXPORT u32 WINAPI GetCurrentThreadId(void) { return (u32)sc3(S_GETPID, 0, 0, 0); }
EXPORT HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)(i64)-1; }
EXPORT u32 WINAPI GetVersion(void) { return 0x0A00 | (10u << 0); }
typedef struct { u32 size, major, minor, build, platform; char csd[128]; } OSVERSIONINFOA;
EXPORT BOOL WINAPI GetVersionExA(OSVERSIONINFOA *v) { v->major = 10; v->minor = 0; v->build = 19045; v->platform = 2; v->csd[0] = 0; return 1; }
typedef struct { unsigned short arch, res; u32 page, min, max_dummy; } SI_HEAD;
EXPORT void WINAPI GetSystemInfo(void *p) {
    unsigned char *b = p; for (int i = 0; i < 48; i++) b[i] = 0;
    *(unsigned short *)b = 9;           /* PROCESSOR_ARCHITECTURE_AMD64 */
    *(u32 *)(b + 4) = 4096;             /* page size */
    *(u64 *)(b + 8) = 0x10000;          /* minimum application address */
    *(u64 *)(b + 16) = 0x00007FFFFFFEFFFFULL;
    *(u64 *)(b + 24) = 1;               /* active processor mask */
    *(u32 *)(b + 32) = 1;               /* number of processors */
    *(u32 *)(b + 36) = 8664;
    *(u32 *)(b + 44) = 65536;           /* allocation granularity */
}
EXPORT u32 WINAPI GetEnvironmentVariableA(const char *n, char *buf, u32 size) { (void)n; (void)buf; (void)size; SetLastError(203); return 0; }
EXPORT BOOL WINAPI SetEnvironmentVariableA(const char *n, const char *v) { (void)n; (void)v; return 1; }
EXPORT u32 WINAPI GetCurrentDirectoryA(u32 size, char *buf) {
    char p[512];
    i64 n = sc3(S_GETCWD, p, sizeof p, 0);
    if (n <= 0) return 0;
    u32 len = (u32)slen(p);
    if (len + 3 >= size) return len + 3;
    buf[0] = 'C'; buf[1] = ':';
    for (u32 i = 0; i <= len; i++) buf[2 + i] = p[i] == '/' ? '\\' : p[i];
    if (len == 1 && p[0] == '/') { buf[2] = '\\'; buf[3] = 0; return 3; }
    return len + 2;
}
EXPORT BOOL WINAPI SetCurrentDirectoryA(const char *d) { char p[512]; dos_path(p, d, sizeof p); return sc3(S_CHDIR, p, 0, 0) == 0; }
EXPORT u32 WINAPI GetModuleFileNameA(HANDLE m, char *buf, u32 size) { (void)m; const char *n = "C:\\program.exe"; cpy(buf, n, size); return (u32)slen(buf); }

/* ---- strings ---- */
EXPORT int WINAPI lstrlenA(const char *s) { return (int)slen(s); }
EXPORT char *WINAPI lstrcpyA(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
EXPORT int WINAPI lstrcmpA(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
static int lw(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
EXPORT int WINAPI lstrcmpiA(const char *a, const char *b) { while (*a && lw((unsigned char)*a) == lw((unsigned char)*b)) { a++; b++; } return lw((unsigned char)*a) - lw((unsigned char)*b); }

/* ---- files ---- */
/* CreateFileA with every disposition (the kernel's built-in one only opens existing files). */
/* The kernel's built-in CreateFileA (CD-ROM / FAT drives, \\Device\\ names): found by walking the export
   directory of the built-in kernel32 module, since importing it by name would find this very function. */
extern HANDLE WINAPI GetModuleHandleA(const char *);
typedef HANDLE (WINAPI *create_file_t)(const char *, u32, u32, void *, u32, u32, HANDLE);
static create_file_t real_create_file(void) {
    static create_file_t fn;
    if (fn) return fn;
    unsigned char *base = GetModuleHandleA("kernel32.dll");
    if (!base) return 0;
    u32 pe = *(u32 *)(base + 0x3C);
    u32 exp = *(u32 *)(base + pe + 0x88);
    unsigned char *ed = base + exp;
    u32 n = *(u32 *)(ed + 0x18), *funcs = (u32 *)(base + *(u32 *)(ed + 0x1C)), *names = (u32 *)(base + *(u32 *)(ed + 0x20));
    unsigned short *ords = (unsigned short *)(base + *(u32 *)(ed + 0x24));
    for (u32 i = 0; i < n; i++) {
        const char *nm = (const char *)(base + names[i]);
        const char *want = "CreateFileA"; int k = 0;
        while (nm[k] && nm[k] == want[k]) k++;
        if (!nm[k] && !want[k]) { fn = (create_file_t)(base + funcs[ords[i]]); return fn; }
    }
    return 0;
}
EXPORT HANDLE WINAPI CreateFileA(const char *name, u32 access, u32 share, void *sa, u32 disp, u32 flags, HANDLE tmpl) {
    /* other drives and device names belong to the kernel's own implementation */
    if ((name[0] && name[1] == ':' && lw((unsigned char)name[0]) != 'c') || name[0] == '\\') {
        create_file_t rf = real_create_file();
        if (rf) return rf(name, access, share, sa, disp, flags, tmpl);
    }
    char p[512];
    if (name[0] == 'N' && name[1] == 'U' && name[2] == 'L' && !name[3]) cpy(p, "/dev/null", sizeof p); else dos_path(p, name, sizeof p);
    int rd = (access & 0x80000000u) != 0, wr = (access & 0x40000000u) != 0;
    int fl = wr ? (rd ? 2 : 1) : 0;
    switch (disp) {
        case 1: fl |= 0100 | 0200; break;                 /* CREATE_NEW */
        case 2: fl |= 0100 | 01000; break;                /* CREATE_ALWAYS */
        case 3: break;                                    /* OPEN_EXISTING */
        case 4: fl |= 0100; break;                        /* OPEN_ALWAYS */
        case 5: fl |= 01000; if (!wr) fl |= 1; break;     /* TRUNCATE_EXISTING */
        default: SetLastError(87); return INVALID_HANDLE;
    }
    i64 fd = sc3(S_OPEN, p, fl, 0644);
    if (fd < 0) { SetLastError(fd == -2 ? 2 : fd == -17 ? 80 : fd == -13 ? 5 : 3); return INVALID_HANDLE; }
    return (HANDLE)fd;
}
EXPORT u32 WINAPI SetFilePointer(HANDLE h, int lo, int *hi, u32 method) {
    i64 off = lo; if (hi) off |= (i64)*hi << 32;
    i64 r = sc3(S_LSEEK, (i64)h, off, method);
    if (r < 0) { SetLastError(87); return 0xFFFFFFFFu; }
    if (hi) *hi = (int)(r >> 32);
    return (u32)r;
}
EXPORT BOOL WINAPI SetFilePointerEx(HANDLE h, i64 off, i64 *newpos, u32 method) {
    i64 r = sc3(S_LSEEK, (i64)h, off, method);
    if (r < 0) return 0;
    if (newpos) *newpos = r;
    return 1;
}
EXPORT BOOL WINAPI GetFileSizeEx(HANDLE h, i64 *sz) {
    i64 cur = sc3(S_LSEEK, (i64)h, 0, 1);
    i64 end = sc3(S_LSEEK, (i64)h, 0, 2);
    if (cur < 0 || end < 0) return 0;
    sc3(S_LSEEK, (i64)h, cur, 0);
    *sz = end;
    return 1;
}
EXPORT u32 WINAPI GetFileSize(HANDLE h, u32 *hi) {
    i64 sz;
    if (!GetFileSizeEx(h, &sz)) return 0xFFFFFFFFu;
    if (hi) *hi = (u32)(sz >> 32);
    return (u32)sz;
}
EXPORT BOOL WINAPI FlushFileBuffers(HANDLE h) { sc3(S_FSYNC, (i64)h, 0, 0); return 1; }
EXPORT BOOL WINAPI CreateDirectoryA(const char *p, void *sa) { (void)sa; char q[512]; dos_path(q, p, sizeof q); return ret(sc3(S_MKDIR, q, 0755, 0)) == 0; }
EXPORT BOOL WINAPI RemoveDirectoryA(const char *p) { char q[512]; dos_path(q, p, sizeof q); return ret(sc3(S_RMDIR, q, 0, 0)) == 0; }
EXPORT BOOL WINAPI DeleteFileA(const char *p) { char q[512]; dos_path(q, p, sizeof q); return ret(sc3(S_UNLINK, q, 0, 0)) == 0; }
EXPORT BOOL WINAPI MoveFileA(const char *a, const char *b) { char x[512], y[512]; dos_path(x, a, sizeof x); dos_path(y, b, sizeof y); return ret(sc3(S_RENAME, x, y, 0)) == 0; }
EXPORT BOOL WINAPI MoveFileExA(const char *a, const char *b, u32 flags) { (void)flags; return MoveFileA(a, b); }
EXPORT BOOL WINAPI CopyFileA(const char *a, const char *b, BOOL fail_if_exists) {
    char x[512], y[512]; dos_path(x, a, sizeof x); dos_path(y, b, sizeof y);
    i64 in = sc3(S_OPEN, x, 0, 0);
    if (in < 0) { SetLastError(2); return 0; }
    i64 out = sc3(S_OPEN, y, 1 | 0100 | (fail_if_exists ? 0200 : 01000), 0644);
    if (out < 0) { sc3(S_CLOSE, in, 0, 0); SetLastError(fail_if_exists ? 80 : 5); return 0; }
    char buf[4096]; i64 n;
    while ((n = sc3(S_READ, in, buf, sizeof buf)) > 0) { i64 off = 0; while (off < n) { i64 w = sc3(S_WRITE, out, buf + off, n - off); if (w <= 0) goto done; off += w; } }
done:
    sc3(S_CLOSE, in, 0, 0); sc3(S_CLOSE, out, 0, 0);
    return 1;
}
static int stat_path(const char *dos, unsigned char *st) {
    char q[512]; dos_path(q, dos, sizeof q);
    return (int)sc6(S_NEWFSTATAT, -100, (i64)q, (i64)st, 0, 0, 0);
}
EXPORT u32 WINAPI GetFileAttributesA(const char *p) {
    unsigned char st[144];
    if (stat_path(p, st) < 0) { SetLastError(2); return 0xFFFFFFFFu; }
    u32 mode = *(u32 *)(st + 24);
    return (mode & 0xF000) == 0x4000 ? 0x10 : 0x80;
}

/* FindFirstFileA / FindNextFileA / FindClose */
typedef struct { int fd; int pos, len; char dir[512]; char pat[260]; char buf[4096]; } FIND;
static int wild(const char *p, const char *s) {
    for (; *p; p++, s++) {
        if (*p == '*') { while (p[1] == '*') p++; if (!p[1]) return 1; for (; *s; s++) if (wild(p + 1, s)) return 1; return wild(p + 1, s); }
        if (!*s || (*p != '?' && lw((unsigned char)*p) != lw((unsigned char)*s))) return 0;
    }
    return !*s;
}
static void ft(unsigned char *d, i64 sec) { u64 v = ((u64)sec + 11644473600ULL) * 10000000ULL; *(u64 *)d = v; }
static int find_next(FIND *f, unsigned char *data) {
    for (;;) {
        if (f->pos >= f->len) {
            i64 n = sc3(S_GETDENTS64, f->fd, f->buf, sizeof f->buf);
            if (n <= 0) { SetLastError(18); return 0; }   /* ERROR_NO_MORE_FILES */
            f->pos = 0; f->len = (int)n;
        }
        unsigned char *e = (unsigned char *)f->buf + f->pos;
        int reclen = *(unsigned short *)(e + 16);
        const char *name = (const char *)(e + 19);
        f->pos += reclen;
        if (!wild(f->pat, name)) continue;
        for (int i = 0; i < 320; i++) data[i] = 0;
        char full[800]; size_t dl = slen(f->dir); cpy(full, f->dir, sizeof full);
        if (dl && full[dl - 1] != '/') { full[dl++] = '/'; full[dl] = 0; }
        cpy(full + dl, name, sizeof full - dl);
        unsigned char st[144];
        u32 attr = 0x80; u64 size = 0; i64 mt = 0;
        if (sc6(S_NEWFSTATAT, -100, (i64)full, (i64)st, 0, 0, 0) == 0) {
            u32 mode = *(u32 *)(st + 24);
            if ((mode & 0xF000) == 0x4000) attr = 0x10;
            size = *(u64 *)(st + 48); mt = *(i64 *)(st + 88);
        }
        *(u32 *)data = attr;
        ft(data + 4, mt); ft(data + 12, mt); ft(data + 20, mt);
        *(u32 *)(data + 28) = (u32)(size >> 32); *(u32 *)(data + 32) = (u32)size;
        cpy((char *)data + 44, name, 260);
        return 1;
    }
}
EXPORT HANDLE WINAPI FindFirstFileA(const char *path, void *data) {
    char q[800]; dos_path(q, path, sizeof q);
    size_t n = slen(q); size_t cut = n;
    while (cut > 0 && q[cut - 1] != '/') cut--;
    FIND *f = (FIND *)(i64)sc6(S_MMAP, 0, (i64)((sizeof(FIND) + 4095) & ~4095UL), 3, 0x22, -1, 0);
    if ((i64)f < 0 && (i64)f > -4096) { SetLastError(8); return INVALID_HANDLE; }
    if (cut == 0) cpy(f->dir, ".", sizeof f->dir); else { for (size_t i = 0; i < cut; i++) f->dir[i] = q[i]; f->dir[cut] = 0; if (cut > 1) f->dir[cut - 1] = 0; }
    cpy(f->pat, q + cut, sizeof f->pat);
    f->fd = (int)sc3(S_OPEN, f->dir, 0, 0);
    if (f->fd < 0) { SetLastError(3); sc3(S_MUNMAP, f, (sizeof(FIND) + 4095) & ~4095UL, 0); return INVALID_HANDLE; }
    f->pos = f->len = 0;
    if (!find_next(f, data)) { sc3(S_CLOSE, f->fd, 0, 0); sc3(S_MUNMAP, f, (sizeof(FIND) + 4095) & ~4095UL, 0); SetLastError(2); return INVALID_HANDLE; }
    return f;
}
EXPORT BOOL WINAPI FindNextFileA(HANDLE h, void *data) { return find_next((FIND *)h, data); }
EXPORT BOOL WINAPI FindClose(HANDLE h) { FIND *f = h; sc3(S_CLOSE, f->fd, 0, 0); sc3(S_MUNMAP, f, (sizeof(FIND) + 4095) & ~4095UL, 0); return 1; }

/* ---- console ---- */
EXPORT BOOL WINAPI GetConsoleMode(HANDLE h, u32 *m) { (void)h; *m = 3; return 1; }
EXPORT BOOL WINAPI SetConsoleMode(HANDLE h, u32 m) { (void)h; (void)m; return 1; }
EXPORT BOOL WINAPI SetConsoleCtrlHandler(void *f, BOOL add) { (void)f; (void)add; return 1; }
EXPORT BOOL WINAPI WriteConsoleA(HANDLE h, const void *buf, u32 n, u32 *written, void *res) { (void)res; i64 r = sc3(S_WRITE, (i64)h, buf, n); if (written) *written = r > 0 ? (u32)r : 0; return r >= 0; }
EXPORT void WINAPI OutputDebugStringA(const char *s) { (void)s; }
EXPORT u32 WINAPI GetACP(void) { return 1252; }

/* ---- memory ---- */
typedef struct { u64 size; u64 magic; } mh;
static void *mem_alloc(u64 n, int zero) {
    u64 total = (n + sizeof(mh) + 4095) & ~4095ULL;
    char *p = (char *)sc6(S_MMAP, 0, (i64)total, 3, 0x22, -1, 0);
    if ((i64)p < 0 && (i64)p > -4096) { SetLastError(8); return 0; }
    (void)zero;                                 /* anonymous memory is zero-filled */
    mh *h = (mh *)p; h->size = total - sizeof(mh); h->magic = 0x4C4F43;
    return h + 1;
}
EXPORT HANDLE WINAPI LocalAlloc(u32 flags, u64 n) { return mem_alloc(n, flags & 0x40); }
EXPORT HANDLE WINAPI LocalFree(HANDLE p) { if (p) { mh *h = (mh *)p - 1; sc3(S_MUNMAP, h, h->size + sizeof(mh), 0); } return 0; }
EXPORT HANDLE WINAPI GlobalAlloc(u32 flags, u64 n) { return mem_alloc(n, flags & 0x40); }
EXPORT HANDLE WINAPI GlobalFree(HANDLE p) { return LocalFree(p); }
EXPORT void *WINAPI HeapReAlloc(HANDLE heap, u32 flags, void *p, u64 n) {
    (void)heap; (void)flags;
    if (!p) return mem_alloc(n, 1);
    mh *h = (mh *)p - 1;
    if (n <= h->size) return p;
    char *q = mem_alloc(n, 1);
    if (!q) return 0;
    char *s = p; for (u64 i = 0; i < h->size; i++) q[i] = s[i];
    LocalFree(p);
    return q;
}


/* ---- threads and synchronisation (Win32 on top of the kernel's ntdll layer) ---- */
extern i64 WINAPI NtCreateThreadEx(HANDLE *h, u32 access, void *oa, HANDLE proc, void *start, void *arg, u32 flags, u64 zb, u64 ss, u64 ms, void *attrs);
extern i64 WINAPI NtTerminateThread(HANDLE h, i64 status);
extern i64 WINAPI NtSetEvent(HANDLE h, void *prev);
extern i64 WINAPI NtResetEvent(HANDLE h, void *prev);
extern i64 WINAPI NtCreateMutant(HANDLE *h, u32 access, void *oa, u32 initial_owner);
extern i64 WINAPI NtReleaseMutant(HANDLE h, void *prev);
extern i64 WINAPI NtCreateSemaphore(HANDLE *h, u32 access, void *oa, i64 initial, i64 max);
extern i64 WINAPI NtReleaseSemaphore(HANDLE h, i64 count, long *prev);
extern i64 WINAPI NtWaitForMultipleObjects(u32 n, HANDLE *h, u32 type, u32 alertable, i64 *timeout);

static u32 g_tid_counter = 1000;
EXPORT HANDLE WINAPI CreateThread(void *sa, u64 stack, void *start, void *arg, u32 flags, u32 *tid) {
    (void)sa; (void)stack; (void)flags;
    HANDLE h = 0;
    i64 st = NtCreateThreadEx(&h, 0x1FFFFF, 0, (HANDLE)(i64)-1, start, arg, 0, 0, 0, 0, 0);
    if (st != 0) { SetLastError(8); return 0; }
    if (tid) *tid = ++g_tid_counter;
    return h;
}
EXPORT void WINAPI ExitThread(u32 code) { NtTerminateThread(0, code); for (;;) {} }
EXPORT BOOL WINAPI GetExitCodeThread(HANDLE h, u32 *code) { (void)h; *code = 0; return 1; }
EXPORT BOOL WINAPI SetEvent(HANDLE h) { return NtSetEvent(h, 0) == 0; }
EXPORT BOOL WINAPI ResetEvent(HANDLE h) { return NtResetEvent(h, 0) == 0; }
EXPORT HANDLE WINAPI CreateMutexA(void *sa, BOOL owner, const char *name) { (void)sa; (void)name; HANDLE h = 0; return NtCreateMutant(&h, 0x1F0001, 0, owner ? 1 : 0) == 0 ? h : 0; }
EXPORT BOOL WINAPI ReleaseMutex(HANDLE h) { return NtReleaseMutant(h, 0) == 0; }
EXPORT HANDLE WINAPI CreateSemaphoreA(void *sa, long initial, long max, const char *name) { (void)sa; (void)name; HANDLE h = 0; return NtCreateSemaphore(&h, 0x1F0003, 0, initial, max) == 0 ? h : 0; }
EXPORT BOOL WINAPI ReleaseSemaphore(HANDLE h, long n, long *prev) { return NtReleaseSemaphore(h, n, prev) == 0; }
EXPORT u32 WINAPI WaitForMultipleObjects(u32 n, HANDLE *h, BOOL all, u32 ms) {
    i64 t = -(i64)ms * 10000;                      /* relative 100 ns units */
    i64 st = NtWaitForMultipleObjects(n, h, all ? 0 : 1, 0, ms == 0xFFFFFFFFu ? 0 : &t);
    if (st == 0x102) return 0x102;                  /* WAIT_TIMEOUT */
    return st < 0 ? 0xFFFFFFFFu : (u32)st;
}

__attribute__((dllexport)) int __stdcall DllMain(void *h, unsigned reason, void *res) { (void)h; (void)reason; (void)res; return 1; }
