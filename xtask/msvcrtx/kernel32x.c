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
EXPORT u32 WINAPI GetCurrentThreadId(void) { i64 t; __asm__("mov %%gs:0x30, %0" : "=r"(t)); return (u32)(t >> 8); }
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

/* ---- memory: see the heap allocator further down ---- */
typedef struct { u64 size; u64 cls; } mh;
static void *mem_alloc(u64 n, int zero);
EXPORT HANDLE WINAPI LocalAlloc(u32 flags, u64 n);
EXPORT HANDLE WINAPI LocalFree(HANDLE p);
EXPORT void *WINAPI HeapReAlloc(HANDLE heap, u32 flags, void *p, u64 n);


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

/* ---- critical sections (the kernel's built-in ones are no-ops; with real threads they must lock) ---- */
static inline i64 thread_id(void) { i64 t; __asm__("mov %%gs:0x30, %0" : "=r"(t)); return t; }
EXPORT void WINAPI InitializeCriticalSection(void *cs) { unsigned char *p = cs; for (int i = 0; i < 40; i++) p[i] = 0; }
EXPORT BOOL WINAPI InitializeCriticalSectionAndSpinCount(void *cs, u32 spin) { (void)spin; InitializeCriticalSection(cs); return 1; }
EXPORT void WINAPI DeleteCriticalSection(void *cs) { (void)cs; }
EXPORT BOOL WINAPI TryEnterCriticalSection(void *cs) {
    unsigned char *p = cs; i64 me = thread_id();
    if (*(volatile i64 *)(p + 16) == me) { (*(volatile int *)(p + 12))++; return 1; }
    if (__sync_lock_test_and_set((volatile int *)(p + 8), 1)) return 0;
    *(volatile i64 *)(p + 16) = me; *(volatile int *)(p + 12) = 1;
    return 1;
}
EXPORT void WINAPI EnterCriticalSection(void *cs) {
    unsigned char *p = cs; i64 me = thread_id();
    if (*(volatile i64 *)(p + 16) == me) { (*(volatile int *)(p + 12))++; return; }
    while (__sync_lock_test_and_set((volatile int *)(p + 8), 1)) sc3(24, 0, 0, 0);   /* sched_yield */
    *(volatile i64 *)(p + 16) = me; *(volatile int *)(p + 12) = 1;
}
EXPORT void WINAPI LeaveCriticalSection(void *cs) {
    unsigned char *p = cs;
    if (--*(volatile int *)(p + 12) == 0) { *(volatile i64 *)(p + 16) = 0; __sync_lock_release((volatile int *)(p + 8)); }
}


/* ======================================================================================================
   Unicode (W) entry points, more file/path/system calls, TLS, interlocked operations. Everything
   converts to UTF-8 and reuses the A versions above.
   ====================================================================================================== */
