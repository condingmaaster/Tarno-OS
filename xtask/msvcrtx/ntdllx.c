// ntdllx.dll — more of ntdll for THOS's NT personality (the loader asks it before the built-in ntdll):
// NtCreateFile / NtOpenFile over POSIX open, and the status-code translation.
#include "wincommon.h"
#include "unwind.h"

typedef struct { u16 len, max; u16 *buf; } UNISTR;
typedef struct { u32 length; u32 pad0; HANDLE root; UNISTR *name; u32 attrs; u32 pad1; void *sd, *sqos; } OBJATTR;
typedef struct { i64 status; u64 info; } IOSB;
#define ST_OK 0
#define ST_NOT_FOUND 0xC0000034u
#define ST_COLLISION 0xC0000035u
#define ST_ACCESS 0xC0000022u
#define ST_PATH_NOT_FOUND 0xC000003Au
#define ST_INVALID 0xC000000Du
#define ST_NOT_IMPL 0xC0000002u

static long to_status(i64 e) {
    switch (-e) { case 2: return (long)ST_NOT_FOUND; case 20: case 21: return (long)ST_ACCESS; case 13: case 1: return (long)ST_ACCESS; case 17: return (long)ST_COLLISION; case 22: return (long)ST_INVALID; default: return (long)0xC0000001u; }
}
/* anonymous pipes the way the Rust standard library makes them: open \??\PIPE\ (a directory stand-in), create the server end
   with NtCreateNamedPipeFile relative to it, open the client end relative to the server handle */
