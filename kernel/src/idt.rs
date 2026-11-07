// SPDX-License-Identifier: GPL-2.0-or-later
//! Phase 1 — IDT + CPU exception handlers.
//!
//! Only the CPU-defined vectors (0..32) for now. Hardware IRQs (APIC) and the
//! `syscall` fast path come with the interrupt-controller and personality work.
//!
//! Every fatal handler dumps the trap frame over serial and halts via
//! `exit_qemu(Failed)` so a headless run fails loudly instead of spinning.

use spin::Lazy;
use x86_64::structures::idt::{InterruptDescriptorTable, InterruptStackFrame};
use x86_64::VirtAddr;

use crate::{apic, exit_qemu, gdt, hcf, kprintln, seh, ExitCode};

fn a(f: unsafe extern "C" fn()) -> VirtAddr {
    VirtAddr::new(f as *const () as u64)
}

// --- hand-rolled entry stubs for the asynchronous hardware IRQs ---
//
// The compiler's `extern "x86-interrupt"` stubs never touch `%gs`. That is
// fine as long as `%gs` is always the per-CPU base wherever an IRQ can land —
// true for kernel code and for POSIX ring 3 (its user `%gs` base *is* the
// per-CPU pointer). It stops being true once a **PE** thread runs in ring 3
// with `%gs` = its TEB: a timer / AHCI IRQ taken there would run
// `sched::on_tick()` etc. with `gs:0` pointing into the TEB, not `PerCpu`.
//
// So these vectors get the same conditional-`swapgs` shim the SEH fault
// stubs already use (`crate::seh`): `swapgs` on entry iff the saved `CS`
// says we interrupted ring 3, `swapgs` back on the way out iff we did on the
// way in. This is what lets a PE thread finally run with `IF=1` (preemptible)
// instead of being cooperatively scheduled with `IF=0`.
//
// No error code is pushed for these vectors, so on entry the stack is
// [rip][cs][rflags][rsp][ss]; `lfence` after `swapgs` is the CVE-2019-1125
// ("SWAPGS") speculation guard.
core::arch::global_asm!(
    r#"
.text

.macro IRQ_ENTRY name, body
.globl \name
\name:
    test byte ptr [rsp + 8], 3      // saved CS.RPL: did we interrupt ring 3?
    jz   1f
    swapgs
    lfence
1:  push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov  rbp, rsp
    mov  rdi, rbp                   // arg 0: the saved register frame (rip at +120, cs at +128)
    and  rsp, -16                   // dynamic 16-byte align for the SysV call
    call thos_cpu_irq_enter
    mov  rdi, rbp
    call \body
    mov  rdi, rbp
    call thos_cpu_irq_leave
    mov  rsp, rbp
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rdi
    pop  rsi
    pop  rbp
    pop  rbx
    pop  rdx
    pop  rcx
    pop  rax
    test byte ptr [rsp + 8], 3      // symmetric: unswap iff we swapped
    jz   2f
    swapgs
2:  iretq
.endm

IRQ_ENTRY thos_irq_timer, thos_irq_timer_body
IRQ_ENTRY thos_irq_ahci,  thos_irq_ahci_body
IRQ_ENTRY thos_irq_input, thos_irq_input_body
IRQ_ENTRY thos_irq_kick,  thos_irq_kick_body
"#
);

extern "C" {
    fn thos_irq_timer();
    fn thos_irq_ahci();
    fn thos_irq_input();
    fn thos_irq_kick();
}

/// Body of the APIC timer IRQ — see the removed `apic_timer` for the previous
/// (compiler-stub) form. Runs with `IF=0` and `%gs` guaranteed to be the
/// per-CPU base by the `thos_irq_timer` shim.
#[no_mangle]
extern "C" fn thos_irq_timer_body(frame: *const u64) {
    apic::on_timer_tick();
    crate::random::add_event(apic::ticks());
    apic::eoi();
    crate::timer::tick(); // advance the monotonic clock + wake timed sleepers (CPU 0 only; 1 kHz)
    // Keep a 10-ms slice in elapsed time. Ten delivered IRQs can represent
    // much longer when an interrupt-disabled syscall coalesces timer ticks.
    static SLICE: [core::sync::atomic::AtomicU64; crate::gdt::MAX_CPUS] = [const { core::sync::atomic::AtomicU64::new(0) }; crate::gdt::MAX_CPUS];
    if !crate::sched::is_started() {
        return; // per-CPU state (gs) is not live yet
    }
    let cpu = crate::smp::this_cpu() as usize;
    let now = crate::timer::monotonic_ns();
    if now.saturating_sub(SLICE[cpu].load(core::sync::atomic::Ordering::Relaxed)) < 10_000_000 {
        return;
    }
    SLICE[cpu].store(now, core::sync::atomic::Ordering::Relaxed);
    crate::ahci::poll_wake(); // safety net for a dropped AHCI completion IRQ
    // Interrupted ring 3 (saved CS.RPL == 3, 16th slot after the 15 pushed GPRs)?
    // Then a fatal signal ends the process even if it never makes a syscall.
    if unsafe { *frame.add(16) } & 3 == 3 {
        crate::signal::irq_check_fatal();
    }
    crate::sched::on_tick();
}

