// SPDX-License-Identifier: GPL-2.0-or-later
// x64 Windows exception unwinding for THOS's NT personality, freestanding C: RtlLookupFunctionEntry,
// RtlVirtualUnwind, RtlUnwindEx / RtlUnwind, RtlCaptureContext / RtlRestoreContext and a frame-based dispatcher
// (RtlDispatchException, run as a last-resort vectored handler). Included by ntdllx.c and kernel32x.c
// (mingw-built C++/Rust programs import these from kernel32.dll, MSVC ones from ntdll.dll).
// Layouts: RUNTIME_FUNCTION { u32 begin, end, unwind }, UNWIND_INFO { ver:3 flags:5, prolog, ncodes, frame:4 off:4, codes[] ... }.

#define UW_EHANDLER 1
#define UW_UHANDLER 2
#define UW_CHAININFO 4
#define EXC_UNWINDING 0x2
#define EXC_EXIT_UNWIND 0x4
#define EXC_NESTED 0x10
#define EXC_TARGET_UNWIND 0x20
#define EXC_COLLIDED 0x40

/* CONTEXT offsets */
#define CX_FLAGS 0x30
#define CX_MXCSR 0x34
#define CX_EFLAGS 0x44
#define CX_RAX 0x78
#define CX_RSP 0x98
#define CX_RBP 0xA0
#define CX_RIP 0xF8
#define CX_XMM0 0x1A0
#define CX_SIZE 0x4D0

typedef struct { u32 begin, end, unwind; } RTFUNC;
typedef struct DISPATCH {
    u64 control_pc, image_base;
    RTFUNC *func;
    u64 establisher, target_ip;
    unsigned char *ctx;
    void *language_handler, *handler_data, *history;
    u32 scope_index, fill;
} DISPATCH;
typedef int (WINAPI *EXC_ROUTINE)(unsigned char *rec, void *frame, unsigned char *ctx, DISPATCH *d);

static inline u64 *cx_reg(unsigned char *ctx, int reg) { return (u64 *)(ctx + CX_RAX + 8 * reg); }   /* 0 rax 1 rcx 2 rdx 3 rbx 4 rsp 5 rbp 6 rsi 7 rdi 8..15 r8..r15 */

/* ---- module lookup: the loader's list in the PEB (Ldr InMemoryOrderModuleList) ---- */
static RTFUNC *uw_lookup(u64 pc, u64 *base_out) {
    u64 peb; __asm__("mov %%gs:0x60, %0" : "=r"(peb));
    unsigned char *ldr = *(unsigned char **)(peb + 0x18);
    unsigned char *head = ldr + 0x20, *e = *(unsigned char **)head;
    for (int guard = 0; e && e != head && guard < 64; e = *(unsigned char **)e, guard++) {
        unsigned char *ent = e - 0x10;
        u64 base = *(u64 *)(ent + 0x30); u32 size = *(u32 *)(ent + 0x40);
        if (!base || pc < base || pc >= base + size) continue;
        u32 pe = *(u32 *)(base + 0x3C);
        u32 rva = *(u32 *)(base + pe + 24 + 112 + 3 * 8), len = *(u32 *)(base + pe + 24 + 112 + 3 * 8 + 4);
        if (!rva || !len) return 0;
        RTFUNC *t = (RTFUNC *)(base + rva); long lo = 0, hi = (long)(len / sizeof(RTFUNC)) - 1; u32 off = (u32)(pc - base);
        while (lo <= hi) {
            long mid = (lo + hi) / 2;
            if (off < t[mid].begin) hi = mid - 1; else if (off >= t[mid].end) lo = mid + 1; else { *base_out = base; return &t[mid]; }
        }
        return 0;
    }
    return 0;
}
EXPORT RTFUNC *WINAPI RtlLookupFunctionEntry(u64 pc, u64 *base, void *hist) { (void)hist; u64 b = 0; RTFUNC *f = uw_lookup(pc, &b); if (base) *base = f ? b : 0; return f; }

