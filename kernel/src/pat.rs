// SPDX-License-Identifier: GPL-2.0-or-later
//! PAT slot 1 is WC, as on Linux. THOS does not use write-through PTEs.
//! Configure before framebuffer mappings and AP startup; each AP repeats it.
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use x86_64::registers::{control::{Cr0, Cr0Flags, Cr4, Cr4Flags}, model_specific::Msr};
static ACTIVE: AtomicBool = AtomicBool::new(false);
static VALUE: AtomicU64 = AtomicU64::new(0);
pub fn configured() -> bool { ACTIVE.load(Ordering::Acquire) }
pub fn init() {
    if core::arch::x86_64::__cpuid(1).edx & (1 << 16) == 0 { return; }
    // All THOS tables use slot 0 for RAM and slot 2 for ordinary MMIO;
    // preserve those and every other firmware slot. Slot 1 is reserved for FB.
    let old = unsafe { Msr::new(0x277).read() };
    VALUE.store((old & !(0xff << 8)) | (1 << 8), Ordering::Release);
    ACTIVE.store(true, Ordering::Release);
    apply();
    crate::kprintln!("THOS: PAT              framebuffer WC slot 1 configured");
}
pub fn apply() {
    if !configured() { return; }
    x86_64::instructions::interrupts::without_interrupts(|| unsafe {
        let cr0 = Cr0::read();
        let cr4 = Cr4::read();
        Cr4::write(cr4 & !Cr4Flags::PAGE_GLOBAL);
        Cr0::write((cr0 | Cr0Flags::CACHE_DISABLE) & !Cr0Flags::NOT_WRITE_THROUGH);
        core::arch::asm!("wbinvd", options(nostack));
        x86_64::instructions::tlb::flush_all();
        Msr::new(0x277).write(VALUE.load(Ordering::Acquire));
        assert_eq!(Msr::new(0x277).read(), VALUE.load(Ordering::Acquire), "PAT programming failed");
        core::arch::asm!("wbinvd", options(nostack));
        x86_64::instructions::tlb::flush_all();
        Cr0::write(cr0);
        Cr4::write(cr4);
    });
}