typedef unsigned short u16;
extern HANDLE WINAPI LoadLibraryA(const char *);
extern void WINAPI Sleep(u32);
static void w2a(const u16 *w, char *out, size_t cap) {
    size_t o = 0;
    for (; *w && o + 5 < cap; w++) {
        u32 c = *w;
        if (c >= 0xD800 && c < 0xDC00 && w[1] >= 0xDC00 && w[1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (w[1] - 0xDC00); w++; }
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
        else { out[o++] = (char)(0xF0 | (c >> 18)); out[o++] = (char)(0x80 | ((c >> 12) & 63)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
    }
    out[o] = 0;
}
static size_t a2w(const char *s, u16 *out, size_t cap) {
    size_t o = 0;
    while (*s && o + 2 < cap) {
        u32 c = (unsigned char)*s++;
        if (c >= 0xF0 && s[0] && s[1] && s[2]) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
        else if (c >= 0xE0 && s[0] && s[1]) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
        else if (c >= 0xC0 && s[0]) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
        if (c >= 0x10000) { c -= 0x10000; out[o++] = (u16)(0xD800 + (c >> 10)); out[o++] = (u16)(0xDC00 + (c & 1023)); }
        else out[o++] = (u16)c;
    }
    out[o] = 0;
    return o;
}
static size_t wlen(const u16 *w) { size_t n = 0; while (w[n]) n++; return n; }
#define W2A(var, w) char var[1024]; w2a((w), var, sizeof var)

EXPORT HANDLE WINAPI CreateFileW(const u16 *n, u32 acc, u32 sh, void *sa, u32 disp, u32 fl, HANDLE t) { W2A(a, n); return CreateFileA(a, acc, sh, sa, disp, fl, t); }
EXPORT BOOL WINAPI DeleteFileW(const u16 *n) { W2A(a, n); return DeleteFileA(a); }
EXPORT u32 WINAPI GetFileAttributesW(const u16 *n) { W2A(a, n); return GetFileAttributesA(a); }
EXPORT BOOL WINAPI CreateDirectoryW(const u16 *n, void *sa) { W2A(a, n); return CreateDirectoryA(a, sa); }
EXPORT BOOL WINAPI RemoveDirectoryW(const u16 *n) { W2A(a, n); return RemoveDirectoryA(a); }
EXPORT BOOL WINAPI MoveFileW(const u16 *x, const u16 *y) { W2A(a, x); W2A(b, y); return MoveFileA(a, b); }
EXPORT BOOL WINAPI MoveFileExW(const u16 *x, const u16 *y, u32 f) { W2A(a, x); W2A(b, y); return MoveFileExA(a, b, f); }
EXPORT BOOL WINAPI CopyFileW(const u16 *x, const u16 *y, BOOL f) { W2A(a, x); W2A(b, y); return CopyFileA(a, b, f); }
EXPORT BOOL WINAPI SetCurrentDirectoryW(const u16 *n) { W2A(a, n); return SetCurrentDirectoryA(a); }
EXPORT u32 WINAPI GetCurrentDirectoryW(u32 size, u16 *buf) {
    char a[600]; u32 n = GetCurrentDirectoryA(sizeof a, a);
    if (!n || n >= sizeof a) return n;
    u16 w[600]; size_t l = a2w(a, w, 600);
    if (l + 1 > size) return (u32)l + 1;
    for (size_t i = 0; i <= l; i++) buf[i] = w[i];
    return (u32)l;
}
EXPORT u32 WINAPI GetModuleFileNameW(HANDLE m, u16 *buf, u32 size) {
    char a[260]; GetModuleFileNameA(m, a, sizeof a);
    u16 w[260]; size_t l = a2w(a, w, 260);
    if (l + 1 > size) l = size ? size - 1 : 0;
    for (size_t i = 0; i < l; i++) buf[i] = w[i];
    if (size) buf[l] = 0;
    return (u32)l;
}
EXPORT int WINAPI lstrlenW(const u16 *w) { return (int)wlen(w); }
EXPORT u16 *WINAPI lstrcpyW(u16 *d, const u16 *s) { u16 *r = d; while ((*d++ = *s++)); return r; }
EXPORT u16 *WINAPI lstrcatW(u16 *d, const u16 *s) { u16 *r = d; while (*d) d++; while ((*d++ = *s++)); return r; }
EXPORT char *WINAPI lstrcatA(char *d, const char *s) { char *r = d; while (*d) d++; while ((*d++ = *s++)); return r; }
EXPORT char *WINAPI lstrcpynA(char *d, const char *s, int n) { int i = 0; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; if (n > 0) d[i] = 0; return d; }
EXPORT u16 *WINAPI lstrcpynW(u16 *d, const u16 *s, int n) { int i = 0; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; if (n > 0) d[i] = 0; return d; }
EXPORT int WINAPI lstrcmpW(const u16 *a, const u16 *b) { while (*a && *a == *b) { a++; b++; } return (int)*a - (int)*b; }
static int lwc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
EXPORT int WINAPI lstrcmpiW(const u16 *a, const u16 *b) { while (*a && lwc(*a) == lwc(*b)) { a++; b++; } return lwc(*a) - lwc(*b); }
EXPORT void WINAPI OutputDebugStringW(const u16 *s) { (void)s; }
EXPORT BOOL WINAPI WriteConsoleW(HANDLE h, const u16 *buf, u32 n, u32 *written, void *res) {
    (void)res;
    char a[1024]; u32 done = 0;
    while (done < n) {
        u16 chunk[200]; u32 k = n - done < 199 ? n - done : 199;
        for (u32 i = 0; i < k; i++) chunk[i] = buf[done + i];
        chunk[k] = 0; w2a(chunk, a, sizeof a);
        sc3(S_WRITE, (i64)h, a, slen(a));
        done += k;
    }
    if (written) *written = n;
    return 1;
}
EXPORT const u16 *WINAPI GetCommandLineW(void) {
    static u16 w[1024]; static int init;
    if (!init) { extern const char *WINAPI GetCommandLineA(void); a2w(GetCommandLineA(), w, 1024); init = 1; }
    return w;
}
EXPORT HANDLE WINAPI GetModuleHandleW(const u16 *n) { if (!n) return GetModuleHandleA(0); W2A(a, n); return GetModuleHandleA(a); }
EXPORT HANDLE WINAPI LoadLibraryW(const u16 *n) { W2A(a, n); return LoadLibraryA(a); }
EXPORT BOOL WINAPI FreeLibrary(HANDLE h) { (void)h; return 1; }
EXPORT BOOL WINAPI DisableThreadLibraryCalls(HANDLE h) { (void)h; return 1; }

/* FindFirstFileW / FindNextFileW: WIN32_FIND_DATAW is the A layout with the names as UTF-16 */
static void find_w(unsigned char *a, unsigned char *w) {
    for (int i = 0; i < 44; i++) w[i] = a[i];
    for (int i = 44; i < 44 + 520 + 28; i++) w[i] = 0;
    a2w((const char *)a + 44, (u16 *)(w + 44), 260);
}
EXPORT HANDLE WINAPI FindFirstFileW(const u16 *n, void *data) {
    W2A(a, n); unsigned char tmp[320]; HANDLE h = FindFirstFileA(a, tmp);
    if (h != INVALID_HANDLE) find_w(tmp, data);
    return h;
}
EXPORT BOOL WINAPI FindNextFileW(HANDLE h, void *data) { unsigned char tmp[320]; if (!FindNextFileA(h, tmp)) return 0; find_w(tmp, data); return 1; }

/* paths */
EXPORT u32 WINAPI GetFullPathNameA(const char *name, u32 size, char *buf, char **part) {
    char tmp[1024];
    if ((name[0] && name[1] == ':') || name[0] == '\\' || name[0] == '/') cpy(tmp, name, sizeof tmp);
    else { u32 n = GetCurrentDirectoryA(sizeof tmp, tmp); if (n && tmp[n - 1] != '\\') { tmp[n++] = '\\'; } cpy(tmp + n, name, sizeof tmp - n); }
    for (char *p = tmp; *p; p++) if (*p == '/') *p = '\\';
    u32 len = (u32)slen(tmp);
    if (len + 1 > size) return len + 1;
    cpy(buf, tmp, size);
    if (part) { char *s = buf + len; while (s > buf && s[-1] != '\\') s--; *part = s; }
    return len;
}
EXPORT u32 WINAPI GetFullPathNameW(const u16 *name, u32 size, u16 *buf, u16 **part) {
    W2A(a, name); char tmp[1024]; char *pa = 0; u32 n = GetFullPathNameA(a, sizeof tmp, tmp, &pa);
    u16 w[1024]; size_t l = a2w(tmp, w, 1024); (void)n;
    if (l + 1 > size) return (u32)l + 1;
    for (size_t i = 0; i <= l; i++) buf[i] = w[i];
    if (part) { u16 *s = buf + l; while (s > buf && s[-1] != '\\') s--; *part = s; }
    return (u32)l;
}
EXPORT u32 WINAPI GetTempPathA(u32 size, char *buf) { const char *t = "C:\\tmp\\"; if (size < 8) return 8; cpy(buf, t, size); return 7; }
EXPORT u32 WINAPI GetTempPathW(u32 size, u16 *buf) { if (size < 8) return 8; a2w("C:\\tmp\\", buf, size); return 7; }
EXPORT u32 WINAPI GetTempFileNameA(const char *dir, const char *prefix, u32 unique, char *out) {
    static u32 ctr; if (!unique) unique = (u32)sc3(S_GETPID, 0, 0, 0) * 100 + (++ctr);
    char hex[9]; for (int i = 0; i < 8; i++) hex[i] = "0123456789abcdef"[(unique >> (28 - 4 * i)) & 15]; hex[8] = 0;
    size_t n = slen(dir); cpy(out, dir, 260); if (n && out[n - 1] != '\\') out[n++] = '\\';
    for (int i = 0; i < 3 && prefix[i]; i++) out[n++] = prefix[i];
    cpy(out + n, hex, 260 - n); n += 8; cpy(out + n, ".tmp", 260 - n);
    HANDLE h = CreateFileA(out, 0x40000000, 0, 0, 1, 0, 0);
    if (h != INVALID_HANDLE) sc3(S_CLOSE, (i64)h, 0, 0);
    return unique;
}
EXPORT u32 WINAPI GetSystemDirectoryA(char *b, u32 n) { if (n < 20) return 20; cpy(b, "C:\\Windows\\System32", n); return 19; }
EXPORT u32 WINAPI GetWindowsDirectoryA(char *b, u32 n) { if (n < 12) return 12; cpy(b, "C:\\Windows", n); return 10; }
EXPORT u32 WINAPI GetSystemDirectoryW(u16 *b, u32 n) { if (n < 20) return 20; a2w("C:\\Windows\\System32", b, n); return 19; }
EXPORT u32 WINAPI GetWindowsDirectoryW(u16 *b, u32 n) { if (n < 12) return 12; a2w("C:\\Windows", b, n); return 10; }
EXPORT BOOL WINAPI GetComputerNameA(char *b, u32 *n) { if (*n < 5) { *n = 5; return 0; } cpy(b, "THOS", *n); *n = 4; return 1; }
EXPORT BOOL WINAPI GetComputerNameW(u16 *b, u32 *n) { if (*n < 5) { *n = 5; return 0; } a2w("THOS", b, *n); *n = 4; return 1; }
EXPORT BOOL WINAPI SetFileAttributesA(const char *p, u32 a) { (void)p; (void)a; return 1; }
EXPORT BOOL WINAPI SetFileAttributesW(const u16 *p, u32 a) { (void)p; (void)a; return 1; }
typedef struct { u32 attr; u64 ct, at, wt; u32 hi, lo; } __attribute__((packed)) FADATA;
EXPORT BOOL WINAPI GetFileAttributesExA(const char *p, u32 level, FADATA *d) {
    (void)level; unsigned char st[144];
    if (stat_path(p, st) < 0) { SetLastError(2); return 0; }
    u32 mode = *(u32 *)(st + 24); u64 size = *(u64 *)(st + 48);
    d->attr = (mode & 0xF000) == 0x4000 ? 0x10 : 0x80;
    ft((unsigned char *)&d->at, *(i64 *)(st + 72)); ft((unsigned char *)&d->wt, *(i64 *)(st + 88)); ft((unsigned char *)&d->ct, *(i64 *)(st + 104));
    d->hi = (u32)(size >> 32); d->lo = (u32)size;
    return 1;
}
EXPORT BOOL WINAPI GetFileAttributesExW(const u16 *p, u32 level, FADATA *d) { W2A(a, p); return GetFileAttributesExA(a, level, d); }
EXPORT u32 WINAPI GetFileType(HANDLE h) { return (i64)h >= 0 && (i64)h <= 2 ? 2 : 1; }
EXPORT BOOL WINAPI GetFileTime(HANDLE h, u64 *c, u64 *a, u64 *w) {
    unsigned char st[144];
    if (sc6(5 /* fstat */, (i64)h, (i64)st, 0, 0, 0, 0) < 0) return 0;
    if (a) ft((unsigned char *)a, *(i64 *)(st + 72)); if (w) ft((unsigned char *)w, *(i64 *)(st + 88)); if (c) ft((unsigned char *)c, *(i64 *)(st + 104));
    return 1;
}
EXPORT BOOL WINAPI SetFileTime(HANDLE h, const u64 *c, const u64 *a, const u64 *w) { (void)h; (void)c; (void)a; (void)w; return 1; }
EXPORT BOOL WINAPI CompareFileTime(const u64 *a, const u64 *b) { return *a < *b ? -1 : *a > *b; }
EXPORT BOOL WINAPI FileTimeToSystemTime(const u64 *f, SYSTEMTIME *st) {
    u64 t = *f / 10000000ULL; i64 secs = (i64)t - 11644473600LL; i64 days = secs / 86400, rem = secs % 86400;
    if (rem < 0) { rem += 86400; days--; }
    int y, m, d; civil(days, &y, &m, &d);
    st->y = (u16)y; st->mo = (u16)m; st->d = (u16)d; st->dow = (u16)(((days + 4) % 7 + 7) % 7);
    st->h = (u16)(rem / 3600); st->mi = (u16)(rem % 3600 / 60); st->s = (u16)(rem % 60); st->ms = (u16)(*f / 10000 % 1000);
    return 1;
}
static i64 days_from_civil(i64 y, unsigned m, unsigned d) {
    y -= m <= 2; i64 era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (i64)doe - 719468;
}
EXPORT BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *st, u64 *f) {
    i64 days = days_from_civil(st->y, st->mo, st->d);
    i64 secs = days * 86400 + st->h * 3600 + st->mi * 60 + st->s;
    *f = ((u64)(secs + 11644473600LL)) * 10000000ULL + (u64)st->ms * 10000ULL;
    return 1;
}
EXPORT BOOL WINAPI FileTimeToLocalFileTime(const u64 *a, u64 *b) { *b = *a; return 1; }
EXPORT BOOL WINAPI LocalFileTimeToFileTime(const u64 *a, u64 *b) { *b = *a; return 1; }
EXPORT void WINAPI GetSystemTimePreciseAsFileTime(u64 *f) { GetSystemTimeAsFileTime(f); }
typedef struct { long bias; u16 sname[32]; SYSTEMTIME sd; long sbias; u16 dname[32]; SYSTEMTIME dd; long dbias; } TZI;
EXPORT u32 WINAPI GetTimeZoneInformation(TZI *t) { unsigned char *p = (unsigned char *)t; for (u32 i = 0; i < sizeof *t; i++) p[i] = 0; return 0; }

/* system information */
typedef struct { u32 len, load; u64 totphys, availphys, totpage, availpage, totvirt, availvirt, availext; } MEMSTATUSEX;
EXPORT BOOL WINAPI GlobalMemoryStatusEx(MEMSTATUSEX *m) {
    m->load = 30; m->totphys = 4ULL << 30; m->availphys = 3ULL << 30; m->totpage = 6ULL << 30; m->availpage = 5ULL << 30;
    m->totvirt = 1ULL << 47; m->availvirt = (1ULL << 47) - (1ULL << 30); m->availext = 0; return 1;
}
EXPORT void WINAPI GetNativeSystemInfo(void *p) { GetSystemInfo(p); }
EXPORT BOOL WINAPI IsProcessorFeaturePresent(u32 f) { return f == 10 || f == 6 || f == 7 || f == 13; }   /* SSE2, SSE3, MMX, ... */
EXPORT u32 WINAPI GetLogicalDrives(void) { return 1u << 2; }
EXPORT u32 WINAPI GetDriveTypeA(const char *r) { (void)r; return 3; }
EXPORT BOOL WINAPI GetDiskFreeSpaceExA(const char *d, u64 *a, u64 *t, u64 *f) { (void)d; if (a) *a = 50ULL << 30; if (t) *t = 100ULL << 30; if (f) *f = 50ULL << 30; return 1; }
EXPORT int WINAPI MulDiv(int a, int b, int c) { if (!c) return -1; return (int)(((i64)a * b + (c / 2)) / c); }
EXPORT HANDLE WINAPI GetCurrentThread(void) { return (HANDLE)(i64)-2; }
EXPORT BOOL WINAPI SwitchToThread(void) { sc3(24, 0, 0, 0); return 1; }
EXPORT u32 WINAPI SleepEx(u32 ms, BOOL alertable) { (void)alertable; Sleep(ms); return 0; }
EXPORT int WINAPI GetThreadPriority(HANDLE h) { (void)h; return 0; }
EXPORT BOOL WINAPI SetThreadPriority(HANDLE h, int p) { (void)h; (void)p; return 1; }
EXPORT BOOL WINAPI SetPriorityClass(HANDLE h, u32 p) { (void)h; (void)p; return 1; }
EXPORT u32 WINAPI SetErrorMode(u32 m) { (void)m; return 0; }
EXPORT u32 WINAPI GetOEMCP(void) { return 437; }
EXPORT u32 WINAPI GetConsoleCP(void) { return 65001; }
EXPORT u32 WINAPI GetConsoleOutputCP(void) { return 65001; }
EXPORT BOOL WINAPI SetConsoleOutputCP(u32 c) { (void)c; return 1; }
EXPORT BOOL WINAPI SetConsoleCP(u32 c) { (void)c; return 1; }
EXPORT BOOL WINAPI SetConsoleTitleA(const char *t) { (void)t; return 1; }
EXPORT BOOL WINAPI SetConsoleTitleW(const u16 *t) { (void)t; return 1; }
EXPORT BOOL WINAPI SetConsoleTextAttribute(HANDLE h, u16 a) { (void)h; (void)a; return 1; }
typedef struct { short x, y; } COORD;
EXPORT BOOL WINAPI GetConsoleScreenBufferInfo(HANDLE h, void *info) {
    (void)h; short *s = info; for (int i = 0; i < 11; i++) s[i] = 0;
    s[0] = 80; s[1] = 25; s[4] = 7; s[5] = 0; s[6] = 0; s[7] = 79; s[8] = 24; s[9] = 80; s[10] = 25; return 1;
}
EXPORT BOOL WINAPI ReadConsoleA(HANDLE h, void *buf, u32 n, u32 *got, void *ctl) { (void)ctl; i64 r = sc3(S_READ, (i64)h, buf, n); if (got) *got = r > 0 ? (u32)r : 0; return r >= 0; }
EXPORT u32 WINAPI GetCPInfo_dummy(void) { return 0; }
typedef struct { u32 max; unsigned char def[2]; unsigned char lead[12]; } CPINFO;
EXPORT BOOL WINAPI GetCPInfo(u32 cp, CPINFO *i) { (void)cp; i->max = 1; i->def[0] = '?'; i->def[1] = 0; for (int k = 0; k < 12; k++) i->lead[k] = 0; return 1; }
EXPORT BOOL WINAPI IsValidCodePage(u32 cp) { return cp == 437 || cp == 1252 || cp == 65001 || cp == 28591; }
EXPORT u32 WINAPI GetUserDefaultLCID(void) { return 0x0407; }
EXPORT u32 WINAPI GetSystemDefaultLCID(void) { return 0x0407; }
EXPORT u16 WINAPI GetUserDefaultUILanguage(void) { return 0x0407; }
EXPORT u16 WINAPI GetSystemDefaultLangID(void) { return 0x0407; }
EXPORT int WINAPI GetLocaleInfoA(u32 lcid, u32 type, char *buf, int n) { (void)lcid; (void)type; if (n > 0) buf[0] = 0; return 1; }
EXPORT int WINAPI GetLocaleInfoW(u32 lcid, u32 type, u16 *buf, int n) { (void)lcid; (void)type; if (n > 0) buf[0] = 0; return 1; }
EXPORT BOOL WINAPI GetStringTypeW(u32 type, const u16 *s, int n, u16 *out) {
    if (n < 0) n = (int)wlen(s) + 1;
    for (int i = 0; i < n; i++) {
        u16 c = s[i], t = 0;
        if (type == 1) {                /* CT_CTYPE1 */
            if (c >= 'A' && c <= 'Z') t = 0x101; else if (c >= 'a' && c <= 'z') t = 0x102;
            else if (c >= '0' && c <= '9') t = 0x4; else if (c == ' ' || (c >= 9 && c <= 13)) t = 0x8 | 0x40;
            else if (c < 32 || c == 127) t = 0x20; else if (c < 127) t = 0x10; else t = 0x100;
            if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9')) t |= 0x80;
        }
        out[i] = t;
    }
    return 1;
}
EXPORT int WINAPI LCMapStringW(u32 lcid, u32 flags, const u16 *s, int n, u16 *d, int dn) {
    (void)lcid; if (n < 0) n = (int)wlen(s) + 1;
    if (!dn) return n;
    if (dn < n) return 0;
    for (int i = 0; i < n; i++) { u16 c = s[i]; if ((flags & 0x200) && c >= 'A' && c <= 'Z') c += 32; else if ((flags & 0x100) && c >= 'a' && c <= 'z') c -= 32; d[i] = c; }
    return n;
}
EXPORT int WINAPI LCMapStringA(u32 lcid, u32 flags, const char *s, int n, char *d, int dn) {
    (void)lcid; if (n < 0) n = (int)slen(s) + 1;
    if (!dn) return n;
    if (dn < n) return 0;
    for (int i = 0; i < n; i++) { char c = s[i]; if ((flags & 0x200) && c >= 'A' && c <= 'Z') c += 32; else if ((flags & 0x100) && c >= 'a' && c <= 'z') c -= 32; d[i] = c; }
    return n;
}
EXPORT int WINAPI CompareStringA(u32 lcid, u32 flags, const char *a, int na, const char *b, int nb) {
    (void)lcid; if (na < 0) na = (int)slen(a); if (nb < 0) nb = (int)slen(b);
    int i = 0;
    for (; i < na && i < nb; i++) { int x = (unsigned char)a[i], y = (unsigned char)b[i]; if (flags & 1) { x = lw(x); y = lw(y); } if (x != y) return x < y ? 1 : 3; }
    return na == nb ? 2 : (na < nb ? 1 : 3);
}
EXPORT int WINAPI CompareStringW(u32 lcid, u32 flags, const u16 *a, int na, const u16 *b, int nb) {
    (void)lcid; if (na < 0) na = (int)wlen(a); if (nb < 0) nb = (int)wlen(b);
    int i = 0;
    for (; i < na && i < nb; i++) { int x = a[i], y = b[i]; if (flags & 1) { x = lwc(x); y = lwc(y); } if (x != y) return x < y ? 1 : 3; }
    return na == nb ? 2 : (na < nb ? 1 : 3);
}
EXPORT u32 WINAPI FormatMessageA(u32 flags, const void *src, u32 id, u32 lang, char *buf, u32 size, void *args) {
    (void)flags; (void)src; (void)lang; (void)args;
    char tmp[48]; const char *pre = "Windows error "; size_t n = 0; for (; pre[n]; n++) tmp[n] = pre[n];
    char d[12]; int k = 0; u32 v = id; do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) tmp[n++] = d[--k];
    tmp[n] = 0;
    if (flags & 0x100) { char **out = (char **)buf; char *p = LocalAlloc(0, n + 1) ; if (!p) return 0; cpy(p, tmp, n + 1); *out = p; return (u32)n; }
    cpy(buf, tmp, size); return (u32)slen(buf);
}
typedef struct { u32 cb; char pad[100]; } STARTUPINFOA;
EXPORT void WINAPI GetStartupInfoA(void *si) { unsigned char *p = si; for (int i = 0; i < 104; i++) p[i] = 0; *(u32 *)p = 104; }
EXPORT void WINAPI GetStartupInfoW(void *si) { GetStartupInfoA(si); }
EXPORT void *WINAPI EncodePointer(void *p) { return p; }
EXPORT void *WINAPI DecodePointer(void *p) { return p; }
EXPORT BOOL WINAPI SetHandleInformation(HANDLE h, u32 m, u32 f) { (void)h; (void)m; (void)f; return 1; }
EXPORT BOOL WINAPI GetHandleInformation(HANDLE h, u32 *f) { (void)h; *f = 0; return 1; }
EXPORT BOOL WINAPI DuplicateHandle(HANDLE sp, HANDLE h, HANDLE tp, HANDLE *out, u32 acc, BOOL inh, u32 opt) {
    (void)sp; (void)tp; (void)acc; (void)inh; (void)opt;
    i64 fd = sc3(32 /* dup */, (i64)h, 0, 0); if (fd < 0) return 0; *out = (HANDLE)fd; return 1;
}
EXPORT BOOL WINAPI CreatePipe(HANDLE *r, HANDLE *w, void *sa, u32 size) {
    (void)sa; (void)size; int fds[2]; if (sc3(22 /* pipe */, fds, 0, 0) < 0) return 0; *r = (HANDLE)(i64)fds[0]; *w = (HANDLE)(i64)fds[1]; return 1;
}
EXPORT HANDLE WINAPI CreateEventW(void *sa, BOOL manual, BOOL init, const u16 *n) { (void)n; extern HANDLE WINAPI CreateEventA(void *, BOOL, BOOL, const char *); return CreateEventA(sa, manual, init, 0); }
EXPORT HANDLE WINAPI CreateMutexW(void *sa, BOOL owner, const u16 *n) { (void)n; return CreateMutexA(sa, owner, 0); }
EXPORT HANDLE WINAPI CreateSemaphoreW(void *sa, long a, long b, const u16 *n) { (void)n; return CreateSemaphoreA(sa, a, b, 0); }
EXPORT u32 WINAPI WaitForSingleObjectEx(HANDLE h, u32 ms, BOOL alertable) { (void)alertable; extern u32 WINAPI WaitForSingleObject(HANDLE, u32); return WaitForSingleObject(h, ms); }
EXPORT u32 WINAPI WaitForMultipleObjectsEx(u32 n, HANDLE *h, BOOL all, u32 ms, BOOL alertable) { (void)alertable; return WaitForMultipleObjects(n, h, all, ms); }

/* heaps (every heap is the process heap) */
EXPORT HANDLE WINAPI HeapCreate(u32 f, u64 a, u64 b) { (void)f; (void)a; (void)b; extern HANDLE WINAPI GetProcessHeap(void); return GetProcessHeap(); }
EXPORT BOOL WINAPI HeapDestroy(HANDLE h) { (void)h; return 1; }
EXPORT BOOL WINAPI HeapValidate(HANDLE h, u32 f, const void *p) { (void)h; (void)f; (void)p; return 1; }
EXPORT void *WINAPI GlobalLock(HANDLE p) { return p; }
EXPORT BOOL WINAPI GlobalUnlock(HANDLE p) { (void)p; return 0; }
EXPORT void *WINAPI LocalLock(HANDLE p) { return p; }
EXPORT BOOL WINAPI LocalUnlock(HANDLE p) { (void)p; return 0; }

/* interlocked operations */
EXPORT long WINAPI InterlockedIncrement(volatile long *p) { return __sync_add_and_fetch(p, 1); }
EXPORT long WINAPI InterlockedDecrement(volatile long *p) { return __sync_sub_and_fetch(p, 1); }
EXPORT long WINAPI InterlockedExchange(volatile long *p, long v) { return __sync_lock_test_and_set(p, v); }
EXPORT long WINAPI InterlockedExchangeAdd(volatile long *p, long v) { return __sync_fetch_and_add(p, v); }
EXPORT long WINAPI InterlockedCompareExchange(volatile long *p, long x, long c) { return __sync_val_compare_and_swap(p, c, x); }
EXPORT i64 WINAPI InterlockedIncrement64(volatile i64 *p) { return __sync_add_and_fetch(p, 1); }
EXPORT i64 WINAPI InterlockedDecrement64(volatile i64 *p) { return __sync_sub_and_fetch(p, 1); }
EXPORT i64 WINAPI InterlockedCompareExchange64(volatile i64 *p, i64 x, i64 c) { return __sync_val_compare_and_swap(p, c, x); }
EXPORT void *WINAPI InterlockedCompareExchangePointer(void *volatile *p, void *x, void *c) { return __sync_val_compare_and_swap(p, c, x); }
EXPORT void *WINAPI InterlockedExchangePointer(void *volatile *p, void *v) { return __sync_lock_test_and_set(p, v); }

/* thread-local storage: one table keyed by (thread id, slot); good for 64 slots and a few hundred values */
#define TLS_SLOTS 64
#define TLS_VALS 512
static volatile int tls_used[TLS_SLOTS], tls_lock;
static struct { i64 tid; u32 slot; void *val; } tls_vals[TLS_VALS];
static void tls_acquire(void) { while (__sync_lock_test_and_set(&tls_lock, 1)) sc3(24, 0, 0, 0); }
static void tls_release(void) { __sync_lock_release(&tls_lock); }
EXPORT u32 WINAPI TlsAlloc(void) {
    tls_acquire();
    for (u32 i = 0; i < TLS_SLOTS; i++) if (!tls_used[i]) { tls_used[i] = 1; tls_release(); return i; }
    tls_release(); SetLastError(8); return 0xFFFFFFFFu;
}
EXPORT BOOL WINAPI TlsFree(u32 i) {
    if (i >= TLS_SLOTS) return 0;
    tls_acquire(); tls_used[i] = 0;
    for (int k = 0; k < TLS_VALS; k++) if (tls_vals[k].tid && tls_vals[k].slot == i) tls_vals[k].tid = 0;
    tls_release(); return 1;
}
EXPORT BOOL WINAPI TlsSetValue(u32 i, void *v) {
    if (i >= TLS_SLOTS) return 0;
    i64 me = thread_id(); int free_k = -1;
    tls_acquire();
    for (int k = 0; k < TLS_VALS; k++) {
        if (tls_vals[k].tid == me && tls_vals[k].slot == i) { tls_vals[k].val = v; tls_release(); return 1; }
        if (!tls_vals[k].tid && free_k < 0) free_k = k;
    }
    if (free_k >= 0) { tls_vals[free_k].tid = me; tls_vals[free_k].slot = i; tls_vals[free_k].val = v; }
    tls_release();
    return free_k >= 0;
}
EXPORT void *WINAPI TlsGetValue(u32 i) {
    i64 me = thread_id(); void *r = 0;
    tls_acquire();
    for (int k = 0; k < TLS_VALS; k++) if (tls_vals[k].tid == me && tls_vals[k].slot == i) { r = tls_vals[k].val; break; }
    tls_release();
    SetLastError(0);
    return r;
}
EXPORT u32 WINAPI FlsAlloc(void *cb) { (void)cb; return TlsAlloc(); }
EXPORT BOOL WINAPI FlsFree(u32 i) { return TlsFree(i); }
EXPORT BOOL WINAPI FlsSetValue(u32 i, void *v) { return TlsSetValue(i, v); }
EXPORT void *WINAPI FlsGetValue(u32 i) { return TlsGetValue(i); }


/* ======================================================================================================
   What the Rust standard library (and similar runtimes) need on top of the above: the environment block,
   file information, file mappings, init-once, waitable-timer refusal (callers fall back to Sleep), and
   assorted stubs.
   ====================================================================================================== */
extern void *WINAPI RtlAddVectoredExceptionHandler(u32 first, void *handler);
extern u32 WINAPI GetLastError(void);

static char *env_names[64], *env_vals[64];
static const char *env_defaults[][2] = { { "PATH", "C:\\Windows;C:\\Windows\\System32" }, { "TEMP", "C:\\tmp" }, { "TMP", "C:\\tmp" }, { "OS", "THOS" },
    { "COMPUTERNAME", "THOS" }, { "SystemRoot", "C:\\Windows" }, { "USERPROFILE", "C:\\Users\\thos" }, { "HOMEDRIVE", "C:" }, { "HOMEPATH", "\\Users\\thos" },
    { "USERNAME", "thos" }, { "PROCESSOR_ARCHITECTURE", "AMD64" } };
static void env_init(void) {
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof env_defaults / sizeof env_defaults[0]; i++) {
        env_names[i] = (char *)env_defaults[i][0]; env_vals[i] = (char *)env_defaults[i][1];
    }
}
static int env_find(const char *n) {
    env_init();
    for (int i = 0; i < 64; i++) if (env_names[i] && lstrcmpiA(env_names[i], n) == 0) return i;
    return -1;
}
EXPORT u32 WINAPI GetEnvironmentVariableA(const char *n, char *buf, u32 size) {
    int i = env_find(n);
    if (i < 0 || !env_vals[i]) { SetLastError(203); return 0; }
    u32 len = (u32)slen(env_vals[i]);
    if (len + 1 > size) return len + 1;
    cpy(buf, env_vals[i], size);
    return len;
}
EXPORT BOOL WINAPI SetEnvironmentVariableA(const char *n, const char *v) {
    int i = env_find(n);
    if (i < 0) { if (!v) return 1; for (i = 0; i < 64; i++) if (!env_names[i]) break; if (i == 64) return 0; char *c = LocalAlloc(0, slen(n) + 1); if (!c) return 0; cpy(c, n, slen(n) + 1); env_names[i] = c; }
    if (!v) { env_vals[i] = 0; env_names[i] = 0; return 1; }
    char *c = LocalAlloc(0, slen(v) + 1); if (!c) return 0; cpy(c, v, slen(v) + 1); env_vals[i] = c; return 1;
}
EXPORT u32 WINAPI GetEnvironmentVariableW(const u16 *n, u16 *buf, u32 size) {
    W2A(a, n); char tmp[2048]; u32 r = GetEnvironmentVariableA(a, tmp, sizeof tmp);
    if (!r) return 0;
    u16 w[2048]; size_t l = a2w(tmp, w, 2048);
    if (l + 1 > size) return (u32)l + 1;
    for (size_t j = 0; j <= l; j++) buf[j] = w[j];
    return (u32)l;
}
EXPORT BOOL WINAPI SetEnvironmentVariableW(const u16 *n, const u16 *v) { W2A(a, n); if (!v) return SetEnvironmentVariableA(a, 0); W2A(b, v); return SetEnvironmentVariableA(a, b); }
EXPORT char *WINAPI GetEnvironmentStringsA(void) {
    env_init(); static char blk[8192]; size_t o = 0;
    for (int i = 0; i < 64; i++) if (env_names[i] && env_vals[i]) {
        size_t a = slen(env_names[i]), b = slen(env_vals[i]);
        if (o + a + b + 3 >= sizeof blk) break;
        for (size_t j = 0; j < a; j++) blk[o++] = env_names[i][j];
        blk[o++] = '='; for (size_t j = 0; j < b; j++) blk[o++] = env_vals[i][j]; blk[o++] = 0;
    }
    blk[o++] = 0; blk[o] = 0;
    return blk;
}
EXPORT u16 *WINAPI GetEnvironmentStringsW(void) {
    char *a = GetEnvironmentStringsA(); static u16 blk[8192]; size_t o = 0;
    while (*a && o + 2048 < 8192) { o += a2w(a, blk + o, 2048) + 1; a += slen(a) + 1; }
    blk[o] = 0;
    return blk;
}
EXPORT BOOL WINAPI FreeEnvironmentStringsA(char *p) { (void)p; return 1; }
EXPORT BOOL WINAPI FreeEnvironmentStringsW(u16 *p) { (void)p; return 1; }