/* ---- unwinding one frame ---- */
EXPORT void *WINAPI RtlVirtualUnwind(u32 type, u64 base, u64 pc, RTFUNC *fe, unsigned char *ctx, void **handler_data, u64 *establisher, void *ptrs) {
    (void)ptrs;
    u64 rsp = *cx_reg(ctx, 4);
    unsigned char *info = (unsigned char *)(base + fe->unwind);
    u32 offset = (u32)(pc - (base + fe->begin));
    void *handler = 0; void *hdata = 0;
    int first = 1;
    for (;;) {
        unsigned flags = info[0] >> 3, ncodes = info[2], frame_reg = info[3] & 15, frame_off = info[3] >> 4, prolog = info[1];
        unsigned char *codes = info + 4;
        if (first || 1) {
            if (frame_reg && (offset >= prolog || !first)) {            /* the frame register is live: rsp is derived from it */
                u64 fp = *cx_reg(ctx, frame_reg);
                rsp = fp - (u64)frame_off * 16;
            }
        }
        for (unsigned i = 0; i < ncodes; i++) {
            u16 code = *(u16 *)(codes + 2 * i); unsigned op = (code >> 8) & 15, opinfo = (code >> 12) & 15, co = code & 255;
            unsigned slots = 1;
            switch (op) { case 1: slots = opinfo == 0 ? 2 : 3; break; case 4: case 8: slots = 2; break; case 5: case 9: slots = 3; break; default: break; }
            int applies = !first || offset >= prolog || co <= offset;     /* inside the prologue only what has already executed */
            if (op == 6) slots = 1;                                        /* (version 2 epilogue marker) */
            if (applies) switch (op) {
                case 0: *cx_reg(ctx, (int)opinfo) = *(u64 *)rsp; rsp += 8; break;                                   /* PUSH_NONVOL */
                case 1: rsp += opinfo == 0 ? (u64)(*(u16 *)(codes + 2 * (i + 1))) * 8 : *(u32 *)(codes + 2 * (i + 1)); break;   /* ALLOC_LARGE */
                case 2: rsp += (u64)opinfo * 8 + 8; break;                                                          /* ALLOC_SMALL */
                case 3: rsp = *cx_reg(ctx, frame_reg) - (u64)frame_off * 16; break;                                 /* SET_FPREG */
                case 4: *cx_reg(ctx, (int)opinfo) = *(u64 *)(rsp + (u64)(*(u16 *)(codes + 2 * (i + 1))) * 8); break;  /* SAVE_NONVOL */
                case 5: *cx_reg(ctx, (int)opinfo) = *(u64 *)(rsp + *(u32 *)(codes + 2 * (i + 1))); break;           /* SAVE_NONVOL_FAR */
                case 8: { u64 *src = (u64 *)(rsp + (u64)(*(u16 *)(codes + 2 * (i + 1))) * 16); u64 *dst = (u64 *)(ctx + CX_XMM0 + 16 * opinfo); dst[0] = src[0]; dst[1] = src[1]; break; }   /* SAVE_XMM128 */
                case 9: { u64 *src = (u64 *)(rsp + *(u32 *)(codes + 2 * (i + 1))); u64 *dst = (u64 *)(ctx + CX_XMM0 + 16 * opinfo); dst[0] = src[0]; dst[1] = src[1]; break; }
                case 10: { u64 *fr = (u64 *)rsp; if (opinfo) rsp += 8; *(u64 *)(ctx + CX_RIP) = fr[(opinfo ? 1 : 0)]; *cx_reg(ctx, 4) = fr[(opinfo ? 4 : 3)]; *establisher = rsp; return 0; }   /* PUSH_MACHFRAME */
                default: break;
            }
            i += slots - 1;
        }
        /* handler of the (outermost) function: the flags of its own UNWIND_INFO */
        unsigned nc = (ncodes + 1) & ~1u;
        if (flags & UW_CHAININFO) {
            RTFUNC *chain = (RTFUNC *)(codes + 2 * nc);
            info = (unsigned char *)(base + chain->unwind);
            first = 0; offset = 0xFFFFFFFF;
            continue;
        }
        if (flags & (type == UW_UHANDLER ? UW_UHANDLER : UW_EHANDLER)) {
            handler = (void *)(base + *(u32 *)(codes + 2 * nc));
            hdata = codes + 2 * nc + 4;
        }
        break;
    }
    *establisher = (*(unsigned char *)(base + fe->unwind + 3) & 15) ? rsp : rsp;      /* the frame address handlers receive */
    *(u64 *)(ctx + CX_RIP) = *(u64 *)rsp; rsp += 8;
    *cx_reg(ctx, 4) = rsp;
    if (handler_data) *handler_data = hdata;
    return handler;
}

