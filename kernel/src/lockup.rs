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
    let kernel = cs & 3 == 0 && rip >= 0xffff_8000_0000_0000;
    let idle = crate::sched::idle_addr();
    if !kernel || (rip >= idle && rip < idle + 0x80) {
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
