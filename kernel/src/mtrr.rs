// SPDX-License-Identifier: GPL-2.0-or-later
//! Write-combining for the framebuffer through a variable MTRR (restored after an accidental delete; the other session's later edits to
//! the first version are lost: see the commit message).
//!
//! Rules (Intel SDM 11.11.8): all CPUs carry the same MTRRs, so the boot CPU chooses and programs the register (`prepare`) and every AP
//! repeats it (`apply`); interrupts off, caches disabled and flushed around the change. Only used when the range lies above all RAM and a
//! free variable register exists.

use core::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use x86_64::registers::control::{Cr0, Cr0Flags, Cr4, Cr4Flags};
use x86_64::registers::model_specific::Msr;

const MTRRCAP: u32 = 0xFE;
const DEF_TYPE: u32 = 0x2FF;
const PHYSBASE0: u32 = 0x200;
const WC: u64 = 1;

static ACTIVE: AtomicBool = AtomicBool::new(false);
static BASE: AtomicU64 = AtomicU64::new(0);
static MASK: AtomicU64 = AtomicU64::new(0);
static SLOT: AtomicUsize = AtomicUsize::new(0);

fn rd(msr: u32) -> u64 {
    unsafe { Msr::new(msr).read() }
}
fn wr(msr: u32, v: u64) {
    unsafe { Msr::new(msr).write(v) }
}

/// Has a write-combining MTRR been set up?
pub fn configured() -> bool {
    ACTIVE.load(Ordering::Acquire)
}

/// Choose the range and the register on the boot CPU and program it there. `ram_top`: first physical address above all RAM.
pub fn prepare(fb_phys: u64, fb_len: u64, ram: impl Iterator<Item = (u64, u64)>) {
    let ram_top = ram.map(|(b, l)| b + l).max().unwrap_or(0);
    if core::arch::x86_64::__cpuid(1).edx & (1 << 12) == 0 {
        crate::kprintln!("THOS: mtrr             not available on this CPU");
        return;
    }
    let cap = rd(MTRRCAP);
    let (vcnt, wc_ok) = ((cap & 0xFF) as usize, cap & (1 << 10) != 0);
    crate::kprintln!("THOS: mtrr             default type {:#x}, {} variable registers, framebuffer {:#x} (RAM ends at {:#x})", rd(DEF_TYPE) & 0xFFF, vcnt, fb_phys, ram_top);
    if !wc_ok || vcnt == 0 {
        crate::kprintln!("THOS: mtrr             no write-combining type or no variable registers");
        return;
    }
    if fb_phys < ram_top {
        crate::kprintln!("THOS: mtrr             framebuffer lies in the RAM range: left alone");
        return;
    }
    let size = fb_len.next_power_of_two().max(1 << 20);
    let base = fb_phys & !(size - 1);
    if base < ram_top {
        crate::kprintln!("THOS: mtrr             the aligned range {:#x} would reach into RAM: left alone", base);
        return;
    }
    let max_ext = core::arch::x86_64::__cpuid(0x8000_0000).eax;
    let bits = if max_ext >= 0x8000_0008 { (core::arch::x86_64::__cpuid(0x8000_0008).eax & 0xFF) as u64 } else { 36 };
    let phys_mask = (1u64 << bits.min(52)) - 1;
    let mut slot = None;
    for i in 0..vcnt {
        let (b, m) = (rd(PHYSBASE0 + 2 * i as u32), rd(PHYSBASE0 + 2 * i as u32 + 1));
        if m & (1 << 11) == 0 {
            if slot.is_none() {
                slot = Some(i);
            }
            continue;
        }
        crate::kprintln!("THOS: mtrr             range {}: base {:#x} type {} mask {:#x}", i, b & !0xFFF, b & 0xFF, m & !0xFFF);
        let (eb, em) = (b & !0xFFF & phys_mask, m & !0xFFF & phys_mask);
        if (base & em) == (eb & em) {
            crate::kprintln!("THOS: mtrr             an existing range already covers the framebuffer: left alone");
            return;
        }
    }
    let Some(slot) = slot else {
        crate::kprintln!("THOS: mtrr             no free variable register");
        return;
    };
    BASE.store(base | WC, Ordering::Release);
    MASK.store((!(size - 1) & phys_mask) | (1 << 11), Ordering::Release);
    SLOT.store(slot, Ordering::Release);
    ACTIVE.store(true, Ordering::Release);
    apply();
    crate::kprintln!("THOS: mtrr             framebuffer {:#x}+{} MiB is write-combining (variable register {})", base, size >> 20, slot);
}

/// Program the chosen register on this CPU (the boot CPU from `prepare`, every AP when it starts).
pub fn apply() {
    if !ACTIVE.load(Ordering::Acquire) {
        return;
    }
    let slot = SLOT.load(Ordering::Acquire) as u32;
    x86_64::instructions::interrupts::without_interrupts(|| unsafe {
        let cr4 = Cr4::read();
        Cr4::write(cr4 & !Cr4Flags::PAGE_GLOBAL);
        let cr0 = Cr0::read();
        Cr0::write((cr0 | Cr0Flags::CACHE_DISABLE) & !Cr0Flags::NOT_WRITE_THROUGH);
        core::arch::asm!("wbinvd", options(nostack));
        x86_64::instructions::tlb::flush_all();
        let def = rd(DEF_TYPE);
        wr(DEF_TYPE, def & !(1 << 11));
        wr(PHYSBASE0 + 2 * slot, BASE.load(Ordering::Acquire));
        wr(PHYSBASE0 + 2 * slot + 1, MASK.load(Ordering::Acquire));
        wr(DEF_TYPE, def | (1 << 11));
        core::arch::asm!("wbinvd", options(nostack));
        x86_64::instructions::tlb::flush_all();
        Cr0::write(cr0);
        Cr4::write(cr4);
    });
}