/* ---- capture / restore ---- */
EXPORT __attribute__((naked)) void WINAPI RtlCaptureContext(void *ctx) {
    __asm__ volatile(
        "pushfq\n\t"
        "mov %rax,0x78(%rcx)\n\tmov %rcx,0x80(%rcx)\n\tmov %rdx,0x88(%rcx)\n\tmov %rbx,0x90(%rcx)\n\t"
        "lea 16(%rsp),%rax\n\tmov %rax,0x98(%rcx)\n\t"
        "mov %rbp,0xa0(%rcx)\n\tmov %rsi,0xa8(%rcx)\n\tmov %rdi,0xb0(%rcx)\n\t"
        "mov %r8,0xb8(%rcx)\n\tmov %r9,0xc0(%rcx)\n\tmov %r10,0xc8(%rcx)\n\tmov %r11,0xd0(%rcx)\n\t"
        "mov %r12,0xd8(%rcx)\n\tmov %r13,0xe0(%rcx)\n\tmov %r14,0xe8(%rcx)\n\tmov %r15,0xf0(%rcx)\n\t"
        "mov 8(%rsp),%rax\n\tmov %rax,0xf8(%rcx)\n\t"
        "popq %rax\n\tmov %eax,0x44(%rcx)\n\t"
        "movl $0x10001f,0x30(%rcx)\n\t"
        "stmxcsr 0x34(%rcx)\n\t"
        "movaps %xmm0,0x1a0(%rcx)\n\tmovaps %xmm1,0x1b0(%rcx)\n\tmovaps %xmm2,0x1c0(%rcx)\n\tmovaps %xmm3,0x1d0(%rcx)\n\t"
        "movaps %xmm4,0x1e0(%rcx)\n\tmovaps %xmm5,0x1f0(%rcx)\n\tmovaps %xmm6,0x200(%rcx)\n\tmovaps %xmm7,0x210(%rcx)\n\t"
        "movaps %xmm8,0x220(%rcx)\n\tmovaps %xmm9,0x230(%rcx)\n\tmovaps %xmm10,0x240(%rcx)\n\tmovaps %xmm11,0x250(%rcx)\n\t"
        "movaps %xmm12,0x260(%rcx)\n\tmovaps %xmm13,0x270(%rcx)\n\tmovaps %xmm14,0x280(%rcx)\n\tmovaps %xmm15,0x290(%rcx)\n\t"
        "mov %cs,%ax\n\tmov %ax,0x38(%rcx)\n\tmov %ss,%ax\n\tmov %ax,0x42(%rcx)\n\t"
        "ret\n\t");
}
EXPORT __attribute__((naked)) void WINAPI RtlRestoreContext(void *ctx, void *rec) {
    __asm__ volatile(
        "movaps 0x1a0(%rcx),%xmm0\n\tmovaps 0x1b0(%rcx),%xmm1\n\tmovaps 0x1c0(%rcx),%xmm2\n\tmovaps 0x1d0(%rcx),%xmm3\n\t"
        "movaps 0x1e0(%rcx),%xmm4\n\tmovaps 0x1f0(%rcx),%xmm5\n\tmovaps 0x200(%rcx),%xmm6\n\tmovaps 0x210(%rcx),%xmm7\n\t"
        "movaps 0x220(%rcx),%xmm8\n\tmovaps 0x230(%rcx),%xmm9\n\tmovaps 0x240(%rcx),%xmm10\n\tmovaps 0x250(%rcx),%xmm11\n\t"
        "movaps 0x260(%rcx),%xmm12\n\tmovaps 0x270(%rcx),%xmm13\n\tmovaps 0x280(%rcx),%xmm14\n\tmovaps 0x290(%rcx),%xmm15\n\t"
        "ldmxcsr 0x34(%rcx)\n\t"
        "mov 0x98(%rcx),%rsp\n\t"
        "push 0xf8(%rcx)\n\t"
        "push 0x44(%rcx)\n\tpopfq\n\t"
        "mov 0x78(%rcx),%rax\n\tmov 0x88(%rcx),%rdx\n\tmov 0x90(%rcx),%rbx\n\tmov 0xa0(%rcx),%rbp\n\t"
        "mov 0xa8(%rcx),%rsi\n\tmov 0xb0(%rcx),%rdi\n\tmov 0xb8(%rcx),%r8\n\tmov 0xc0(%rcx),%r9\n\t"
        "mov 0xc8(%rcx),%r10\n\tmov 0xd0(%rcx),%r11\n\tmov 0xd8(%rcx),%r12\n\tmov 0xe0(%rcx),%r13\n\t"
        "mov 0xe8(%rcx),%r14\n\tmov 0xf0(%rcx),%r15\n\tmov 0x80(%rcx),%rcx\n\t"
        "ret\n\t");
}

static void uw_copy(unsigned char *d, const unsigned char *s) { for (int i = 0; i < CX_SIZE; i++) d[i] = s[i]; }
static void uw_die(u32 code) { (void)code; sc3(231, 0x7F, 0, 0); for (;;) {} }

/* one frame: fills `d` and unwinds `ctx` into the caller's context */
static void uw_step(u32 type, DISPATCH *d, unsigned char *ctx) {
    u64 pc = *(u64 *)(ctx + CX_RIP);
    d->control_pc = pc; d->handler_data = 0; d->language_handler = 0;
    d->func = uw_lookup(pc, &d->image_base);
    if (d->func) {
        void *hd = 0; u64 est = 0;
        d->language_handler = RtlVirtualUnwind(type, d->image_base, pc, d->func, ctx, &hd, &est, 0);
        d->handler_data = hd; d->establisher = est;
    } else {                                                           /* a leaf: the return address is at the top of the stack */
        u64 rsp = *cx_reg(ctx, 4);
        d->establisher = rsp; d->image_base = 0;
        *(u64 *)(ctx + CX_RIP) = *(u64 *)rsp; *cx_reg(ctx, 4) = rsp + 8;
    }
}