/* file information */
EXPORT BOOL WINAPI GetFileInformationByHandle(HANDLE h, u32 *info) {
    unsigned char st[144];
    if (sc3(5, (i64)h, st, 0) < 0) { SetLastError(6); return 0; }
    u32 mode = *(u32 *)(st + 24); u64 size = *(u64 *)(st + 48);
    for (int i = 0; i < 13; i++) info[i] = 0;
    info[0] = (mode & 0xF000) == 0x4000 ? 0x10 : 0x80;
    ft((unsigned char *)&info[1], *(i64 *)(st + 104)); ft((unsigned char *)&info[3], *(i64 *)(st + 72)); ft((unsigned char *)&info[5], *(i64 *)(st + 88));
    info[7] = 1; info[8] = (u32)(size >> 32); info[9] = (u32)size; info[10] = 1;
    u64 ino = *(u64 *)(st + 8); info[11] = (u32)(ino >> 32); info[12] = (u32)ino;
    return 1;
}
EXPORT BOOL WINAPI GetFileInformationByHandleEx(HANDLE h, u32 cls, void *buf, u32 size) {
    unsigned char st[144];
    if (sc3(5, (i64)h, st, 0) < 0) { SetLastError(6); return 0; }
    u32 mode = *(u32 *)(st + 24); u64 sz = *(u64 *)(st + 48); int dir = (mode & 0xF000) == 0x4000;
    unsigned char *p = buf;
    if (cls == 0 && size >= 40) {                 /* FileBasicInfo */
        for (int i = 0; i < 40; i++) p[i] = 0;
        ft(p, *(i64 *)(st + 104)); ft(p + 8, *(i64 *)(st + 72)); ft(p + 16, *(i64 *)(st + 88)); ft(p + 24, *(i64 *)(st + 88));
        *(u32 *)(p + 32) = dir ? 0x10 : 0x80; return 1;
    }
    if (cls == 1 && size >= 24) {                 /* FileStandardInfo */
        for (int i = 0; i < 24; i++) p[i] = 0;
        *(u64 *)p = sz; *(u64 *)(p + 8) = sz; *(u32 *)(p + 16) = 1; p[21] = (unsigned char)dir; return 1;
    }
    SetLastError(87); return 0;
}
EXPORT BOOL WINAPI SetFileInformationByHandle(HANDLE h, u32 cls, void *buf, u32 size) { (void)h; (void)cls; (void)buf; (void)size; SetLastError(120); return 0; }
EXPORT u32 WINAPI GetFinalPathNameByHandleW(HANDLE h, u16 *buf, u32 size, u32 flags) { (void)h; (void)buf; (void)size; (void)flags; SetLastError(120); return 0; }
EXPORT BOOL WINAPI LockFileEx(HANDLE h, u32 f, u32 r, u32 lo, u32 hi, void *ov) { (void)h; (void)f; (void)r; (void)lo; (void)hi; (void)ov; return 1; }
EXPORT BOOL WINAPI UnlockFile(HANDLE h, u32 a, u32 b, u32 c, u32 d) { (void)h; (void)a; (void)b; (void)c; (void)d; return 1; }
EXPORT BOOL WINAPI CancelIo(HANDLE h) { (void)h; return 1; }
EXPORT BOOL WINAPI GetOverlappedResult(HANDLE h, void *ov, u32 *n, BOOL wait) { (void)h; (void)ov; (void)n; (void)wait; SetLastError(6); return 0; }
EXPORT BOOL WINAPI ReadFileEx(HANDLE h, void *b, u32 n, void *ov, void *cb) { (void)h; (void)b; (void)n; (void)ov; (void)cb; SetLastError(120); return 0; }
EXPORT BOOL WINAPI WriteFileEx(HANDLE h, const void *b, u32 n, void *ov, void *cb) { (void)h; (void)b; (void)n; (void)ov; (void)cb; SetLastError(120); return 0; }
EXPORT BOOL WINAPI CopyFileExW(const u16 *a, const u16 *b, void *cb, void *d, BOOL *cancel, u32 flags) { (void)cb; (void)d; (void)cancel; return CopyFileW(a, b, (flags & 1) != 0); }
EXPORT BOOL WINAPI CreateHardLinkW(const u16 *a, const u16 *b, void *sa) { (void)a; (void)b; (void)sa; SetLastError(50); return 0; }
EXPORT BOOL WINAPI CreateSymbolicLinkW(const u16 *link, const u16 *target, u32 flags) {
    (void)flags; W2A(l, link); W2A(t, target); char x[512], y[512]; dos_path(x, t, sizeof x); dos_path(y, l, sizeof y);
    return ret(sc3(88, x, y, 0)) == 0;
}
EXPORT HANDLE WINAPI FindFirstFileExW(const u16 *n, int level, void *data, int op, void *filter, u32 flags) { (void)level; (void)op; (void)filter; (void)flags; return FindFirstFileW(n, data); }
EXPORT int WINAPI CompareStringOrdinal(const u16 *a, int na, const u16 *b, int nb, BOOL ic) { return CompareStringW(0, ic ? 1 : 0, a, na, b, nb); }
EXPORT u32 WINAPI FormatMessageW(u32 flags, const void *src, u32 id, u32 lang, u16 *buf, u32 size, void *args) {
    char tmp[64]; u32 n;
    if (flags & 0x100) { char *p; n = FormatMessageA(flags, src, id, lang, (char *)&p, 0, args); if (!n) return 0; u16 *w = LocalAlloc(0, (n + 1) * 2); if (!w) return 0; a2w(p, w, n + 1); *(u16 **)buf = w; return n; }
    n = FormatMessageA(0, src, id, lang, tmp, sizeof tmp, args); a2w(tmp, buf, size); return n;
}
EXPORT BOOL WINAPI GetModuleHandleExW(u32 flags, const u16 *name, HANDLE *out) { (void)flags; HANDLE h = GetModuleHandleW(name); if (out) *out = h; return h != 0; }
EXPORT u32 WINAPI GetProcessId(HANDLE h) { (void)h; return (u32)sc3(S_GETPID, 0, 0, 0); }
EXPORT HANDLE WINAPI CreateToolhelp32Snapshot(u32 f, u32 pid) { (void)f; (void)pid; SetLastError(120); return INVALID_HANDLE; }
EXPORT BOOL WINAPI Module32FirstW(HANDLE h, void *e) { (void)h; (void)e; return 0; }
EXPORT BOOL WINAPI Module32NextW(HANDLE h, void *e) { (void)h; (void)e; return 0; }
EXPORT BOOL WINAPI InitializeProcThreadAttributeList(void *l, u32 n, u32 f, u64 *size) { (void)l; (void)n; (void)f; if (size) *size = 64; return 1; }
EXPORT BOOL WINAPI UpdateProcThreadAttribute(void *l, u32 f, u64 a, void *v, u64 s, void *pv, u64 *r) { (void)l; (void)f; (void)a; (void)v; (void)s; (void)pv; (void)r; return 1; }
EXPORT void WINAPI DeleteProcThreadAttributeList(void *l) { (void)l; }
EXPORT BOOL WINAPI DeviceIoControl(HANDLE h, u32 code, void *in, u32 il, void *out, u32 ol, u32 *ret_len, void *ov) { (void)h; (void)code; (void)in; (void)il; (void)out; (void)ol; (void)ret_len; (void)ov; SetLastError(1); return 0; }
EXPORT HANDLE WINAPI CreateWaitableTimerExW(void *sa, const u16 *n, u32 f, u32 a) { (void)sa; (void)n; (void)f; (void)a; SetLastError(120); return 0; }
EXPORT BOOL WINAPI SetWaitableTimer(HANDLE h, const i64 *due, long period, void *cb, void *arg, BOOL resume) { (void)h; (void)due; (void)period; (void)cb; (void)arg; (void)resume; return 0; }
EXPORT BOOL WINAPI IsThreadAFiber(void) { return 0; }
EXPORT BOOL WINAPI SetThreadStackGuarantee(u32 *s) { (void)s; return 1; }
EXPORT void *WINAPI AddVectoredExceptionHandler(u32 first, void *h) { return RtlAddVectoredExceptionHandler(first, h); }
EXPORT BOOL WINAPI ReadConsoleW(HANDLE h, u16 *buf, u32 n, u32 *got, void *ctl) {
    (void)ctl; char tmp[512]; if (n > 120) n = 120;
    i64 r = sc3(S_READ, (i64)h, tmp, n); if (r <= 0) { if (got) *got = 0; return r == 0; }
    tmp[r] = 0; size_t w = a2w(tmp, buf, n + 1); if (got) *got = (u32)w; return 1;
}

