// SPDX-License-Identifier: GPL-2.0-or-later
// msvcrtx.dll — THOS's C runtime extension for Windows programs. The kernel's built-in msvcrt only
// knows ~35 functions; the loader asks this DLL first for everything a program imports from
// msvcrt.dll, so every function defined here wins. It is a small freestanding C library that talks
// to the kernel with plain system calls (files are POSIX file descriptors, which is what the
// stdio FILE objects of the built-in msvcrt already assume: the descriptor sits at FILE offset 28).
//
// Build:  x86_64-w64-mingw32-gcc -O2 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -shared
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>

#define EXPORT __attribute__((dllexport))

typedef long long i64;
typedef unsigned long long u64;
typedef unsigned int u32;

/* ---- system calls (the Linux ABI the kernel implements) ---- */
static inline i64 sc6(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    i64 r;
    register i64 r10 __asm__("r10") = d;
    register i64 r8 __asm__("r8") = e;
    register i64 r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
#define sc3(n, a, b, c) sc6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)
enum { S_READ = 0, S_WRITE = 1, S_OPEN = 2, S_CLOSE = 3, S_LSEEK = 8, S_MMAP = 9, S_MUNMAP = 11, S_RENAME = 82, S_MKDIR = 83, S_UNLINK = 87, S_CLOCK_GETTIME = 228, S_GETPID = 39 };

static int *errno_ptr(void);
static i64 sys_ret(i64 r) {
    if (r < 0 && r > -4096) { *errno_ptr() = (int)-r; return -1; }
    return r;
}

/* Share the native CRT errno cell with the executable and other system calls. */
__declspec(dllimport) extern int *_errno(void);
static int *errno_ptr(void) { return _errno(); }

/* ---- memory and strings ---- */
EXPORT void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dp = d; const unsigned char *sp = s;
    if (dp < sp) { for (size_t i = 0; i < n; i++) dp[i] = sp[i]; }
    else { for (size_t i = n; i > 0; i--) dp[i - 1] = sp[i - 1]; }
    return d;
}
EXPORT int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
EXPORT void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return 0;
}
static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
EXPORT int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
EXPORT char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)); return r; }
EXPORT char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
EXPORT char *strcat(char *d, const char *s) { strcpy(d + slen(d), s); return d; }
EXPORT char *strncat(char *d, const char *s, size_t n) {
    char *e = d + slen(d); size_t i = 0;
    for (; i < n && s[i]; i++) e[i] = s[i];
    e[i] = 0;
    return d;
}
EXPORT char *strchr(const char *s, int c) {
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return 0; }
}
EXPORT char *strrchr(const char *s, int c) {
    const char *r = 0;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char *)r; }
}
EXPORT char *strstr(const char *h, const char *n) {
    size_t nl = slen(n);
    if (!nl) return (char *)h;
    for (; *h; h++) if (*h == *n && !memcmp(h, n, nl)) return (char *)h;
    return 0;
}
EXPORT size_t strspn(const char *s, const char *set) { size_t n = 0; while (s[n] && strchr(set, s[n])) n++; return n; }
EXPORT size_t strcspn(const char *s, const char *set) { size_t n = 0; while (s[n] && !strchr(set, s[n])) n++; return n; }
EXPORT char *strpbrk(const char *s, const char *set) { s += strcspn(s, set); return *s ? (char *)s : 0; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
EXPORT int _stricmp(const char *a, const char *b) {
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) { a++; b++; }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}
EXPORT int _strnicmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        int d = lower((unsigned char)*a) - lower((unsigned char)*b);
        if (d || !*a) return d;
    }
    return 0;
}
EXPORT char *strtok(char *s, const char *delim) {
    static char *save;
    if (s) save = s;
    if (!save) return 0;
    save += strspn(save, delim);
    if (!*save) { save = 0; return 0; }
    char *start = save;
    save += strcspn(save, delim);
    if (*save) *save++ = 0; else save = 0;
    return start;
}

/* ---- ctype ---- */
EXPORT int toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
EXPORT int tolower(int c) { return lower(c); }
EXPORT int isdigit(int c) { return c >= '0' && c <= '9'; }
EXPORT int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
EXPORT int isalnum(int c) { return isalpha(c) || isdigit(c); }
EXPORT int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
EXPORT int isupper(int c) { return c >= 'A' && c <= 'Z'; }
EXPORT int islower(int c) { return c >= 'a' && c <= 'z'; }
EXPORT int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
EXPORT int isprint(int c) { return c >= 32 && c < 127; }
EXPORT int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
EXPORT int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }

/* ---- allocator: size-class free lists on top of anonymous mmap ---- */
typedef struct hdr { u64 size; u64 magic; } hdr;     /* 16 bytes in front of every block */
#define CLASSES 14                                     /* 32 B .. 256 KiB */
static hdr *freelist[CLASSES];
static char *arena, *arena_end;
static int class_of(size_t n) { int c = 0; size_t s = 32; while (s < n && c < CLASSES - 1) { s <<= 1; c++; } return c; }
static void *big_alloc(size_t n) {
    size_t total = (n + sizeof(hdr) + 4095) & ~(size_t)4095;
    void *p = (void *)sc6(S_MMAP, 0, total, 3, 0x22, -1, 0);
    if ((i64)p < 0 && (i64)p > -4096) return 0;
    hdr *h = p; h->size = total - sizeof(hdr); h->magic = 0xB16;
    return h + 1;
}
EXPORT void *malloc(size_t n) {
    if (n == 0) n = 1;
    if (n > (256u << 10) - sizeof(hdr)) return big_alloc(n);
    int c = class_of(n + sizeof(hdr));
    if (freelist[c]) { hdr *h = freelist[c]; freelist[c] = *(hdr **)(h + 1); return h + 1; }
    size_t bs = (size_t)32 << c;
    if (arena + bs > arena_end) {
        size_t chunk = bs > (1u << 20) ? bs : (1u << 20);
        void *p = (void *)sc6(S_MMAP, 0, chunk, 3, 0x22, -1, 0);
        if ((i64)p < 0 && (i64)p > -4096) return 0;
        arena = p; arena_end = arena + chunk;
    }
    hdr *h = (hdr *)arena; arena += bs;
    h->size = bs - sizeof(hdr); h->magic = 0xA11C0 + c;
    return h + 1;
}
EXPORT void free(void *p) {
    if (!p) return;
    hdr *h = (hdr *)p - 1;
    if (h->magic == 0xB16) { sc3(S_MUNMAP, h, h->size + sizeof(hdr), 0); return; }
    int c = (int)(h->magic - 0xA11C0);
    if (c < 0 || c >= CLASSES) return;
    *(hdr **)p = freelist[c]; freelist[c] = h;
}
EXPORT void *calloc(size_t a, size_t b) {
    size_t n = a * b;
    char *p = malloc(n);
    if (p) for (size_t i = 0; i < n; i++) p[i] = 0;   /* fresh mmap memory is zero, recycled blocks are not */
    return p;
}
EXPORT void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    hdr *h = (hdr *)p - 1;
    if (n <= h->size) return p;
    void *q = malloc(n);
    if (!q) return 0;
    char *d = q, *s = p;
    for (u64 i = 0; i < h->size; i++) d[i] = s[i];
    free(p);
    return q;
}
EXPORT char *_strdup(const char *s) { size_t n = slen(s) + 1; char *d = malloc(n); if (d) memmove(d, s, n); return d; }
EXPORT char *strdup(const char *s) { return _strdup(s); }

/* ---- numbers ---- */
static int digit_val(int c) { if (isdigit(c)) return c - '0'; if (c >= 'a' && c <= 'z') return c - 'a' + 10; if (c >= 'A' && c <= 'Z') return c - 'A' + 10; return 99; }
static unsigned long long conv(const char *s, char **end, int base, int *neg) {
    while (isspace((unsigned char)*s)) s++;
    *neg = 0;
    if (*s == '-') { *neg = 1; s++; } else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') base = 8;
    else if (base == 0) base = 10;
    unsigned long long v = 0;
    const char *start = s;
    while (digit_val((unsigned char)*s) < base) { v = v * base + digit_val((unsigned char)*s); s++; }
    if (end) *end = (char *)(s == start ? start : s);
    return v;
}
EXPORT long strtol(const char *s, char **end, int base) { int neg; unsigned long long v = conv(s, end, base, &neg); return neg ? -(long)v : (long)v; }
EXPORT unsigned long strtoul(const char *s, char **end, int base) { int neg; unsigned long long v = conv(s, end, base, &neg); return neg ? -(unsigned long)v : (unsigned long)v; }
EXPORT long long _strtoi64(const char *s, char **end, int base) { int neg; unsigned long long v = conv(s, end, base, &neg); return neg ? -(long long)v : (long long)v; }
EXPORT long long strtoll(const char *s, char **end, int base) { return _strtoi64(s, end, base); }
EXPORT unsigned long long strtoull(const char *s, char **end, int base) { int neg; unsigned long long v = conv(s, end, base, &neg); return neg ? -v : v; }
EXPORT int atoi(const char *s) { return (int)strtol(s, 0, 10); }
EXPORT long atol(const char *s) { return strtol(s, 0, 10); }
EXPORT long long _atoi64(const char *s) { return _strtoi64(s, 0, 10); }
EXPORT double strtod(const char *s, char **end) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    double v = 0; int any = 0;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p++ - '0'); any = 1; }
    if (*p == '.') { p++; double f = 0.1; while (isdigit((unsigned char)*p)) { v += (*p++ - '0') * f; f /= 10; any = 1; } }
    if (any && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1; int en = 0, ex = 0;
        if (*q == '-') { en = 1; q++; } else if (*q == '+') q++;
        if (isdigit((unsigned char)*q)) { while (isdigit((unsigned char)*q)) ex = ex * 10 + (*q++ - '0'); while (ex--) v = en ? v / 10 : v * 10; p = q; }
    }
    if (end) *end = (char *)(any ? p : s);
    return neg ? -v : v;
}
EXPORT double atof(const char *s) { return strtod(s, 0); }
EXPORT int abs(int x) { return x < 0 ? -x : x; }
EXPORT long labs(long x) { return x < 0 ? -x : x; }
static unsigned long long rng = 1;
EXPORT void srand(unsigned s) { rng = s; }
EXPORT int rand(void) { rng = rng * 6364136223846793005ULL + 1442695040888963407ULL; return (int)((rng >> 33) & 0x7fff); }

