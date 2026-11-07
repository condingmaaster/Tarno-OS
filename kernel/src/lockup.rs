// SPDX-License-Identifier: GPL-2.0-or-later
//! A lock-up detector. Every timer tick each CPU reports the kernel address it was interrupted at. A CPU that sits in the same 256 bytes of
//! kernel code for three seconds, outside the idle loop, is stuck in a spin loop (a deadlocked lock, say): the detector writes where, and
//! where the other CPUs are, to the serial port and the screen without taking any lock, so a hang on real hardware leaves a note that a
//! photo of the screen carries back (find the address with `nm target/.../thos-kernel`).

use core::fmt::Write;
use core::sync::atomic::{AtomicU32, AtomicU64, Ordering};

const N: usize = 8;
const STUCK_TICKS: u32 = 3000; // ~3 s at the 1 kHz tick
static WIN: [AtomicU64; N] = [const { AtomicU64::new(0) }; N];
static CNT: [AtomicU32; N] = [const { AtomicU32::new(0) }; N];
static RIP: [AtomicU64; N] = [const { AtomicU64::new(0) }; N];
static SEEN: [AtomicU64; N] = [const { AtomicU64::new(0) }; N];       // timer ticks each CPU has taken
static CHK: [AtomicU64; N] = [const { AtomicU64::new(0) }; N];        // ... as last seen by a checking CPU
static STALE: [AtomicU32; N] = [const { AtomicU32::new(0) }; N];      // checks in a row that saw no new tick
static NMI_ASKED: [AtomicU32; N] = [const { AtomicU32::new(0) }; N];
static REPORTED: [AtomicU32; N] = [const { AtomicU32::new(0) }; N];

struct Buf([u8; 400], usize);
impl Write for Buf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &b in s.as_bytes() {
            if self.1 < self.0.len() {
                self.0[self.1] = b;
                self.1 += 1;
            }
        }
        Ok(())
    }
}

/// Called from the timer interrupt of every CPU with the interrupted instruction pointer and code segment.
pub fn sample(cpu: usize, rip: u64, cs: u64) {
    if cpu >= N {
        return;
    }
    RIP[cpu].store(rip, Ordering::Relaxed);
    // A CPU that stopped taking timer ticks has interrupts off (stuck in a syscall or an interrupt handler): another CPU notices and sends
    // it an NMI, whose handler reports where it is (`nmi`).
    let mine = SEEN[cpu].fetch_add(1, Ordering::Relaxed) + 1;
    if mine % 1000 == 0 {
        for c in 0..N {
            let seen = SEEN[c].load(Ordering::Relaxed);
            if c == cpu || seen == 0 {
                continue;
            }
            if seen != CHK[c].swap(seen, Ordering::Relaxed) {
                STALE[c].store(0, Ordering::Relaxed);
            } else if STALE[c].fetch_add(1, Ordering::Relaxed) + 1 == 3 && NMI_ASKED[c].fetch_add(1, Ordering::Relaxed) == 0 {
                crate::apic::send_nmi(crate::smp::lapic_id_of(c));
            }
        }
    }
    let kernel = cs & 3 == 0 && rip >= 0xffff_8000_0000_0000;
    let idle = crate::sched::idle_addr();
    // a CPU asleep in `hlt` is interrupted right after the instruction: the byte before rip is 0xF4 (idle loops exist in several places)
    let asleep = kernel && unsafe { *((rip - 1) as *const u8) } == 0xF4;
    if !kernel || asleep || (rip >= idle && rip < idle + 0x80) {
        CNT[cpu].store(0, Ordering::Relaxed);
        return;
    }
    let win = rip & !0xFF;
    if WIN[cpu].swap(win, Ordering::Relaxed) != win {
        CNT[cpu].store(0, Ordering::Relaxed);
        return;
    }
    let n = CNT[cpu].fetch_add(1, Ordering::Relaxed) + 1;
    if n == STUCK_TICKS && REPORTED[cpu].fetch_add(1, Ordering::Relaxed) == 0 {
        let mut b = Buf([0; 400], 0);
        let _ = write!(b, "\r\nTHOS: LOCKUP? cpu {} has been at kernel address {:#x} for 3 s (not idle). Others:", cpu, rip);
        for c in 0..N {
            let r = RIP[c].load(Ordering::Relaxed);
            if c != cpu && r != 0 {
                let _ = write!(b, " cpu{}={:#x}", c, r);
            }
        }
        let _ = write!(b, "\r\n");
        crate::serial::emergency(&b.0[..b.1]);
        crate::fbcon::emergency(&b.0[..b.1]);
    }
}

/// The NMI handler: if a CPU asked for it (see `sample`), report this CPU's address; returns whether the NMI was ours.
pub fn nmi(cpu: usize, rip: u64) -> bool {
    if cpu >= N || NMI_ASKED[cpu].load(Ordering::Relaxed) == 0 {
        return false;
    }
    let mut b = Buf([0; 400], 0);
    let _ = write!(b, "\r\nTHOS: LOCKUP? cpu {} takes no timer ticks (interrupts off) and was at kernel address {:#x}\r\n", cpu, rip);
    crate::serial::emergency(&b.0[..b.1]);
    crate::fbcon::emergency(&b.0[..b.1]);
    true
}
