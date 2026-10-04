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

#define EXPORT __attribute__((dllexport))

typedef long long i64;
typedef unsigned long long u64;

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

/* errno: our own cell (the built-in _errno cannot be called from here without a cycle) */
static int g_errno;
static int *errno_ptr(void) { return &g_errno; }

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
EXPORT char *getenv(const char *n) { (void)n; return 0; }

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
static int vfmt(out_t *o, const char *f, va_list ap) {
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
        if (c == 'c') { char ch = (char)va_arg(ap, int); if (!left) pad(o, width - 1, ' '); put(o, ch); if (left) pad(o, width - 1, ' '); continue; }
        if (c == 's') {
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
            int neg = 0; if (d < 0) { neg = 1; d = -d; }
            if (d != d) { const char *s = "nan"; pad(o, width - 3, ' '); while (*s) put(o, *s++); continue; }
            if (prec < 0) prec = 6;
            if (c == 'g' || c == 'G' || c == 'e' || c == 'E') {
                /* simplified: print %g/%e as %f with enough digits and trimmed zeros for %g */
                if (c == 'g' || c == 'G') { if (prec == 0) prec = 1; }
            }
            double scale = 1; for (int i = 0; i < prec; i++) scale *= 10;
            double r = d * scale + 0.5;
            unsigned long long ip = (unsigned long long)(r / scale);
            unsigned long long fp = (unsigned long long)(r - (double)ip * scale);
            char ib[32], fb[32];
            int in = fmt_uint(ib, ip, 10, 0);
            int fn = fmt_uint(fb, fp, 10, 0);
            int fl = prec;
            if ((c == 'g' || c == 'G')) {                 /* trim trailing zeros of the fraction */
                char fz[32]; int k = 0; for (int i = 0; i < fl - fn; i++) fz[k++] = '0'; for (int i = fn; i > 0; i--) fz[k++] = fb[i - 1];
                int keep = k; while (keep > 0 && fz[keep - 1] == '0') keep--;
                fl = keep;
                int total = neg + in + (fl ? 1 + fl : 0);
                if (!left) pad(o, width - total, ' ');
                if (neg) put(o, '-');
                while (in > 0) put(o, ib[--in]);
                if (fl) { put(o, '.'); for (int i = 0; i < fl; i++) put(o, fz[i]); }
                if (left) pad(o, width - total, ' ');
                continue;
            }
            int total = neg + in + (prec ? 1 + prec : 0) + (plus && !neg);
            if (!left && !zero) pad(o, width - total, ' ');
            if (neg) put(o, '-'); else if (plus) put(o, '+');
            if (!left && zero) pad(o, width - total, '0');
            while (in > 0) put(o, ib[--in]);
            if (prec) { put(o, '.'); pad(o, prec - fn, '0'); while (fn > 0) put(o, fb[--fn]); }
            if (left) pad(o, width - total, ' ');
            continue;
        }
        put(o, '%'); put(o, c);
    }
    if (o->cap) o->buf[o->len < o->cap ? o->len : o->cap - 1] = 0;
    return (int)o->len;
}
EXPORT int vsnprintf(char *b, size_t n, const char *f, va_list ap) { out_t o = { b, n, 0 }; return vfmt(&o, f, ap); }
EXPORT int _vsnprintf(char *b, size_t n, const char *f, va_list ap) { return vsnprintf(b, n, f, ap); }
EXPORT int vsprintf(char *b, const char *f, va_list ap) { return vsnprintf(b, (size_t)-1 >> 1, f, ap); }
EXPORT int snprintf(char *b, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
EXPORT int _snprintf(char *b, size_t n, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsnprintf(b, n, f, ap); va_end(ap); return r; }
EXPORT int sprintf(char *b, const char *f, ...) { va_list ap; va_start(ap, f); int r = vsprintf(b, f, ap); va_end(ap); return r; }
static int out_fd(void *fp, const char *f, va_list ap) {
    char stackbuf[1024];
    out_t o = { stackbuf, sizeof stackbuf, 0 };
    int n = vfmt(&o, f, ap);
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

__attribute__((dllexport)) int __stdcall DllMain(void *h, unsigned reason, void *res) { (void)h; (void)reason; (void)res; return 1; }