EXPORT void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    char *a = base;
    if (n < 2) return;
    /* heap sort: no recursion, no allocation, O(n log n) */
    char tmp[256];
    if (sz > sizeof tmp) {                    /* huge elements: insertion sort through malloc */
        char *t = malloc(sz);
        if (!t) return;
        for (size_t i = 1; i < n; i++) {
            memmove(t, a + i * sz, sz);
            size_t j = i;
            while (j > 0 && cmp(a + (j - 1) * sz, t) > 0) { memmove(a + j * sz, a + (j - 1) * sz, sz); j--; }
            memmove(a + j * sz, t, sz);
        }
        free(t);
        return;
    }
#define EL(i) (a + (size_t)(i) * sz)
    for (size_t start = n / 2; start-- > 0;) {
        size_t root = start;
        for (;;) {
            size_t child = 2 * root + 1;
            if (child >= n) break;
            if (child + 1 < n && cmp(EL(child), EL(child + 1)) < 0) child++;
            if (cmp(EL(root), EL(child)) >= 0) break;
            memmove(tmp, EL(root), sz); memmove(EL(root), EL(child), sz); memmove(EL(child), tmp, sz);
            root = child;
        }
    }
    for (size_t end = n - 1; end > 0; end--) {
        memmove(tmp, EL(0), sz); memmove(EL(0), EL(end), sz); memmove(EL(end), tmp, sz);
        size_t root = 0;
        for (;;) {
            size_t child = 2 * root + 1;
            if (child >= end) break;
            if (child + 1 < end && cmp(EL(child), EL(child + 1)) < 0) child++;
            if (cmp(EL(root), EL(child)) >= 0) break;
            memmove(tmp, EL(root), sz); memmove(EL(root), EL(child), sz); memmove(EL(child), tmp, sz);
            root = child;
        }
    }
#undef EL
}
EXPORT void *bsearch(const void *key, const void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *e = (const char *)base + mid * sz;
        int c = cmp(key, e);
        if (!c) return (void *)e;
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return 0;
}

/* ---- time ---- */
EXPORT i64 _time64(i64 *t) {
    struct { i64 s, ns; } ts;
    sc3(S_CLOCK_GETTIME, 0, &ts, 0);
    if (t) *t = ts.s;
    return ts.s;
}
EXPORT i64 time(i64 *t) { return _time64(t); }
EXPORT long clock(void) {
    struct { i64 s, ns; } ts;
    sc3(S_CLOCK_GETTIME, 1, &ts, 0);   /* monotonic */
    return (long)(ts.s * 1000 + ts.ns / 1000000);
}

/* ---- stdio ---- */
/* A FILE keeps the descriptor at offset 28 like the built-in msvcrt's three standard streams
   (stdin/stdout/stderr come from its __iob_func and have no private part: raw, unbuffered). */
typedef struct FILEX {
    char pad0[28]; int fd; char pad1[16];             /* the 48-byte Microsoft layout */
    u64 magic; int eof, err; int rpos, rlen; char rbuf[4096];
} FILEX;
#define MAGIC 0x54484F53464C45ULL
static int fdof(void *f) { return *(int *)((char *)f + 28); }
static FILEX *mine(void *f) { return ((FILEX *)f)->magic == MAGIC ? (FILEX *)f : 0; }

static void dos_path(char *out, const char *in, size_t cap) {
    size_t i = 0;
    if (in[0] && in[1] == ':') in += 2;             /* C:\x -> \x */
    for (; *in && i + 1 < cap; in++, i++) out[i] = *in == '\\' ? '/' : *in;
    out[i] = 0;
}
EXPORT void *fopen(const char *path, const char *mode) {
    char p[512]; dos_path(p, path, sizeof p);
    int flags = 0, rd = 0, wr = 0, app = 0;
    for (const char *m = mode; *m; m++) {
        if (*m == 'r') rd = 1; else if (*m == 'w') wr = 1; else if (*m == 'a') app = 1; else if (*m == '+') { rd = 1; wr = 1; }
    }
    if (mode[0] == 'r') { flags = (wr ? 2 : 0); }
    else if (mode[0] == 'w') { flags = (rd ? 2 : 1) | 0100 | 01000; }
    else { flags = (rd ? 2 : 1) | 0100 | 02000; }
    int fd = (int)sys_ret(sc3(S_OPEN, p, flags, 0644));
    if (fd < 0) return 0;
    FILEX *f = calloc(1, sizeof(FILEX));
    if (!f) { sc3(S_CLOSE, fd, 0, 0); return 0; }
    f->fd = fd; f->magic = MAGIC;
    return f;
}
EXPORT int fclose(void *fp) {
    FILEX *f = mine(fp);
    if (!f) return 0;
    sc3(S_CLOSE, f->fd, 0, 0);
    f->magic = 0;
    free(f);
    return 0;
}
static int rd_fill(FILEX *f) {
    i64 n = sc3(S_READ, f->fd, f->rbuf, sizeof f->rbuf);
    if (n <= 0) { f->eof = 1; if (n < 0) f->err = 1; return -1; }
    f->rpos = 0; f->rlen = (int)n;
    return 0;
}
EXPORT int fgetc(void *fp) {
    FILEX *f = mine(fp);
    if (!f) { unsigned char c; return sc3(S_READ, fdof(fp), &c, 1) == 1 ? c : -1; }
    if (f->rpos >= f->rlen && rd_fill(f) < 0) return -1;
    return (unsigned char)f->rbuf[f->rpos++];
}
EXPORT int getc(void *fp) { return fgetc(fp); }
EXPORT int ungetc(int c, void *fp) {
    FILEX *f = mine(fp);
    if (!f || c < 0 || f->rpos == 0) return -1;
    f->rbuf[--f->rpos] = (char)c; f->eof = 0;
    return c;
}
EXPORT char *fgets(char *s, int n, void *fp) {
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(fp);
        if (c < 0) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = 0;
    return s;
}
EXPORT size_t fread(void *buf, size_t sz, size_t cnt, void *fp) {
    size_t total = sz * cnt, got = 0;
    char *d = buf;
    while (got < total) {
        int c = fgetc(fp);
        if (c < 0) break;
        d[got++] = (char)c;
    }
    return sz ? got / sz : 0;
}
EXPORT size_t fwrite(const void *buf, size_t sz, size_t cnt, void *fp) {
    size_t total = sz * cnt, done = 0;
    int fd = fdof(fp);
    while (done < total) {
        i64 n = sc3(S_WRITE, fd, (const char *)buf + done, total - done);
        if (n <= 0) { FILEX *f = mine(fp); if (f) f->err = 1; break; }
        done += (size_t)n;
    }
    return sz ? done / sz : 0;
}
EXPORT int fputs(const char *s, void *fp) { size_t n = slen(s); return fwrite(s, 1, n, fp) == n ? 0 : -1; }
EXPORT int fputc(int c, void *fp) { char ch = (char)c; return fwrite(&ch, 1, 1, fp) == 1 ? (unsigned char)ch : -1; }
EXPORT int putc(int c, void *fp) { return fputc(c, fp); }
EXPORT int feof(void *fp) { FILEX *f = mine(fp); return f ? f->eof : 0; }
EXPORT int ferror(void *fp) { FILEX *f = mine(fp); return f ? f->err : 0; }
EXPORT int fflush(void *fp) { (void)fp; return 0; }
EXPORT int fseek(void *fp, long off, int whence) {
    FILEX *f = mine(fp);
    if (f) { if (whence == 1) off -= f->rlen - f->rpos; f->rpos = f->rlen = 0; f->eof = 0; }
    return sys_ret(sc3(S_LSEEK, fdof(fp), off, whence)) < 0 ? -1 : 0;
}
EXPORT long ftell(void *fp) {
    FILEX *f = mine(fp);
    i64 pos = sc3(S_LSEEK, fdof(fp), 0, 1);
    if (pos < 0) return -1;
    return (long)(pos - (f ? f->rlen - f->rpos : 0));
}
EXPORT void rewind(void *fp) { fseek(fp, 0, 0); }
EXPORT int remove(const char *p) { char q[512]; dos_path(q, p, sizeof q); return sys_ret(sc3(S_UNLINK, q, 0, 0)) < 0 ? -1 : 0; }
EXPORT int rename(const char *a, const char *b) { char x[512], y[512]; dos_path(x, a, sizeof x); dos_path(y, b, sizeof y); return sys_ret(sc3(S_RENAME, x, y, 0)) < 0 ? -1 : 0; }
EXPORT int _mkdir(const char *p) { char q[512]; dos_path(q, p, sizeof q); return sys_ret(sc3(S_MKDIR, q, 0755, 0)) < 0 ? -1 : 0; }

/* ---- printf ---- */
typedef struct { char *buf; size_t cap, len; } out_t;
static void put(out_t *o, char c) { if (o->len + 1 < o->cap) o->buf[o->len] = c; o->len++; }
static void pad(out_t *o, int n, char c) { while (n-- > 0) put(o, c); }
static int fmt_uint(char *tmp, unsigned long long v, int base, int upper) {
    int n = 0; const char *dg = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (!v) tmp[n++] = '0';
    while (v) { tmp[n++] = dg[v % base]; v /= base; }
    return n;
}