#include "unwind.h"
/* RaiseException: build the record, offer it to the frame handlers, resume if one says so, else the process ends */
EXPORT void WINAPI RaiseException(u32 code, u32 flags, u32 n, const u64 *args) {
    unsigned char rec[0x98]; for (int i = 0; i < 0x98; i++) rec[i] = 0;
    unsigned char *ctx = (unsigned char *)__builtin_alloca(CX_SIZE + 16); ctx = (unsigned char *)(((u64)ctx + 15) & ~15ULL);
    *(u32 *)rec = code; *(u32 *)(rec + 4) = flags;
    if (n > 15) n = 15;
    *(u32 *)(rec + 0x18) = n; for (u32 i = 0; i < n; i++) *(u64 *)(rec + 0x20 + 8 * i) = args[i];
    RtlCaptureContext(ctx);
    *(u64 *)(rec + 0x10) = *(u64 *)(ctx + CX_RIP);
    if (uw_dispatch(rec, ctx)) RtlRestoreContext(ctx, rec);
    sc3(231, code & 0xFF ? code & 0xFF : 3, 0, 0);
    for (;;) {}
}
/* the C-language SEH personality lives in the built-in msvcrt: forward to it */
static void *find_in(const char *module, const char *want) {
    unsigned char *base = GetModuleHandleA(module);
    if (!base) return 0;
    u32 pe = *(u32 *)(base + 0x3C), exp = *(u32 *)(base + pe + 0x88);
    unsigned char *ed = base + exp;
    u32 n = *(u32 *)(ed + 0x18), *funcs = (u32 *)(base + *(u32 *)(ed + 0x1C)), *names = (u32 *)(base + *(u32 *)(ed + 0x20));
    unsigned short *ords = (unsigned short *)(base + *(u32 *)(ed + 0x24));
    for (u32 i = 0; i < n; i++) {
        const char *nm = (const char *)(base + names[i]); int k = 0;
        while (nm[k] && nm[k] == want[k]) k++;
        if (!nm[k] && !want[k]) return base + funcs[ords[i]];
    }
    return 0;
}
EXPORT int WINAPI __C_specific_handler(void *rec, void *frame, void *ctx, void *disp) {
    typedef int (WINAPI *h_t)(void *, void *, void *, void *);
    static h_t fn; if (!fn) fn = (h_t)find_in("msvcrt.dll", "__C_specific_handler");
    return fn ? fn(rec, frame, ctx, disp) : 1;
}