static struct { int server, other; } anon_pipes[32];
static int ieq(const char *a, const char *b) { while (*a && *b) { char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b; if (x != y) return 0; a++; b++; } return !*a && !*b; }
EXPORT long WINAPI NtCreateFile(HANDLE *out, u32 access, OBJATTR *oa, IOSB *iosb, i64 *alloc, u32 attrs, u32 share, u32 disp, u32 opts, void *ea, u32 ealen) {
    (void)alloc; (void)attrs; (void)share; (void)ea; (void)ealen;
    if (oa && oa->root) {                                                            /* the client end of an anonymous pipe: relative to the server handle */
        for (int i = 0; i < 32; i++) if (anon_pipes[i].server == (int)(i64)oa->root && anon_pipes[i].other > 0) {
            *out = (HANDLE)(i64)anon_pipes[i].other; anon_pipes[i].other = 0; anon_pipes[i].server = 0;
            if (iosb) { iosb->status = ST_OK; iosb->info = 1; }
            return 0;
        }
        return (long)ST_INVALID;
    }
    if (!oa || !oa->name) return (long)ST_INVALID;                                   /* only absolute names */
    char dos[1024]; u16 tmp[512]; u32 n = oa->name->len / 2; if (n > 511) n = 511;
    for (u32 i = 0; i < n; i++) tmp[i] = oa->name->buf[i];
    tmp[n] = 0; w2a(tmp, dos, sizeof dos);
    if (ieq(dos, "\\??\\PIPE\\") || ieq(dos, "\\??\\PIPE") || ieq(dos, "\\Device\\NamedPipe\\") || ieq(dos, "\\Device\\NamedPipe")) {                       /* the named-pipe "file system" */
        i64 d = sc3(2, "/", 0200000, 0);
        if (d < 0) return to_status(d);
        if (iosb) { iosb->status = ST_OK; iosb->info = 1; }
        *out = (HANDLE)d; return 0;
    }
    char p[1024]; dos_to_posix(p, dos, sizeof p);
    int rd = (access & (0x80000000u | 1 | 0x20000 | 0x120089)) != 0, wr = (access & (0x40000000u | 2 | 4 | 0x120116)) != 0;
    int fl = wr ? (rd ? 2 : 1) : 0;
    int created = 0;
    i64 fd;
    if (opts & 1) {                                                                  /* FILE_DIRECTORY_FILE */
        if (disp == 2 || disp == 3) {
            i64 r = sc3(83, p, 0755, 0);
            if (r == 0) created = 1; else if (!(r == -17 && disp == 3)) return to_status(r);
        }
        fd = sc3(2, p, 0200000, 0);                                                  /* O_DIRECTORY */
        if (fd < 0) return to_status(fd);
        if (iosb) { iosb->status = ST_OK; iosb->info = created ? 2 : 1; }
        *out = (HANDLE)fd; return 0;
    }
    switch (disp) {
        case 0: fl |= 0100 | 01000; break;                                           /* FILE_SUPERSEDE */
        case 1: break;                                                               /* FILE_OPEN */
        case 2: fl |= 0100 | 0200; break;                                            /* FILE_CREATE */
        case 3: fl |= 0100; break;                                                   /* FILE_OPEN_IF */
        case 4: fl |= 01000; if (!wr) fl |= 1; break;                                /* FILE_OVERWRITE */
        case 5: fl |= 0100 | 01000; break;                                           /* FILE_OVERWRITE_IF */
        default: return (long)ST_INVALID;
    }
    if (disp == 3 || disp == 5) { i64 probe = sc3(2, p, 0, 0); if (probe < 0) created = 1; else sc3(3, probe, 0, 0); }
    if (disp == 2) created = 1;
    fd = sc3(2, p, fl, 0644);
    if (fd < 0) return to_status(fd);
    if (iosb) { iosb->status = ST_OK; iosb->info = created ? 2 : 1; }
    *out = (HANDLE)fd;
    return 0;
}
EXPORT long WINAPI NtOpenFile(HANDLE *out, u32 access, OBJATTR *oa, IOSB *iosb, u32 share, u32 opts) { return NtCreateFile(out, access, oa, iosb, 0, 0, share, 1, opts, 0, 0); }
EXPORT long WINAPI NtCreateNamedPipeFile(HANDLE *h, u32 access, OBJATTR *oa, IOSB *io, u32 share, u32 disp, u32 opts, u32 type, u32 readmode, u32 compl, u32 maxinst, u32 inq, u32 outq, i64 *to) {
    (void)oa; (void)share; (void)disp; (void)opts; (void)type; (void)readmode; (void)compl; (void)maxinst; (void)inq; (void)outq; (void)to;
    int fds[2]; if (sc3(22, fds, 0, 0) < 0) return (long)0xC0000001u;
    int server_reads = (access & (0x80000000u | 1)) != 0;                           /* GENERIC_READ / FILE_READ_DATA */
    int slot = -1; for (int i = 0; i < 32; i++) if (!anon_pipes[i].server) { slot = i; break; }
    if (slot < 0) { sc3(3, fds[0], 0, 0); sc3(3, fds[1], 0, 0); return (long)0xC0000001u; }
    anon_pipes[slot].server = server_reads ? fds[0] : fds[1]; anon_pipes[slot].other = server_reads ? fds[1] : fds[0];
    *h = (HANDLE)(i64)anon_pipes[slot].server;
    if (io) { io->status = ST_OK; io->info = 1; }
    return 0;
}
EXPORT u32 WINAPI RtlNtStatusToDosError(long st) {
    switch ((u32)st) {
        case 0: return 0;
        case ST_NOT_FOUND: return 2; case ST_PATH_NOT_FOUND: return 3; case ST_ACCESS: return 5; case ST_COLLISION: return 183;
        case 0xC0000008u: return 6; case ST_INVALID: return 87; case ST_NOT_IMPL: return 120; case 0xC0000005u: return 998;
        case 0x80000005u: return 234; case 0xC000009Au: return 8; case 0xC0000011u: return 38;
        default: return 317;
    }
}
EXPORT void WINAPI RtlRaiseException(unsigned char *rec) {
    unsigned char *ctx = (unsigned char *)__builtin_alloca(CX_SIZE + 16); ctx = (unsigned char *)(((u64)ctx + 15) & ~15ULL);
    RtlCaptureContext(ctx);
    *(u64 *)(rec + 0x10) = *(u64 *)(ctx + CX_RIP);
    if (uw_dispatch(rec, ctx)) RtlRestoreContext(ctx, rec);
    sc3(231, *(u32 *)rec & 0xFF ? *(u32 *)rec & 0xFF : 3, 0, 0);
    for (;;) {}
}
__attribute__((dllexport)) int __stdcall DllMain(void *h, unsigned reason, void *res) { (void)h; (void)res; if (reason == 1) uw_install(); return 1; }