static int put_utf8(char *u, unsigned c) {
    if (c < 0x80) { u[0] = (char)c; return 1; }
    if (c < 0x800) { u[0] = (char)(0xC0 | (c >> 6)); u[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { u[0] = (char)(0xE0 | (c >> 12)); u[1] = (char)(0x80 | ((c >> 6) & 63)); u[2] = (char)(0x80 | (c & 63)); return 3; }
    u[0] = (char)(0xF0 | (c >> 18)); u[1] = (char)(0x80 | ((c >> 12) & 63)); u[2] = (char)(0x80 | ((c >> 6) & 63)); u[3] = (char)(0x80 | (c & 63)); return 4;
}
/* 10^n in extended precision */
static long double p10(int n) {
    long double r = 1, b = 10; int neg = n < 0; if (neg) n = -n;
    while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
    return neg ? 1 / r : r;
}
static int is_inf(double d) { union { double d; u64 u; } x = { d }; return (x.u & 0x7fffffffffffffffULL) == 0x7ff0000000000000ULL; }
static int is_nan(double d) { return d != d; }
/* the first `ndig` significant decimal digits of d > 0 (digit 0 is for 10^e10), rounded half up; only the first 17
   are meaningful (like the Microsoft runtime the rest are zeros). Returns how many digits are valid: ndig, or ndig + 1
   when rounding carried into a new leading digit. */
static int exp10_of(double d) {
    union { double d; u64 u; } x = { d };
    int be = (int)((x.u >> 52) & 0x7ff) - 1023;
    if (be == -1023) be = -1022;
    int e = (int)((be * 30103LL) / 100000) - (be < 0);
    long double s = (long double)d / p10(e);
    while (s >= 10) { s /= 10; e++; }
    while (s < 1) { s *= 10; e--; }
    return e;
}
static int digits_of(double d, int ndig, char *out, int *e10) {
    union { double d; u64 u; } x = { d };
    int be = (int)((x.u >> 52) & 0x7ff) - 1023;
    if (be == -1023) be = -1022;
    int e = (int)((be * 30103LL) / 100000) - (be < 0);
    long double s = (long double)d / p10(e);
    while (s >= 10) { s /= 10; e++; }
    while (s < 1) { s *= 10; e--; }
    if (ndig > 40) ndig = 40;
    int real = ndig < 17 ? ndig : 17;
    for (int i = 0; i < real; i++) { int dg = (int)s; if (dg > 9) dg = 9; out[i] = (char)dg; s = (s - dg) * 10; }
    for (int i = real; i < ndig + 1; i++) out[i] = 0;
    int carried = 0;
    if (s >= 5) {                                   /* round half up on the remaining digits */
        int i = real - 1;
        while (i >= 0 && ++out[i] == 10) { out[i] = 0; i--; }
        if (i < 0) { for (int k = real; k > 0; k--) out[k] = out[k - 1]; out[0] = 1; e++; carried = 1; }
    }
    *e10 = e;
    return ndig + carried;
}
static void fmt_double(out_t *o, double d, char c, int width, int prec, int left, int zero, int plus, int space, int alt) {
    int neg = (d < 0) || (d == 0 && 1 / d < 0), upper = c == 'E' || c == 'G' || c == 'F';
    if (neg) d = -d;
    char sign = neg ? '-' : plus ? '+' : space ? ' ' : 0;
    if (is_nan(d) || is_inf(d)) {
        const char *t = is_nan(d) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        int total = 3 + (sign != 0);
        if (!left) pad(o, width - total, ' ');
        if (sign) put(o, sign);
        while (*t) put(o, *t++);
        if (left) pad(o, width - total, ' ');
        return;
    }
    if (prec < 0) prec = 6;
    char dg[48]; int e10 = 0, nd;
    char body[400]; int bn = 0;
    char lc = (char)(c | 32);
    if (lc == 'g' && prec == 0) prec = 1;
    if (d == 0) { for (int i = 0; i < 48; i++) dg[i] = 0; e10 = 0; }
    int mode = lc;                                   /* 'e', 'f' or 'g' */
    int use_e = lc == 'e';
    int p_e = prec;                                  /* digits after the point in the chosen style */
    if (lc == 'g') {
        if (d != 0) digits_of(d, prec, dg, &e10);
        int x = e10;
        if (x < -4 || x >= prec) { use_e = 1; p_e = prec - 1; } else { use_e = 0; p_e = prec - 1 - x; }
        mode = 'g';
    }
    if (use_e) {
        if (d != 0) nd = digits_of(d, p_e + 1, dg, &e10); else nd = p_e + 1;
        body[bn++] = (char)('0' + dg[0]);
        if (p_e > 0 || alt) body[bn++] = '.';
        for (int i = 1; i <= p_e; i++) body[bn++] = (char)('0' + (i < nd ? dg[i] : 0));
        body[bn++] = upper ? 'E' : 'e';
        int ex = d == 0 ? 0 : e10; body[bn++] = ex < 0 ? '-' : '+'; if (ex < 0) ex = -ex;
        if (ex >= 100) body[bn++] = (char)('0' + ex / 100);
        body[bn++] = (char)('0' + ex / 10 % 10); body[bn++] = (char)('0' + ex % 10);
    } else {
        /* fixed notation: integer digits then p_e fraction digits */
        for (int i = 0; i < 48; i++) dg[i] = 0;
        nd = 48;
        if (d == 0) e10 = 0;
        else {
            e10 = exp10_of(d);
            int total_dig = e10 + 1 + p_e;
            if (total_dig > 0) nd = digits_of(d, total_dig > 40 ? 40 : total_dig, dg, &e10);
            else if (total_dig == 0 && (long double)d * p10(p_e) >= 0.5L) { if (p_e == 0) { e10 = 0; dg[0] = 1; } else { e10 = -1; dg[p_e - 1] = 1; } }
            else e10 = -1;                                  /* rounds to zero */
        }
        int ip = e10 + 1;                           /* digits before the point */
        if (ip <= 0) body[bn++] = '0';
        else for (int i = 0; i < ip; i++) body[bn++] = (char)('0' + (i < nd ? dg[i] : 0));
        if (p_e > 0 || alt) body[bn++] = '.';
        for (int k = 0; k < p_e; k++) {
            int idx = ip + k;                       /* index into the digit string (negative = leading zeros) */
            body[bn++] = (char)('0' + (idx >= 0 && idx < nd ? dg[idx] : 0));
        }
    }
    if (mode == 'g' && !alt) {                      /* %g drops trailing zeros (and a bare point) */
        int ep = bn; for (int i = 0; i < bn; i++) if (body[i] == 'e' || body[i] == 'E') { ep = i; break; }
        int dot = -1; for (int i = 0; i < ep; i++) if (body[i] == '.') dot = i;
        if (dot >= 0) {
            int z = ep; while (z > dot + 1 && body[z - 1] == '0') z--;
            if (z == dot + 1) z = dot;
            for (int i = ep; i < bn; i++) body[z++] = body[i];
            bn = z;
        }
    }
    int total = bn + (sign != 0);
    if (!left && !zero) pad(o, width - total, ' ');
    if (sign) put(o, sign);
    if (!left && zero) pad(o, width - total, '0');
    for (int i = 0; i < bn; i++) put(o, body[i]);
    if (left) pad(o, width - total, ' ');
}
static int vfmt(out_t *o, const char *f, va_list ap, int wide) {
    for (; *f; f++) {
        if (*f != '%') { put(o, *f); continue; }
        f++;
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1, lng = 0;
        for (;; f++) {
            if (*f == '-') left = 1; else if (*f == '0') zero = 1; else if (*f == '+') plus = 1; else if (*f == ' ') space = 1; else if (*f == '#') alt = 1; else break;
        }
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
        else while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
        if (*f == '.') { f++; prec = 0; if (*f == '*') { prec = va_arg(ap, int); f++; } else while (isdigit((unsigned char)*f)) prec = prec * 10 + (*f++ - '0'); }
        for (;; f++) {
            if (*f == 'l') lng++; else if (*f == 'h') lng = -1; else if (*f == 'z' || *f == 'j' || *f == 't') lng = 2;
            else if (*f == 'I' && f[1] == '6' && f[2] == '4') { lng = 2; f += 2; }
            else if (*f == 'I' && f[1] == '3' && f[2] == '2') { lng = 0; f += 2; }
            else break;
        }
        char c = *f, tmp[80];
        if (c == '%') { put(o, '%'); continue; }
        if (c == 'c' || c == 'C') {
            int is_w = c == 'C' ? !wide : (lng == 1 ? 1 : lng == -1 ? 0 : wide);
            unsigned ch = (unsigned)va_arg(ap, int);
            if (!is_w) ch &= 0xff;
            char u[4]; int un = put_utf8(u, ch);
            if (!left) pad(o, width - 1, ' ');
            for (int i = 0; i < un; i++) put(o, u[i]);
            if (left) pad(o, width - 1, ' ');
            continue;
        }
        if (c == 's' || c == 'S') {
            int is_w = c == 'S' ? !wide : (lng == 1 ? 1 : lng == -1 ? 0 : wide);
            if (is_w) {
                const unsigned short *w = va_arg(ap, const unsigned short *);
                static const unsigned short nul[] = { '(', 'n', 'u', 'l', 'l', ')', 0 };
                if (!w) w = nul;
                int n = 0; while (w[n] && (prec < 0 || n < prec)) n++;
                if (!left) pad(o, width - n, ' ');
                for (int i = 0; i < n; i++) {
                    unsigned ch = w[i]; char u[4];
                    if (ch >= 0xD800 && ch < 0xDC00 && i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) { ch = 0x10000 + ((ch - 0xD800) << 10) + (w[i + 1] - 0xDC00); i++; }
                    int un = put_utf8(u, ch); for (int k = 0; k < un; k++) put(o, u[k]);
                }
                if (left) pad(o, width - n, ' ');
                continue;
            }
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int n = 0; while (s[n] && (prec < 0 || n < prec)) n++;
            if (!left) pad(o, width - n, ' ');
            for (int i = 0; i < n; i++) put(o, s[i]);
            if (left) pad(o, width - n, ' ');
            continue;
        }
        if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'o' || c == 'p') {
            int neg = 0; unsigned long long v;
            if (c == 'p') { v = (unsigned long long)va_arg(ap, void *); alt = 1; lng = 2; c = 'x'; }
            else if (c == 'd' || c == 'i') {
                long long sv = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
                if (lng == -1) sv = (short)sv;
                if (sv < 0) { neg = 1; v = (unsigned long long)-sv; } else v = (unsigned long long)sv;
            } else {
                v = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                if (lng == -1) v &= 0xffff;
            }
            int base = c == 'o' ? 8 : (c == 'x' || c == 'X') ? 16 : 10;
            int n = fmt_uint(tmp, v, base, c == 'X');
            int digits = n; if (prec >= 0 && prec > digits) digits = prec;
            const char *pre = neg ? "-" : plus && base == 10 ? "+" : space && base == 10 ? " " : (alt && base == 16 && v) ? (c == 'X' ? "0X" : "0x") : "";
            int plen = (int)slen(pre);
            int total = plen + digits;
            if (!left && !(zero && prec < 0)) pad(o, width - total, ' ');
            for (int i = 0; i < plen; i++) put(o, pre[i]);
            if (!left && zero && prec < 0) pad(o, width - total, '0');
            pad(o, digits - n, '0');
            while (n > 0) put(o, tmp[--n]);
            if (left) pad(o, width - total, ' ');
            continue;
        }
        if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G') {
            double d = va_arg(ap, double);
            fmt_double(o, d, c, width, prec, left, zero, plus, space, alt);
            continue;
        }
        put(o, '%'); put(o, c);
    }
    if (o->cap) o->buf[o->len < o->cap ? o->len : o->cap - 1] = 0;
    return (int)o->len;
}
EXPORT int vsnprintf(char *b, size_t n, const char *f, va_list ap) { out_t o = { b, n, 0 }; return vfmt(&o, f, ap, 0); }
EXPORT int _vsnprintf(char *b, size_t n, const char *f, va_list ap) { return vsnprintf(b, n, f, ap); }
EXPORT int vsprintf(char *b, const char *f, va_list ap) { return vsnprintf(b, (size_t)-1 >> 1, f, ap); }
EXPORT int snprintf(char *b, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
EXPORT int _snprintf(char *b, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
EXPORT int sprintf(char *b, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsprintf(b, f, ap); va_end(ap); return r; }
static int out_fd(void *fp, const char *f, va_list ap) {
    char stackbuf[1024];
    out_t o = { stackbuf, sizeof stackbuf, 0 };
    int n = vfmt(&o, f, ap, 0);
    if ((size_t)n < sizeof stackbuf) { fwrite(stackbuf, 1, n, fp); return n; }
    char *big = malloc((size_t)n + 1);
    if (!big) return -1;
    out_t o2 = { big, (size_t)n + 1, 0 };
    va_list ap2; (void)ap2;
    /* the first pass consumed ap; callers with long output re-run through a copy made by vfprintf */
    free(big);
    fwrite(stackbuf, 1, sizeof stackbuf - 1, fp);
    (void)o2;
    return n;
}
extern void *__iob_func(void);
#define STDOUT_FP ((char *)__iob_func() + 48)
EXPORT int vfprintf(void *fp, const char *f, va_list ap) { return out_fd(fp, f, ap); }
EXPORT int fprintf(void *fp, const char *f, ...) { va_list ap; va_start(ap, f); int r = out_fd(fp, f, ap); va_end(ap); return r; }
EXPORT int vprintf(const char *f, va_list ap) { return out_fd(STDOUT_FP, f, ap); }
EXPORT int printf(const char *f, ...) { va_list ap; va_start(ap, f); int r = out_fd(STDOUT_FP, f, ap); va_end(ap); return r; }
EXPORT int puts(const char *s) { fputs(s, STDOUT_FP); fputc('\n', STDOUT_FP); return 0; }
EXPORT int putchar(int c) { return fputc(c, STDOUT_FP); }
EXPORT int getchar(void) { return fgetc((char *)__iob_func()); }
EXPORT void perror(const char *s) { fputs(s, (char *)__iob_func() + 96); fputs(": error\n", (char *)__iob_func() + 96); }

/* ---- process ---- */
EXPORT int _getpid(void) { return (int)sc3(S_GETPID, 0, 0, 0); }
EXPORT int getpid(void) { return _getpid(); }


/* ======================================================================================================
   math (x87 for the transcendental functions: exact enough, and the same instructions a 2010 CPU has)
   ====================================================================================================== */
typedef long double ld;
static ld ld_log2(ld x) { ld r; __asm__("fld1; fxch %%st(1); fyl2x" : "=t"(r) : "0"(x)); return r; }
static ld ld_exp2(ld x) {
    ld i = x, f; __asm__("frndint" : "+t"(i));
    f = x - i; __asm__("f2xm1" : "+t"(f)); f += 1;
    __asm__("fscale" : "+t"(f) : "u"(i));
    return f;
}
EXPORT double sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }
EXPORT double fabs(double x) { union { double d; u64 u; } v = { x }; v.u &= 0x7fffffffffffffffULL; return v.d; }
EXPORT double sin(double x) { ld r = x; __asm__("fsin" : "+t"(r)); return (double)r; }
EXPORT double cos(double x) { ld r = x; __asm__("fcos" : "+t"(r)); return (double)r; }
EXPORT double tan(double x) { ld r = x; __asm__("fptan; fstp %%st(0)" : "+t"(r)); return (double)r; }
static ld ld_atan2(ld y, ld x) { ld r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
EXPORT double atan2(double y, double x) { return (double)ld_atan2(y, x); }
EXPORT double atan(double x) { return (double)ld_atan2(x, 1); }
EXPORT double asin(double x) { ld t = ((ld)1 - x) * ((ld)1 + x); ld r; __asm__("fsqrt" : "=t"(r) : "0"(t)); return (double)ld_atan2(x, r); }
EXPORT double acos(double x) { ld t = ((ld)1 - x) * ((ld)1 + x); ld r; __asm__("fsqrt" : "=t"(r) : "0"(t)); return (double)ld_atan2(r, x); }
EXPORT double log(double x) { return (double)(ld_log2(x) * 0.693147180559945309417232121458L); }
EXPORT double log10(double x) { return (double)(ld_log2(x) * 0.301029995663981195213738894724L); }
EXPORT double log2(double x) { return (double)ld_log2(x); }
EXPORT double exp(double x) { if (x > 709.8) return 1e308 * 10; if (x < -745.2) return 0; return (double)ld_exp2((ld)x * 1.442695040888963407359924681001L); }
EXPORT double exp2(double x) { return (double)ld_exp2(x); }
EXPORT double sinh(double x) { ld e = ld_exp2((ld)x * 1.442695040888963407359924681001L); return (double)((e - 1 / e) / 2); }
EXPORT double cosh(double x) { ld e = ld_exp2((ld)x * 1.442695040888963407359924681001L); return (double)((e + 1 / e) / 2); }
EXPORT double tanh(double x) { if (x > 20) return 1; if (x < -20) return -1; ld e = ld_exp2((ld)2 * x * 1.442695040888963407359924681001L); return (double)((e - 1) / (e + 1)); }
EXPORT double floor(double x) { if (!(x < 4503599627370496.0 && x > -4503599627370496.0)) return x; double r = (double)(i64)x; return r > x ? r - 1 : r; }
EXPORT double ceil(double x) { if (!(x < 4503599627370496.0 && x > -4503599627370496.0)) return x; double r = (double)(i64)x; return r < x ? r + 1 : r; }
EXPORT double trunc(double x) { if (!(x < 4503599627370496.0 && x > -4503599627370496.0)) return x; return (double)(i64)x; }
EXPORT double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }
EXPORT double fmod(double x, double y) { ld a = x, b = y; if (y == 0) return 0.0 / 0.0; __asm__("1: fprem; fnstsw %%ax; sahf; jp 1b" : "+t"(a) : "u"(b) : "ax", "cc"); return (double)a; }
EXPORT double modf(double x, double *ip) { double t = trunc(x); *ip = t; return x - t; }
EXPORT double ldexp(double x, int e) { ld r = x, n = e; __asm__("fscale" : "+t"(r) : "u"(n)); return (double)r; }
EXPORT double frexp(double x, int *e) {
    union { double d; u64 u; } v = { x }; int ex = (int)((v.u >> 52) & 0x7ff);
    if (x == 0 || ex == 0x7ff) { *e = 0; return x; }
    if (ex == 0) { v.d *= 18014398509481984.0; ex = (int)((v.u >> 52) & 0x7ff) - 54; }
    *e = ex - 1022; v.u = (v.u & ~(0x7ffULL << 52)) | (1022ULL << 52); return v.d;
}
EXPORT double pow(double x, double y) {
    if (y == 0) return 1;
    if (x == 1) return 1;
    if (x != x || y != y) return x + y;
    int yint = y == (double)(i64)y && y > -9e15 && y < 9e15;
    int odd = yint && ((i64)y & 1);
    if (x == 0) return y < 0 ? (odd ? 1 / x : 1e308 * 10) : (odd ? x : 0);
    double ax = x < 0 ? -x : x;
    if (x < 0 && !yint) return 0.0 / 0.0;
    ld t = (ld)y * ld_log2(ax);
    if (t > 1024) return x < 0 && odd ? -1e308 * 10 : 1e308 * 10;
    if (t < -1100) return 0;
    double r = (double)ld_exp2(t);
    return x < 0 && odd ? -r : r;
}
EXPORT double hypot(double x, double y) { return sqrt(x * x + y * y); }
EXPORT double cbrt(double x) { if (x == 0) return 0; double a = x < 0 ? -x : x; double r = pow(a, 1.0 / 3.0); r = r - (r * r * r - a) / (3 * r * r); return x < 0 ? -r : r; }
EXPORT double fmin(double a, double b) { return a < b || b != b ? a : b; }
EXPORT double fmax(double a, double b) { return a > b || b != b ? a : b; }
EXPORT double copysign(double x, double y) { union { double d; u64 u; } a = { x }, b = { y }; a.u = (a.u & 0x7fffffffffffffffULL) | (b.u & 0x8000000000000000ULL); return a.d; }
EXPORT double expm1(double x) { return exp(x) - 1; }
EXPORT double log1p(double x) { return log(1 + x); }
EXPORT int _isnan(double x) { return x != x; }
EXPORT int _finite(double x) { return x == x && x - x == 0; }
EXPORT double _copysign(double x, double y) { return copysign(x, y); }
EXPORT double _hypot(double x, double y) { return hypot(x, y); }
EXPORT unsigned _controlfp(unsigned n, unsigned m) { (void)n; (void)m; return 0x9001F; }
EXPORT void _fpreset(void) { __asm__ volatile("fninit"); }
EXPORT int _fpclass(double x) { return x != x ? 2 : (x == 0 ? 0x40 : 0x100); }
EXPORT int *_fpecode(void) { static int c; return &c; }

/* ======================================================================================================
   more of string.h / stdlib.h / ctype.h
   ====================================================================================================== */
EXPORT size_t strnlen(const char *s, size_t n) { size_t i = 0; while (i < n && s[i]) i++; return i; }
EXPORT char *_strlwr(char *s) { for (char *p = s; *p; p++) *p = (char)lower((unsigned char)*p); return s; }
EXPORT char *_strupr(char *s) { for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p); return s; }
EXPORT int _strcmpi(const char *a, const char *b) { return _stricmp(a, b); }
EXPORT int strcoll(const char *a, const char *b) { return strcmp(a, b); }
EXPORT unsigned long long _strtoui64(const char *s, char **end, int base) { return strtoull(s, end, base); }
EXPORT long long llabs(long long x) { return x < 0 ? -x : x; }
EXPORT long long _abs64(long long x) { return x < 0 ? -x : x; }
EXPORT long long atoll(const char *s) { return _strtoi64(s, 0, 10); }
typedef struct { int quot, rem; } div_t_;
EXPORT div_t_ div(int a, int b) { div_t_ r = { a / b, a % b }; return r; }
EXPORT int isblank(int c) { return c == ' ' || c == '\t'; }
EXPORT int isascii(int c) { return (unsigned)c < 128; }
EXPORT int __isascii(int c) { return (unsigned)c < 128; }
EXPORT int __toascii(int c) { return c & 127; }
EXPORT int _toupper(int c) { return toupper(c); }
EXPORT int _tolower(int c) { return lower(c); }
EXPORT char *_itoa(int v, char *buf, int base) {
    char t[34]; int n = 0, neg = v < 0 && base == 10; unsigned u = neg ? (unsigned)-v : (unsigned)v;
    do { int d = (int)(u % (unsigned)base); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (unsigned)base; } while (u);
    int k = 0; if (neg) buf[k++] = '-'; while (n) buf[k++] = t[--n]; buf[k] = 0; return buf;
}
EXPORT char *_ltoa(long v, char *buf, int base) { return _itoa((int)v, buf, base); }
EXPORT char *_ultoa(unsigned long v, char *buf, int base) {
    char t[34]; int n = 0; do { int d = (int)(v % (unsigned long)base); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= (unsigned long)base; } while (v);
    int k = 0; while (n) buf[k++] = t[--n]; buf[k] = 0; return buf;
}
EXPORT char *_i64toa(long long v, char *buf, int base) {
    char t[66]; int n = 0, neg = v < 0 && base == 10; unsigned long long u = neg ? (unsigned long long)-v : (unsigned long long)v;
    do { int d = (int)(u % (unsigned)base); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (unsigned)base; } while (u);
    int k = 0; if (neg) buf[k++] = '-'; while (n) buf[k++] = t[--n]; buf[k] = 0; return buf;
}
EXPORT char *_ui64toa(unsigned long long u, char *buf, int base) {
    char t[66]; int n = 0; do { int d = (int)(u % (unsigned)base); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); u /= (unsigned)base; } while (u);
    int k = 0; while (n) buf[k++] = t[--n]; buf[k] = 0; return buf;
}
EXPORT int _vscprintf(const char *f, va_list ap) { out_t o = { 0, 0, 0 }; return vfmt(&o, f, ap, 0); }
EXPORT int _scprintf(const char *f, ...) { va_list ap; va_start(ap, f); int r = _vscprintf(f, ap); va_end(ap); return r; }