/* init-once: the INIT_ONCE is one pointer-sized word: 0 = fresh, 1 = being initialised, otherwise (context | 2) */
EXPORT BOOL WINAPI InitOnceBeginInitialize(volatile u64 *once, u32 flags, BOOL *pending, void **ctx) {
    for (;;) {
        u64 v = *once;
        if ((v & 3) == 2) { if (pending) *pending = 0; if (ctx) *ctx = (void *)(v & ~3ULL); return 1; }
        if (flags & 1) { if (pending) *pending = 1; return 1; }                      /* CHECK_ONLY */
        if (v == 0 && __sync_bool_compare_and_swap(once, 0, 1)) { if (pending) *pending = 1; return 1; }
        sc3(24, 0, 0, 0);
    }
}
EXPORT BOOL WINAPI InitOnceComplete(volatile u64 *once, u32 flags, void *ctx) {
    if (flags & 2) { *once = 0; return 1; }                                          /* INIT_ONCE_INIT_FAILED */
    *once = ((u64)ctx & ~3ULL) | 2; return 1;
}

/* file mappings */
typedef struct { i64 fd; u64 size; u32 prot; } MAPOBJ;
static struct { void *p; u64 n; } views[64];
static HANDLE map_create(HANDLE h, u32 prot, u32 hi, u32 lo) {
    MAPOBJ *m = LocalAlloc(0, sizeof *m); if (!m) return 0;
    m->fd = (i64)h; m->size = ((u64)hi << 32) | lo; m->prot = prot;
    if (m->size == 0 && (i64)h != -1) { GetFileSizeEx(h, (i64 *)&m->size); }
    return m;
}
EXPORT HANDLE WINAPI CreateFileMappingA(HANDLE h, void *sa, u32 prot, u32 hi, u32 lo, const char *n) { (void)sa; (void)n; return map_create(h, prot, hi, lo); }
EXPORT HANDLE WINAPI CreateFileMappingW(HANDLE h, void *sa, u32 prot, u32 hi, u32 lo, const u16 *n) { (void)sa; (void)n; return map_create(h, prot, hi, lo); }
EXPORT void *WINAPI MapViewOfFile(HANDLE hm, u32 access, u32 offhi, u32 offlo, u64 bytes) {
    MAPOBJ *m = hm; u64 len = bytes ? bytes : m->size; if (!len) { SetLastError(87); return 0; }
    int prot = 1 | ((access & 2) || (m->prot & 4) || (m->prot & 8) ? 2 : 0);
    int flags = (m->fd == -1) ? 0x22 : ((access & 4) ? 0x2 : 0x1);          /* anonymous, or shared / private file view */
    if (m->fd != -1 && (access & 1)) flags = 0x2;                            /* FILE_MAP_COPY */
    i64 p = sc6(S_MMAP, 0, (i64)len, prot, flags, m->fd, ((i64)offhi << 32) | offlo);
    if (p < 0 && p > -4096) { SetLastError(8); return 0; }
    for (int i = 0; i < 64; i++) if (!views[i].p) { views[i].p = (void *)p; views[i].n = len; break; }
    return (void *)p;
}
EXPORT BOOL WINAPI UnmapViewOfFile(void *p) {
    for (int i = 0; i < 64; i++) if (views[i].p == p) { sc3(S_MUNMAP, p, views[i].n, 0); views[i].p = 0; return 1; }
    SetLastError(998); return 0;
}