EXPORT void WINAPI RtlUnwindEx(void *end_frame, void *target_ip, unsigned char *rec, void *retval, unsigned char *context, void *table) {
    static unsigned char own_rec[0x98];
    unsigned char *nctx = (unsigned char *)__builtin_alloca(CX_SIZE + 16); nctx = (unsigned char *)(((u64)nctx + 15) & ~15ULL);
    unsigned char *ctx = context;
    if (!ctx) { ctx = (unsigned char *)__builtin_alloca(CX_SIZE + 16); ctx = (unsigned char *)(((u64)ctx + 15) & ~15ULL); }
    RtlCaptureContext(ctx);
    uw_copy(nctx, ctx);
    if (!rec) { for (int i = 0; i < 0x98; i++) own_rec[i] = 0; *(u32 *)own_rec = 0xC0000027u; *(u64 *)(own_rec + 0x10) = *(u64 *)(ctx + CX_RIP); rec = own_rec; }
    *(u32 *)(rec + 4) |= EXC_UNWINDING | (end_frame ? 0 : EXC_EXIT_UNWIND);
    DISPATCH d; d.target_ip = (u64)target_ip; d.ctx = ctx; d.history = table; d.scope_index = 0; d.fill = 0;
    for (;;) {
        uw_step(UW_UHANDLER, &d, nctx);
      again:
        if (!d.establisher) break;
        if (d.language_handler) {
            if (end_frame && d.establisher > (u64)end_frame) uw_die(0xC0000029u);
            if (d.establisher == (u64)end_frame) *(u32 *)(rec + 4) |= EXC_TARGET_UNWIND;
            int disp = ((EXC_ROUTINE)d.language_handler)(rec, (void *)d.establisher, ctx, &d);
            if (disp == 3) {                                           /* ExceptionCollidedUnwind: continue from the context the handler left */
                ctx = d.ctx; uw_copy(nctx, ctx);
                uw_step(UW_UHANDLER, &d, nctx);
                goto again;
            } else if (disp != 1) uw_die(0xC0000026u);
            *(u32 *)(rec + 4) &= ~EXC_COLLIDED;
        }
        if (d.establisher == (u64)end_frame) break;
        uw_copy(ctx, nctx);
    }
    *(u64 *)(ctx + CX_RAX) = (u64)retval;
    *(u64 *)(ctx + CX_RIP) = (u64)target_ip;
    RtlRestoreContext(ctx, rec);
}
EXPORT void WINAPI RtlUnwind(void *target_frame, void *target_ip, unsigned char *rec, void *retval) { RtlUnwindEx(target_frame, target_ip, rec, retval, 0, 0); }

/* ---- first-phase dispatch of a raised exception: walk the frames' exception handlers ---- */
static int uw_dispatch(unsigned char *rec, unsigned char *ctx) {                 /* 1 = handled (continue), 0 = not handled */
    unsigned char *nctx = (unsigned char *)__builtin_alloca(CX_SIZE + 16); nctx = (unsigned char *)(((u64)nctx + 15) & ~15ULL);
    unsigned char *c2 = (unsigned char *)__builtin_alloca(CX_SIZE + 16); c2 = (unsigned char *)(((u64)c2 + 15) & ~15ULL);
    uw_copy(nctx, ctx);
    DISPATCH d; d.target_ip = 0; d.history = 0; d.scope_index = 0; d.fill = 0;
    for (int depth = 0; depth < 512; depth++) {
        uw_copy(c2, nctx);
        d.ctx = c2;
        uw_step(UW_EHANDLER, &d, nctx);
        if (!d.establisher) return 0;
        if (d.language_handler) {
            int disp = ((EXC_ROUTINE)d.language_handler)(rec, (void *)d.establisher, c2, &d);
            if (disp == 0) { return 1; }                                   /* ExceptionContinueExecution */
            if (disp == 2) return 0;
        }
        if (!d.func) { if (*(u64 *)(nctx + CX_RIP) == 0) return 0; }
    }
    return 0;
}
/* registered as a last-resort vectored handler: -1 = resume, 0 = continue searching */
static long WINAPI uw_vectored(void **ptrs) {
    unsigned char *rec = ptrs[0], *ctx = ptrs[1];
    if (*(u32 *)(rec + 4) & EXC_UNWINDING) return 0;
    return uw_dispatch(rec, ctx) ? -1 : 0;
}
extern void *WINAPI RtlAddVectoredExceptionHandler(u32 first, void *handler);
static void uw_install(void) { RtlAddVectoredExceptionHandler(0, (void *)uw_vectored); }