/* environment: a small private table (a Windows process starts with the usual few variables) */
static char *env_tab[64][2];
static const char *env_default(const char *n) {
    static const char *kv[][2] = { { "PATH", "C:\\Windows;C:\\Windows\\System32" }, { "TEMP", "C:\\tmp" }, { "TMP", "C:\\tmp" }, { "OS", "THOS" },
        { "COMPUTERNAME", "THOS" }, { "SystemRoot", "C:\\Windows" }, { "USERPROFILE", "C:\\Users\\thos" }, { "HOMEDRIVE", "C:" }, { "HOMEPATH", "\\Users\\thos" } };
    for (unsigned k = 0; k < sizeof kv / sizeof kv[0]; k++) if (_stricmp(kv[k][0], n) == 0) return kv[k][1];
    return 0;
}
EXPORT char *getenv(const char *n) {
    for (int i = 0; i < 64; i++) if (env_tab[i][0] && _stricmp(env_tab[i][0], n) == 0) return env_tab[i][1] && env_tab[i][1][0] ? env_tab[i][1] : 0;
    return (char *)env_default(n);
}
EXPORT int _putenv(const char *kv) {
    const char *eq = strchr(kv, '='); if (!eq) return -1;
    size_t kl = (size_t)(eq - kv); char *k = malloc(kl + 1), *v = _strdup(eq + 1);
    if (!k || !v) return -1;
    memmove(k, kv, kl); k[kl] = 0;
    int slot = -1;
    for (int i = 0; i < 64; i++) { if (env_tab[i][0] && _stricmp(env_tab[i][0], k) == 0) { slot = i; break; } if (!env_tab[i][0] && slot < 0) slot = i; }
    if (slot < 0) return -1;
    env_tab[slot][0] = k; env_tab[slot][1] = v;
    return 0;
}