/* ======================================================================================================
   The heap: size-class free lists carved from big anonymous mappings; blocks above 32 KiB are mapped on their own.
   Every block has a 16-byte header (payload size, class), so HeapSize / realloc / free need nothing else.
   (The kernel's built-in HeapAlloc hands out whole pages and has no header: this one takes over for programs that
   import from kernel32, since the loader prefers kernel32x.)
   ====================================================================================================== */
#define H_CLASSES 11
#define H_BIG 0xFF
static void *h_free[H_CLASSES];
static char *h_arena, *h_arena_end;
static volatile int h_lock;
static void h_acquire(void) { while (__sync_lock_test_and_set(&h_lock, 1)) sc3(24, 0, 0, 0); }
static void h_release(void) { __sync_lock_release(&h_lock); }
static void *mem_alloc(u64 n, int zero) {
    u64 total = n + sizeof(mh);
    if (total < n) { SetLastError(8); return 0; }
    mh *b;
    if (total > 32768) {
        u64 map = (total + 4095) & ~4095ULL;
        char *p = (char *)sc6(S_MMAP, 0, (i64)map, 3, 0x22, -1, 0);
        if ((i64)p < 0 && (i64)p > -4096) { SetLastError(8); return 0; }
        b = (mh *)p; b->size = map - sizeof(mh); b->cls = H_BIG;
        return b + 1;                                                                  /* fresh pages are zero */
    }
    int cls = 0; u64 csz = 32; while (csz < total) { csz <<= 1; cls++; }
    h_acquire();
    if (h_free[cls]) { b = h_free[cls]; h_free[cls] = *(void **)(b + 1); }
    else {
        if (h_arena + csz > h_arena_end) {
            char *p = (char *)sc6(S_MMAP, 0, 1 << 20, 3, 0x22, -1, 0);
            if ((i64)p < 0 && (i64)p > -4096) { h_release(); SetLastError(8); return 0; }
            h_arena = p; h_arena_end = p + (1 << 20);
        }
        b = (mh *)h_arena; h_arena += csz;
    }
    h_release();
    b->size = csz - sizeof(mh); b->cls = (u64)cls;
    void *pl = b + 1;
    if (zero) { char *q = pl; for (u64 i = 0; i < b->size; i++) q[i] = 0; }
    return pl;
}
static void mem_free(void *p) {
    if (!p) return;
    mh *b = (mh *)p - 1;
    if (b->cls == H_BIG) { sc3(S_MUNMAP, b, b->size + sizeof(mh), 0); return; }
    h_acquire(); *(void **)p = h_free[b->cls]; h_free[b->cls] = b; h_release();
}
EXPORT void *WINAPI HeapAlloc(HANDLE heap, u32 flags, u64 n) { (void)heap; return mem_alloc(n ? n : 1, (flags & 8) != 0); }
EXPORT BOOL WINAPI HeapFree(HANDLE heap, u32 flags, void *p) { (void)heap; (void)flags; mem_free(p); return 1; }
EXPORT u64 WINAPI HeapSize(HANDLE heap, u32 flags, const void *p) { (void)heap; (void)flags; return p ? ((const mh *)p - 1)->size : (u64)-1; }
EXPORT void *WINAPI HeapReAlloc(HANDLE heap, u32 flags, void *p, u64 n) {
    (void)heap;
    if (!p) return mem_alloc(n ? n : 1, (flags & 8) != 0);
    mh *b = (mh *)p - 1;
    if (n <= b->size) return p;
    char *q = mem_alloc(n, (flags & 8) != 0);
    if (!q) return 0;
    char *s = p; for (u64 i = 0; i < b->size; i++) q[i] = s[i];
    mem_free(p);
    return q;
}
HANDLE WINAPI LocalAlloc(u32 flags, u64 n) { return mem_alloc(n ? n : 1, (flags & 0x40) != 0); }
EXPORT HANDLE WINAPI LocalFree(HANDLE p) { mem_free(p); return 0; }
EXPORT HANDLE WINAPI GlobalAlloc(u32 flags, u64 n) { return mem_alloc(n ? n : 1, (flags & 0x40) != 0); }
EXPORT HANDLE WINAPI GlobalFree(HANDLE p) { mem_free(p); return 0; }
EXPORT u64 WINAPI LocalSize(HANDLE p) { return p ? ((mh *)p - 1)->size : 0; }
EXPORT u64 WINAPI GlobalSize(HANDLE p) { return LocalSize(p); }
EXPORT HANDLE WINAPI LocalReAlloc(HANDLE p, u64 n, u32 f) { return HeapReAlloc(0, (f & 0x40) ? 8 : 0, p, n); }
EXPORT HANDLE WINAPI GlobalReAlloc(HANDLE p, u64 n, u32 f) { return HeapReAlloc(0, (f & 0x40) ? 8 : 0, p, n); }