/// A thread became runnable while this CPU was idle: run the scheduler now.
#[no_mangle]
extern "C" fn thos_irq_kick_body(_frame: *const u64) {
    apic::eoi();
    crate::sched::on_tick();
}

/// PS/2 keyboard / mouse byte ready: stash it and wake the input thread.
#[no_mangle]
extern "C" fn thos_irq_input_body(_frame: *const u64) {
    crate::ps2::irq();
    apic::eoi();
}

#[no_mangle]
extern "C" fn thos_irq_ahci_body(_frame: *const u64) {
    crate::ahci::on_irq();
    apic::eoi();
}

static IDT: Lazy<InterruptDescriptorTable> = Lazy::new(|| {
    let mut idt = InterruptDescriptorTable::new();

    idt.breakpoint.set_handler_fn(breakpoint);
    idt.stack_segment_fault.set_handler_fn(stack_segment_fault);

    // #DE / #UD / #GP / #PF route through GPR-saving stubs so a PE process's
    // fault can be delivered to ring-3 SEH (see `crate::seh`).
    unsafe {
        idt.divide_error.set_handler_addr(a(seh::thos_de_entry));
        idt.invalid_opcode.set_handler_addr(a(seh::thos_ud_entry));
        idt.general_protection_fault.set_handler_addr(a(seh::thos_gp_entry));

        idt.double_fault
            .set_handler_fn(double_fault)
            .set_stack_index(gdt::DOUBLE_FAULT_IST_INDEX);
        idt.non_maskable_interrupt
            .set_handler_fn(nmi)
            .set_stack_index(gdt::NMI_IST_INDEX);
        idt.page_fault
            .set_handler_addr(a(seh::thos_pf_entry));
        // Demand faults can park. A per-CPU IST would be overwritten by
        // another task's fault while the first task sleeps. User faults use
        // the thread's TSS RSP0; double faults keep an emergency IST stack.
    }

    // APIC vectors (>= 32). Timer + AHCI use hand-rolled entry stubs with a
    // conditional `swapgs` (see the `global_asm!` above) so they are safe to
    // take while a PE thread is in ring 3 with `%gs` = TEB. The spurious
    // handler touches no per-CPU state, so the compiler stub is fine there.
    unsafe {
        idt[apic::TIMER_VECTOR].set_handler_addr(a(thos_irq_timer));
        idt[apic::AHCI_VECTOR].set_handler_addr(a(thos_irq_ahci));
        idt[apic::KBD_VECTOR].set_handler_addr(a(thos_irq_input));
        idt[apic::MOUSE_VECTOR].set_handler_addr(a(thos_irq_input));
        idt[apic::KICK_VECTOR].set_handler_addr(a(thos_irq_kick));
    }
    idt[apic::SPURIOUS_VECTOR].set_handler_fn(apic_spurious);

    idt
});

pub fn init() {
    IDT.load();
}

// --- non-fatal ---

extern "x86-interrupt" fn breakpoint(frame: InterruptStackFrame) {
    kprintln!(
        "THOS trap: #BP at {:#x}",
        frame.instruction_pointer.as_u64()
    );
}

extern "x86-interrupt" fn apic_spurious(_frame: InterruptStackFrame) {
    // A spurious interrupt gets no EOI by design.
}

// --- fatal ---

extern "x86-interrupt" fn nmi(frame: InterruptStackFrame) {
    kprintln!("THOS trap: NMI\n{:#?}", frame);
    exit_qemu(ExitCode::Failed);
    hcf();
}

extern "x86-interrupt" fn stack_segment_fault(frame: InterruptStackFrame, code: u64) {
    fatal("#SS stack-segment fault", &frame, Some(code));
}

extern "x86-interrupt" fn double_fault(frame: InterruptStackFrame, code: u64) -> ! {
    kprintln!("THOS trap: #DF double fault (error {:#x})\n{:#?}", code, frame);
    exit_qemu(ExitCode::Failed);
    hcf();
}

/// A fault from ring 3 kills the process; a fault in the kernel is fatal.
fn fatal(name: &str, frame: &InterruptStackFrame, code: Option<u64>) -> ! {
    let from_user = frame.code_segment.rpl() == x86_64::PrivilegeLevel::Ring3;
    match code {
        Some(c) => kprintln!("THOS trap: {} (error {:#x}){}", name, c, if from_user { " [user]" } else { "" }),
        None => kprintln!("THOS trap: {}{}", name, if from_user { " [user]" } else { "" }),
    }
    if from_user {
        kprintln!("  killed user rip={:#x}", frame.instruction_pointer.as_u64());
        crate::syscall::note_user_exit();
        crate::process::set_term_signal(11); // SIGSEGV
        crate::sched::exit();
    }
    kprintln!("{:#?}", frame);
    exit_qemu(ExitCode::Failed);
    hcf();
}