/* ======================================================================================================
   time
   ====================================================================================================== */
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
static void civil_from_days(i64 z, int *y, int *m, int *d) {
    z += 719468; i64 era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; i64 yy = (i64)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100); unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1); *m = (int)(mp < 10 ? mp + 3 : mp - 9); *y = (int)(yy + (*m <= 2));
}
static i64 days_from_civ(i64 y, unsigned m, unsigned d) {
    y -= m <= 2; i64 era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (i64)doe - 719468;
}
static struct tm *fill_tm(struct tm *r, i64 t) {
    i64 days = t / 86400, rem = t % 86400; if (rem < 0) { rem += 86400; days--; }
    int y, m, d; civil_from_days(days, &y, &m, &d);
    r->tm_sec = (int)(rem % 60); r->tm_min = (int)(rem / 60 % 60); r->tm_hour = (int)(rem / 3600);
    r->tm_mday = d; r->tm_mon = m - 1; r->tm_year = y - 1900; r->tm_wday = (int)(((days + 4) % 7 + 7) % 7);
    r->tm_yday = (int)(days - days_from_civ(y, 1, 1)); r->tm_isdst = 0;
    return r;
}
static struct tm g_tm;
EXPORT struct tm *_gmtime64(const i64 *t) { return fill_tm(&g_tm, *t); }
EXPORT struct tm *_localtime64(const i64 *t) { return fill_tm(&g_tm, *t); }
EXPORT struct tm *gmtime(const i64 *t) { return fill_tm(&g_tm, *t); }
EXPORT struct tm *localtime(const i64 *t) { return fill_tm(&g_tm, *t); }
EXPORT i64 _mktime64(struct tm *t) {
    i64 y = t->tm_year + 1900 + t->tm_mon / 12; int mo = t->tm_mon % 12; if (mo < 0) { mo += 12; y--; }
    i64 days = days_from_civ(y, (unsigned)mo + 1, 1) + (t->tm_mday - 1);
    i64 secs = days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
    fill_tm(t, secs);
    return secs;
}
EXPORT i64 mktime(struct tm *t) { return _mktime64(t); }
EXPORT i64 _mkgmtime64(struct tm *t) { return _mktime64(t); }
EXPORT double difftime(i64 a, i64 b) { return (double)(a - b); }
static const char *wd_s[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *mo_s[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static size_t put_num(char *b, size_t cap, size_t n, int v, int digits, char padc) {
    char t[12]; int k = 0; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k < digits) t[k++] = padc;
    while (k && n + 1 < cap) b[n++] = t[--k];
    return n;
}
EXPORT size_t strftime(char *b, size_t cap, const char *f, const struct tm *t) {
    size_t n = 0;
    for (; *f && n + 1 < cap; f++) {
        if (*f != '%') { b[n++] = *f; continue; }
        f++;
        const char *str = 0;
        switch (*f) {
            case 'Y': n = put_num(b, cap, n, t->tm_year + 1900, 4, '0'); break;
            case 'y': n = put_num(b, cap, n, (t->tm_year + 1900) % 100, 2, '0'); break;
            case 'm': n = put_num(b, cap, n, t->tm_mon + 1, 2, '0'); break;
            case 'd': n = put_num(b, cap, n, t->tm_mday, 2, '0'); break;
            case 'e': n = put_num(b, cap, n, t->tm_mday, 2, ' '); break;
            case 'H': n = put_num(b, cap, n, t->tm_hour, 2, '0'); break;
            case 'I': n = put_num(b, cap, n, t->tm_hour % 12 ? t->tm_hour % 12 : 12, 2, '0'); break;
            case 'M': n = put_num(b, cap, n, t->tm_min, 2, '0'); break;
            case 'S': n = put_num(b, cap, n, t->tm_sec, 2, '0'); break;
            case 'j': n = put_num(b, cap, n, t->tm_yday + 1, 3, '0'); break;
            case 'p': str = t->tm_hour < 12 ? "AM" : "PM"; break;
            case 'a': { const char *s = wd_s[t->tm_wday % 7]; for (int i = 0; i < 3 && n + 1 < cap; i++) b[n++] = s[i]; break; }
            case 'A': str = wd_s[t->tm_wday % 7]; break;
            case 'b': case 'h': { const char *s = mo_s[t->tm_mon % 12]; for (int i = 0; i < 3 && n + 1 < cap; i++) b[n++] = s[i]; break; }
            case 'B': str = mo_s[t->tm_mon % 12]; break;
            case 'Z': str = "UTC"; break;
            case 'z': str = "+0000"; break;
            case 'F': n += strftime(b + n, cap - n, "%Y-%m-%d", t); break;
            case 'T': case 'X': n += strftime(b + n, cap - n, "%H:%M:%S", t); break;
            case 'D': case 'x': n += strftime(b + n, cap - n, "%m/%d/%y", t); break;
            case 'R': n += strftime(b + n, cap - n, "%H:%M", t); break;
            case 'c': n += strftime(b + n, cap - n, "%a %b %e %H:%M:%S %Y", t); break;
            case 'n': b[n++] = '\n'; break;
            case 't': b[n++] = '\t'; break;
            case '%': b[n++] = '%'; break;
            default: b[n++] = '%'; if (n + 1 < cap) b[n++] = *f; break;
        }
        if (str) for (; *str && n + 1 < cap; str++) b[n++] = *str;
    }
    if (n >= cap) return 0;
    b[n] = 0;
    return n;
}
EXPORT char *asctime(const struct tm *t) { static char buf[32]; strftime(buf, sizeof buf, "%a %b %e %H:%M:%S %Y\n", t); return buf; }
EXPORT char *_ctime64(const i64 *t) { return asctime(_gmtime64(t)); }
EXPORT char *ctime(const i64 *t) { return asctime(_gmtime64(t)); }
EXPORT void _tzset(void) {}
EXPORT void tzset(void) {}

/* ======================================================================================================
   low-level file I/O (descriptors), stat, directories
   ====================================================================================================== */
enum { S_FSTAT = 5, S_DUP = 32, S_DUP2 = 33, S_PIPE = 22, S_GETCWD = 79, S_CHDIR = 80, S_RMDIR = 84, S_NEWFSTATAT = 262, S_FTRUNCATE = 77, S_ACCESS = 21, S_IOCTL = 16, S_FSYNC = 74 };
EXPORT int _open(const char *path, int oflag, ...) {
    char q[512]; dos_path(q, path, sizeof q);
    int fl = (oflag & 3);
    if (oflag & 0x8) fl |= 02000;                    /* _O_APPEND */
    if (oflag & 0x100) fl |= 0100;                   /* _O_CREAT  */
    if (oflag & 0x200) fl |= 01000;                  /* _O_TRUNC  */
    if (oflag & 0x400) fl |= 0200;                   /* _O_EXCL   */
    return (int)sys_ret(sc3(S_OPEN, q, fl, 0644));
}
EXPORT int _sopen(const char *path, int oflag, int shflag, ...) { (void)shflag; return _open(path, oflag); }
EXPORT int open(const char *path, int oflag, ...) { return _open(path, oflag); }
EXPORT int _close(int fd) { return (int)sys_ret(sc3(S_CLOSE, fd, 0, 0)); }
EXPORT int close(int fd) { return _close(fd); }
EXPORT int _read(int fd, void *buf, unsigned n) { return (int)sys_ret(sc3(S_READ, fd, buf, n)); }
EXPORT int read(int fd, void *buf, unsigned n) { return _read(fd, buf, n); }
EXPORT int _write(int fd, const void *buf, unsigned n) { return (int)sys_ret(sc3(S_WRITE, fd, buf, n)); }
EXPORT int write(int fd, const void *buf, unsigned n) { return _write(fd, buf, n); }
EXPORT long _lseek(int fd, long off, int whence) { return (long)sys_ret(sc3(S_LSEEK, fd, off, whence)); }
EXPORT long long _lseeki64(int fd, long long off, int whence) { return sys_ret(sc3(S_LSEEK, fd, off, whence)); }
EXPORT long lseek(int fd, long off, int whence) { return _lseek(fd, off, whence); }
EXPORT int _dup(int fd) { return (int)sys_ret(sc3(S_DUP, fd, 0, 0)); }
EXPORT int _dup2(int a, int b) { return (int)sys_ret(sc3(S_DUP2, a, b, 0)); }
EXPORT int _commit(int fd) { sc3(S_FSYNC, fd, 0, 0); return 0; }
EXPORT int _setmode(int fd, int mode) { (void)fd; (void)mode; return 0x8000; }
EXPORT int _isatty(int fd) { return fd >= 0 && fd <= 2; }
EXPORT int _fileno(void *fp) { return fdof(fp); }
EXPORT int fileno(void *fp) { return fdof(fp); }
EXPORT int _pipe(int *fds, unsigned size, int mode) { (void)size; (void)mode; return (int)sys_ret(sc3(S_PIPE, fds, 0, 0)); }
EXPORT int _chsize(int fd, long size) { return (int)sys_ret(sc3(S_FTRUNCATE, fd, size, 0)); }
EXPORT int _access(const char *path, int mode) { char q[512]; dos_path(q, path, sizeof q); return (int)sys_ret(sc3(S_ACCESS, q, mode & 6 ? mode : 0, 0)); }
EXPORT int access(const char *path, int mode) { return _access(path, mode); }
EXPORT char *_getcwd(char *buf, int size) {
    char p[512]; i64 n = sc3(S_GETCWD, p, sizeof p, 0);
    if (n <= 0) return 0;
    size_t l = slen(p); if ((size_t)size < l + 3) { *errno_ptr() = 34; return 0; }
    if (!buf) { buf = malloc(l + 3); if (!buf) return 0; }
    buf[0] = 'C'; buf[1] = ':';
    for (size_t i = 0; i <= l; i++) buf[2 + i] = p[i] == '/' ? '\\' : p[i];
    if (l == 1 && p[0] == '/') { buf[2] = '\\'; buf[3] = 0; }
    return buf;
}
EXPORT char *getcwd(char *buf, int size) { return _getcwd(buf, size); }
EXPORT int _chdir(const char *p) { char q[512]; dos_path(q, p, sizeof q); return (int)sys_ret(sc3(S_CHDIR, q, 0, 0)); }
EXPORT int chdir(const char *p) { return _chdir(p); }
EXPORT int _rmdir(const char *p) { char q[512]; dos_path(q, p, sizeof q); return (int)sys_ret(sc3(S_RMDIR, q, 0, 0)); }
EXPORT int rmdir(const char *p) { return _rmdir(p); }
EXPORT int _unlink(const char *p) { return remove(p); }
EXPORT int unlink(const char *p) { return remove(p); }
EXPORT int mkdir(const char *p) { return _mkdir(p); }
typedef struct { u32 st_dev; unsigned short st_ino, st_mode; short st_nlink, st_uid, st_gid; u32 st_rdev; i64 st_size, st_atime, st_mtime, st_ctime; } STAT64;
static void to_stat64(const unsigned char *st, STAT64 *o) {
    o->st_dev = 2; o->st_ino = 0; o->st_mode = (unsigned short)(*(u32 *)(st + 24) & 0xF1FF); o->st_nlink = 1; o->st_uid = 0; o->st_gid = 0; o->st_rdev = 0;
    o->st_size = *(i64 *)(st + 48); o->st_atime = *(i64 *)(st + 72); o->st_mtime = *(i64 *)(st + 88); o->st_ctime = *(i64 *)(st + 104);
}
EXPORT int _stat64(const char *path, STAT64 *o) {
    char q[512]; dos_path(q, path, sizeof q); unsigned char st[144];
    if (sys_ret(sc6(S_NEWFSTATAT, -100, (i64)q, (i64)st, 0, 0, 0)) < 0) return -1;
    to_stat64(st, o); return 0;
}
EXPORT int _stat(const char *path, STAT64 *o) { return _stat64(path, o); }
EXPORT int stat(const char *path, STAT64 *o) { return _stat64(path, o); }
EXPORT int _fstat64(int fd, STAT64 *o) { unsigned char st[144]; if (sys_ret(sc3(S_FSTAT, fd, st, 0)) < 0) return -1; to_stat64(st, o); return 0; }
EXPORT int _fstat(int fd, STAT64 *o) { return _fstat64(fd, o); }
EXPORT long long _filelengthi64(int fd) { STAT64 s; return _fstat64(fd, &s) < 0 ? -1 : s.st_size; }
EXPORT long _filelength(int fd) { return (long)_filelengthi64(fd); }
EXPORT int _fseeki64(void *fp, long long off, int whence) { return fseek(fp, (long)off, whence); }
EXPORT long long _ftelli64(void *fp) { return ftell(fp); }
EXPORT int fseeko64(void *fp, long long off, int whence) { return fseek(fp, (long)off, whence); }
EXPORT long long ftello64(void *fp) { return ftell(fp); }
EXPORT int setvbuf(void *fp, char *buf, int mode, size_t size) { (void)fp; (void)buf; (void)mode; (void)size; return 0; }
EXPORT void setbuf(void *fp, char *buf) { (void)fp; (void)buf; }
EXPORT int _flushall(void) { return 0; }
EXPORT int fgetpos(void *fp, long long *pos) { *pos = ftell(fp); return 0; }
EXPORT int fsetpos(void *fp, const long long *pos) { return fseek(fp, (long)*pos, 0); }
EXPORT int puts_dummy_unused(void) { return 0; }

/* ======================================================================================================
   wide characters
   ====================================================================================================== */
typedef unsigned short wc_t;
static size_t wslen(const wc_t *s) { size_t n = 0; while (s[n]) n++; return n; }
EXPORT wc_t *wcscpy(wc_t *d, const wc_t *s) { wc_t *r = d; while ((*d++ = *s++)); return r; }
EXPORT wc_t *wcsncpy(wc_t *d, const wc_t *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
EXPORT wc_t *wcscat(wc_t *d, const wc_t *s) { wcscpy(d + wslen(d), s); return d; }
EXPORT wc_t *wcsncat(wc_t *d, const wc_t *s, size_t n) { wc_t *e = d + wslen(d); size_t i = 0; for (; i < n && s[i]; i++) e[i] = s[i]; e[i] = 0; return d; }
EXPORT int wcscmp(const wc_t *a, const wc_t *b) { while (*a && *a == *b) { a++; b++; } return (int)*a - (int)*b; }
EXPORT int wcsncmp(const wc_t *a, const wc_t *b, size_t n) { for (size_t i = 0; i < n; i++) { if (a[i] != b[i]) return (int)a[i] - (int)b[i]; if (!a[i]) return 0; } return 0; }
EXPORT int towlower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : (c >= 0xC0 && c <= 0xDE && c != 0xD7 ? c + 32 : c); }
EXPORT int towupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : (c >= 0xE0 && c <= 0xFE && c != 0xF7 ? c - 32 : c); }
EXPORT int _wcsicmp(const wc_t *a, const wc_t *b) { while (*a && towlower(*a) == towlower(*b)) { a++; b++; } return towlower(*a) - towlower(*b); }
EXPORT int _wcsnicmp(const wc_t *a, const wc_t *b, size_t n) { for (size_t i = 0; i < n; i++) { int x = towlower(a[i]), y = towlower(b[i]); if (x != y) return x - y; if (!a[i]) return 0; } return 0; }
EXPORT wc_t *wcschr(const wc_t *s, wc_t c) { for (; *s; s++) if (*s == c) return (wc_t *)s; return c ? 0 : (wc_t *)s; }
EXPORT wc_t *wcsrchr(const wc_t *s, wc_t c) { const wc_t *r = 0; for (; *s; s++) if (*s == c) r = s; return c ? (wc_t *)r : (wc_t *)s; }
EXPORT wc_t *wcsstr(const wc_t *h, const wc_t *n) { size_t l = wslen(n); if (!l) return (wc_t *)h; for (; *h; h++) if (wcsncmp(h, n, l) == 0) return (wc_t *)h; return 0; }
EXPORT size_t wcsnlen(const wc_t *s, size_t n) { size_t i = 0; while (i < n && s[i]) i++; return i; }
EXPORT size_t wcsspn(const wc_t *s, const wc_t *set) { size_t n = 0; while (s[n] && wcschr(set, s[n])) n++; return n; }
EXPORT size_t wcscspn(const wc_t *s, const wc_t *set) { size_t n = 0; while (s[n] && !wcschr(set, s[n])) n++; return n; }
EXPORT wc_t *wcspbrk(const wc_t *s, const wc_t *set) { s += wcscspn(s, set); return *s ? (wc_t *)s : 0; }
EXPORT wc_t *_wcsdup(const wc_t *s) { size_t n = wslen(s) + 1; wc_t *d = malloc(n * 2); if (d) memmove(d, s, n * 2); return d; }
EXPORT wc_t *_wcslwr(wc_t *s) { for (wc_t *p = s; *p; p++) *p = (wc_t)towlower(*p); return s; }
EXPORT wc_t *_wcsupr(wc_t *s) { for (wc_t *p = s; *p; p++) *p = (wc_t)towupper(*p); return s; }
EXPORT wc_t *wcstok(wc_t *s, const wc_t *delim) {
    static wc_t *save;
    if (!s) s = save; if (!s) return 0;
    s += wcsspn(s, delim); if (!*s) { save = 0; return 0; }
    wc_t *e = s + wcscspn(s, delim);
    if (*e) { *e = 0; save = e + 1; } else save = 0;
    return s;
}
EXPORT int iswalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0xC0; }
EXPORT int iswdigit(int c) { return c >= '0' && c <= '9'; }
EXPORT int iswalnum(int c) { return iswalpha(c) || iswdigit(c); }
EXPORT int iswspace(int c) { return c == ' ' || (c >= 9 && c <= 13) || c == 0xA0; }
EXPORT int iswupper(int c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE); }
EXPORT int iswlower(int c) { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF); }
EXPORT int iswxdigit(int c) { return iswdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
EXPORT int iswprint(int c) { return c >= 32 && c != 127; }
EXPORT int iswpunct(int c) { return c > 32 && c < 127 && !iswalnum(c); }
EXPORT int iswcntrl(int c) { return c < 32 || c == 127; }
EXPORT int iswctype(int c, unsigned type) { return (type & 1 && iswupper(c)) || (type & 2 && iswlower(c)) || (type & 4 && iswdigit(c)) || (type & 8 && iswspace(c)) || (type & 0x100 && iswalpha(c)); }
static unsigned long long wconv(const wc_t *s, wc_t **end, int base, int *neg) {
    char tmp[80]; size_t i = 0; while (s[i] && i < 79 && s[i] < 128) { tmp[i] = (char)s[i]; i++; } tmp[i] = 0;
    char *e; unsigned long long v = conv(tmp, &e, base, neg); if (end) *end = (wc_t *)s + (e - tmp); return v;
}
EXPORT long wcstol(const wc_t *s, wc_t **end, int base) { int neg; unsigned long long v = wconv(s, end, base, &neg); return neg ? -(long)v : (long)v; }
EXPORT unsigned long wcstoul(const wc_t *s, wc_t **end, int base) { int neg; unsigned long long v = wconv(s, end, base, &neg); return neg ? -(unsigned long)v : (unsigned long)v; }
EXPORT long long _wcstoi64(const wc_t *s, wc_t **end, int base) { int neg; unsigned long long v = wconv(s, end, base, &neg); return neg ? -(long long)v : (long long)v; }
EXPORT double wcstod(const wc_t *s, wc_t **end) {
    char tmp[80]; size_t i = 0; while (s[i] && i < 79 && s[i] < 128) { tmp[i] = (char)s[i]; i++; } tmp[i] = 0;
    char *e; double v = strtod(tmp, &e); if (end) *end = (wc_t *)s + (e - tmp); return v;
}
EXPORT int _wtoi(const wc_t *s) { return (int)wcstol(s, 0, 10); }
EXPORT long _wtol(const wc_t *s) { return wcstol(s, 0, 10); }
EXPORT long long _wtoi64(const wc_t *s) { return _wcstoi64(s, 0, 10); }
EXPORT double _wtof(const wc_t *s) { return wcstod(s, 0); }

/* UTF-16 <-> UTF-8 helpers for the wide stdio and printf */
static size_t utf16_to_utf8(const wc_t *w, char *out, size_t cap) {
    size_t o = 0;
    for (; *w && o + 4 < cap; w++) {
        unsigned c = *w;
        if (c >= 0xD800 && c < 0xDC00 && w[1] >= 0xDC00 && w[1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (w[1] - 0xDC00); w++; }
        o += (size_t)put_utf8(out + o, c);
    }
    out[o] = 0; return o;
}
static size_t utf8_to_utf16(const char *s, size_t n, wc_t *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 2 < cap;) {
        unsigned c = (unsigned char)s[i++];
        if (c >= 0xF0 && i + 2 < n + 0) { c = ((c & 7) << 18) | ((s[i] & 63) << 12) | ((s[i + 1] & 63) << 6) | (s[i + 2] & 63); i += 3; }
        else if (c >= 0xE0 && i + 1 < n) { c = ((c & 15) << 12) | ((s[i] & 63) << 6) | (s[i + 1] & 63); i += 2; }
        else if (c >= 0xC0 && i < n) { c = ((c & 31) << 6) | (s[i] & 63); i += 1; }
        if (c >= 0x10000) { c -= 0x10000; out[o++] = (wc_t)(0xD800 + (c >> 10)); out[o++] = (wc_t)(0xDC00 + (c & 1023)); }
        else out[o++] = (wc_t)c;
    }
    out[o] = 0; return o;
}
EXPORT unsigned fputwc(unsigned c, void *fp) { char u[4]; int n = put_utf8(u, c); return fwrite(u, 1, (size_t)n, fp) == (size_t)n ? c : 0xFFFF; }
EXPORT unsigned putwc(unsigned c, void *fp) { return fputwc(c, fp); }
EXPORT unsigned putwchar(unsigned c) { return fputwc(c, STDOUT_FP); }
EXPORT int fputws(const wc_t *w, void *fp) { char tmp[1024]; size_t n = utf16_to_utf8(w, tmp, sizeof tmp); return fwrite(tmp, 1, n, fp) == n ? 0 : -1; }
static int wvformat(char **outbuf, const wc_t *wfmt, va_list ap) {       /* returns the UTF-8 length; *outbuf is malloc'ed */
    char fmt[1024]; utf16_to_utf8(wfmt, fmt, sizeof fmt);
    va_list ap2; __builtin_va_copy(ap2, ap);
    out_t cnt = { 0, 0, 0 }; int n = vfmt(&cnt, fmt, ap, 1);
    char *b = malloc((size_t)n + 1); if (!b) return -1;
    out_t o = { b, (size_t)n + 1, 0 }; vfmt(&o, fmt, ap2, 1);
    *outbuf = b; return n;
}
EXPORT int vfwprintf(void *fp, const wc_t *f, va_list ap) { char *b; int n = wvformat(&b, f, ap); if (n < 0) return -1; fwrite(b, 1, (size_t)n, fp); free(b); return n; }
EXPORT int fwprintf(void *fp, const wc_t *f, ...) { va_list ap; va_start(ap, f); int r = vfwprintf(fp, f, ap); va_end(ap); return r; }
EXPORT int vwprintf(const wc_t *f, va_list ap) { return vfwprintf(STDOUT_FP, f, ap); }
EXPORT int wprintf(const wc_t *f, ...) { va_list ap; va_start(ap, f); int r = vfwprintf(STDOUT_FP, f, ap); va_end(ap); return r; }
EXPORT int _vsnwprintf(wc_t *d, size_t cap, const wc_t *f, va_list ap) {
    char *b; int n = wvformat(&b, f, ap); if (n < 0) return -1;
    wc_t *tmp = malloc(((size_t)n + 1) * 2 + 2); if (!tmp) { free(b); return -1; }
    size_t wn = utf8_to_utf16(b, (size_t)n, tmp, (size_t)n + 2);
    free(b);
    int r = (int)wn;
    if (cap) { size_t k = wn < cap ? wn : cap - 1; for (size_t i = 0; i < k; i++) d[i] = tmp[i]; if (wn < cap) d[k] = 0; else if (cap) { d[cap - 1] = (wc_t)tmp[cap - 1]; r = -1; } }
    free(tmp);
    return r;
}
EXPORT int vswprintf(wc_t *d, size_t cap, const wc_t *f, va_list ap) { return _vsnwprintf(d, cap, f, ap); }
EXPORT int _snwprintf(wc_t *d, size_t cap, const wc_t *f, ...) { va_list ap; va_start(ap, f); int r = _vsnwprintf(d, cap, f, ap); va_end(ap); return r; }
EXPORT int swprintf(wc_t *d, const wc_t *f, ...) { va_list ap; va_start(ap, f); int r = _vsnwprintf(d, 0x7fffffff, f, ap); va_end(ap); return r; }
EXPORT int _vscwprintf(const wc_t *f, va_list ap) { char *b; int n = wvformat(&b, f, ap); if (n < 0) return -1; free(b); return n; }
EXPORT void *_wfopen(const wc_t *path, const wc_t *mode) { char p[1024], m[16]; utf16_to_utf8(path, p, sizeof p); utf16_to_utf8(mode, m, sizeof m); return fopen(p, m); }
EXPORT int _wremove(const wc_t *p) { char a[1024]; utf16_to_utf8(p, a, sizeof a); return remove(a); }
EXPORT int _wunlink(const wc_t *p) { return _wremove(p); }
EXPORT int _wmkdir(const wc_t *p) { char a[1024]; utf16_to_utf8(p, a, sizeof a); return _mkdir(a); }
EXPORT int _wrmdir(const wc_t *p) { char a[1024]; utf16_to_utf8(p, a, sizeof a); return _rmdir(a); }
EXPORT int _wchdir(const wc_t *p) { char a[1024]; utf16_to_utf8(p, a, sizeof a); return _chdir(a); }
EXPORT int _waccess(const wc_t *p, int m) { char a[1024]; utf16_to_utf8(p, a, sizeof a); return _access(a, m); }
EXPORT int _wrename(const wc_t *x, const wc_t *y) { char a[1024], b[1024]; utf16_to_utf8(x, a, sizeof a); utf16_to_utf8(y, b, sizeof b); return rename(a, b); }
EXPORT int _wopen(const wc_t *p, int oflag, ...) { char a[1024]; utf16_to_utf8(p, a, sizeof a); return _open(a, oflag); }
EXPORT int _wstat64(const wc_t *p, STAT64 *o) { char a[1024]; utf16_to_utf8(p, a, sizeof a); return _stat64(a, o); }
EXPORT wc_t *_wgetenv(const wc_t *n) {
    char a[256]; utf16_to_utf8(n, a, sizeof a); char *v = getenv(a); if (!v) return 0;
    static wc_t buf[512]; utf8_to_utf16(v, slen(v), buf, 512); return buf;
}
EXPORT int _wputenv(const wc_t *kv) { char a[1024]; utf16_to_utf8(kv, a, sizeof a); return _putenv(a); }
EXPORT wc_t *_wgetcwd(wc_t *buf, int size) { char a[600]; if (!_getcwd(a, sizeof a)) return 0; static wc_t tmp[600]; size_t n = utf8_to_utf16(a, slen(a), tmp, 600); if ((size_t)size < n + 1) return 0; if (!buf) buf = malloc((n + 1) * 2); for (size_t i = 0; i <= n; i++) buf[i] = tmp[i]; return buf; }
EXPORT size_t mbstowcs(wc_t *d, const char *s, size_t n) { size_t l = slen(s); if (!d) { wc_t t[2048]; return utf8_to_utf16(s, l < 2000 ? l : 2000, t, 2048); } size_t r = utf8_to_utf16(s, l, d, n); return r; }
EXPORT size_t wcstombs(char *d, const wc_t *w, size_t n) { char t[4096]; size_t l = utf16_to_utf8(w, t, sizeof t); if (!d) return l; size_t k = l < n ? l : n; for (size_t i = 0; i < k; i++) d[i] = t[i]; if (k < n) d[k] = 0; return k; }
EXPORT int mbtowc(wc_t *w, const char *s, size_t n) { if (!s) return 0; if (!n || !*s) return 0; wc_t t[4]; size_t l = 1; unsigned char c = (unsigned char)*s; if (c >= 0xF0) l = 4; else if (c >= 0xE0) l = 3; else if (c >= 0xC0) l = 2; if (l > n) return -1; utf8_to_utf16(s, l, t, 4); if (w) *w = t[0]; return (int)l; }
EXPORT int wctomb(char *s, wc_t w) { if (!s) return 0; return put_utf8(s, w); }
EXPORT size_t mbstowcs_dummy(void) { return 0; }

/* Wide CRT startup reads the real PEB command line and initial environment.
   Current THOS launches still provide an empty PEB environment; this does not
   manufacture inherited Linux variables. */
#include "winargv16.h"
EXPORT wc_t **__winitenv;
EXPORT wc_t *_wcmdln;
static int wide_state;
static int wide_argc;
static wc_t **wide_argv;
static wc_t **wide_initial_env;
static u64 wide_parameters(void){u64 peb;__asm__ volatile("movq %%gs:0x60,%0":"=r"(peb));return peb?*(u64*)(peb+0x20):0;}
static wc_t *wide_command(void){static wc_t empty[1];u64 p=wide_parameters();wc_t*cmd=p?*(wc_t**)(p+0x78):0;return cmd?cmd:empty;}
static int wide_fail(int error){*errno_ptr()=error;return error;}
static int wide_build(void){
    const wc_t *cmd=wide_command();size_t len=0;while(len<32768&&cmd[len])len++;if(len==32768)return 7;
    wc_t *storage=malloc((len+1)*2);wc_t **args=malloc((len+2)*sizeof *args);
    if(!storage||!args){free(storage);free(args);return 12;}
    int count=thos_wargv16(cmd,len,storage,len+1,args,len+2);if(count<0){free(storage);free(args);return 7;}
    u64 params=wide_parameters();const wc_t *block=params?*(wc_t**)(params+0x80):0;
    size_t units=2,entries=0;static const wc_t empty[2]={0,0};if(!block)block=empty;
    for(units=0;units+1<32768;units++){if(!block[units]&&!block[units+1])break;}
    if(units+1==32768){free(storage);free(args);return 7;}units+=2;
    for(size_t i=0;i<units&&block[i];){entries++;while(i<units&&block[i])i++;i++;}
    wc_t *copy=malloc(units*2);wc_t **env=malloc((entries+1)*sizeof *env);
    if(!copy||!env){free(copy);free(env);free(storage);free(args);return 12;}
    memmove(copy,block,units*2);size_t n=0;for(size_t i=0;i<units&&copy[i];){env[n++]=copy+i;while(i<units&&copy[i])i++;i++;}env[n]=0;
    wide_argc=count;wide_argv=args;wide_initial_env=env;__winitenv=env;_wcmdln=(wc_t*)cmd;return 0;
}
static int wide_initialize(void){
    for(;;){int state=__atomic_load_n(&wide_state,__ATOMIC_ACQUIRE);if(state==2)return 0;
        int expected=0;if(__atomic_compare_exchange_n(&wide_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
            int error=wide_build();__atomic_store_n(&wide_state,error?0:2,__ATOMIC_RELEASE);
            sc6(202,(i64)&wide_state,129,2147483647,0,0,0);return error;
        }
        sc6(202,(i64)&wide_state,128,1,0,0,0);
    }
}
EXPORT int __wgetmainargs(int *argc,wc_t ***argv,wc_t ***env,int wildcard,const int *startup){
    if(!argc||!argv||!env)return wide_fail(22);
    if(wildcard||(startup&&*startup))return wide_fail(ENOSYS);
    int error=wide_initialize();if(error)return wide_fail(error);
    *argc=wide_argc;*argv=wide_argv;*env=wide_initial_env;return 0;
}
__attribute__((dllexport)) int __stdcall DllMain(void *h,unsigned reason,void *res){(void)h;(void)res;if(reason==1)_wcmdln=wide_command();return 1;}