/* ======================================================================================================
   Processes: CreateProcess over fork + execve (ELF and PE programs alike), process handles that WaitForSingleObject,
   GetExitCodeProcess, TerminateProcess and CloseHandle understand.
   ====================================================================================================== */
#define PH_BASE 0x40000000ULL
#define PH_MAX 64
static struct { int pid; int done; int code; } procs[PH_MAX];
static int is_ph(HANDLE h) { u64 v = (u64)h; return v >= PH_BASE && v < PH_BASE + PH_MAX; }
static void ph_poll(int idx, int block_ms) {                       /* block_ms < 0: wait forever */
    if (procs[idx].done) return;
    i64 waited = 0;
    for (;;) {
        int status = 0;
        i64 r = sc6(61 /* wait4 */, procs[idx].pid, (i64)&status, 1 /* WNOHANG */, 0, 0, 0);
        if (r == procs[idx].pid) { procs[idx].done = 1; procs[idx].code = (status & 0x7F) ? (0xC0000000 | (status & 0x7F)) : ((status >> 8) & 0xFF); return; }
        if (r < 0 && r != 0) { procs[idx].done = 1; procs[idx].code = 0; return; }
        if (block_ms >= 0 && waited >= block_ms) return;
        i64 ts[2] = { 0, 5000000 }; sc3(35, ts, 0, 0); waited += 5;
    }
}
static void *kernel32_builtin(const char *name) { return find_in("kernel32.dll", name); }
EXPORT u32 WINAPI WaitForSingleObject(HANDLE h, u32 ms) {
    if (is_ph(h)) { int idx = (int)((u64)h - PH_BASE); ph_poll(idx, ms == 0xFFFFFFFFu ? -1 : (int)ms); return procs[idx].done ? 0 : 0x102; }
    typedef u32 (WINAPI *f_t)(HANDLE, u32); static f_t f; if (!f) f = (f_t)kernel32_builtin("WaitForSingleObject");
    return f ? f(h, ms) : 0xFFFFFFFFu;
}
EXPORT BOOL WINAPI CloseHandle(HANDLE h) {
    if (is_ph(h) || ((u64)h >= PH_BASE + 0x10000 && (u64)h < PH_BASE + 0x10000 + PH_MAX)) return 1;
    typedef BOOL (WINAPI *f_t)(HANDLE); static f_t f; if (!f) f = (f_t)kernel32_builtin("CloseHandle");
    return f ? f(h) : 0;
}
EXPORT BOOL WINAPI GetExitCodeProcess(HANDLE h, u32 *c) {
    if (!is_ph(h)) { if (c) *c = 259; return 1; }
    int idx = (int)((u64)h - PH_BASE); ph_poll(idx, 0);
    if (c) *c = procs[idx].done ? (u32)procs[idx].code : 259;
    return 1;
}
EXPORT BOOL WINAPI TerminateProcess(HANDLE h, u32 code) {
    if ((i64)h == -1) sc3(231, code & 0xFF, 0, 0);
    if (is_ph(h)) { sc3(62, procs[(int)((u64)h - PH_BASE)].pid, 9, 0); return 1; }
    SetLastError(5); return 0;
}
static int split_cmd(char *cmd, char **argv, int max) {            /* Windows command-line rules (quotes group, backslashes only before quotes) */
    int n = 0; char *p = cmd;
    while (*p && n < max - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *out = p; argv[n++] = out;
        int inq = 0;
        while (*p && (inq || (*p != ' ' && *p != '\t'))) {
            int bs = 0; while (*p == '\\') { bs++; p++; }
            if (*p == '"') { for (int i = 0; i < bs / 2; i++) *out++ = '\\'; if (bs & 1) *out++ = '"'; else inq = !inq; p++; }
            else { for (int i = 0; i < bs; i++) *out++ = '\\'; if (*p) *out++ = *p++; }
        }
        if (*p) p++;
        *out = 0;
    }
    argv[n] = 0;
    return n;
}
static int file_exists(const char *posix) { unsigned char st[144]; return sc6(S_NEWFSTATAT, -100, (i64)posix, (i64)st, 0, 0, 0) == 0; }
static BOOL create_process(const char *app, char *cmd, void *si, int wide_si, void *pi) {
    (void)wide_si;
    char buf[2048]; cpy(buf, cmd ? cmd : app, sizeof buf);
    char *argv[32]; int argc = split_cmd(buf, argv, 32);
    const char *prog = app ? app : (argc ? argv[0] : 0);
    if (!prog || !*prog) { SetLastError(2); return 0; }
    char path[600]; dos_path(path, prog, sizeof path);
    if (!file_exists(path)) {
        size_t l = slen(path); int has_dot = 0; for (size_t i = 0; i < l; i++) if (path[i] == '.') has_dot = i + 1;
        char alt[640]; cpy(alt, path, sizeof alt); if (l + 4 < sizeof alt) { alt[l] = '.'; alt[l + 1] = 'e'; alt[l + 2] = 'x'; alt[l + 3] = 'e'; alt[l + 4] = 0; }
        (void)has_dot;
        if (file_exists(alt)) cpy(path, alt, sizeof path);
        else if (path[0] != '/' || !file_exists(path)) {                    /* a bare name: the root and the usual places */
            const char *dirs[] = { "/", "/bin/", "/usr/bin/", "/Windows/System32/" };
            int found = 0;
            for (int d = 0; d < 4 && !found; d++) {
                char t[700]; cpy(t, dirs[d], sizeof t); size_t n = slen(t); const char *base = path; if (base[0] == '/') base++;
                cpy(t + n, base, sizeof t - n); if (file_exists(t)) { cpy(path, t, sizeof path); found = 1; break; }
                n = slen(t); cpy(t + n, ".exe", sizeof t - n); if (file_exists(t)) { cpy(path, t, sizeof path); found = 1; }
            }
            if (!found) { SetLastError(2); return 0; }
        }
    }
    int h_in = -1, h_out = -1, h_err = -1;
    if (si) { unsigned char *s = si; if (*(u32 *)(s + 60) & 0x100) { h_in = (int)*(i64 *)(s + 80); h_out = (int)*(i64 *)(s + 88); h_err = (int)*(i64 *)(s + 96); } }
    int slot = -1; for (int i = 0; i < PH_MAX; i++) if (!procs[i].pid) { slot = i; break; }
    if (slot < 0) { SetLastError(8); return 0; }
    i64 pid = sc3(57 /* fork */, 0, 0, 0);
    if (pid < 0) { SetLastError(8); return 0; }
    if (pid == 0) {
        if (h_in >= 0 && h_in != 0) sc3(33, h_in, 0, 0);
        if (h_out >= 0 && h_out != 1) sc3(33, h_out, 1, 0);
        if (h_err >= 0 && h_err != 2) sc3(33, h_err, 2, 0);
        if (!argc) { argv[0] = path; argv[1] = 0; }
        char *envp[1] = { 0 };
        sc3(59, path, argv, envp);
        sc3(231, 127, 0, 0);
    }
    procs[slot].pid = (int)pid; procs[slot].done = 0; procs[slot].code = 0;
    if (pi) { u64 *o = pi; o[0] = PH_BASE + (u64)slot; o[1] = PH_BASE + 0x10000 + (u64)slot; ((u32 *)o)[4] = (u32)pid; ((u32 *)o)[5] = (u32)pid; }
    return 1;
}
EXPORT BOOL WINAPI CreateProcessA(const char *app, char *cmd, void *pa, void *ta, BOOL inh, u32 fl, void *env, const char *cwd, void *si, void *pi) {
    (void)pa; (void)ta; (void)inh; (void)fl; (void)env; (void)cwd; return create_process(app, cmd, si, 0, pi);
}
EXPORT BOOL WINAPI CreateProcessW(const u16 *app, u16 *cmd, void *pa, void *ta, BOOL inh, u32 fl, void *env, const u16 *cwd, void *si, void *pi) {
    (void)pa; (void)ta; (void)inh; (void)fl; (void)env; (void)cwd;
    char a[1024], c[2048]; if (app) w2a(app, a, sizeof a); if (cmd) w2a(cmd, c, sizeof c);
    return create_process(app ? a : 0, cmd ? c : 0, si, 1, pi);
}
EXPORT BOOL WINAPI GetStdHandle_dummy_unused(void) { return 0; }

__attribute__((dllexport)) int __stdcall DllMain(void *h, unsigned reason, void *res) { (void)h; (void)reason; (void)res; return 1; }
