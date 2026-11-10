// SPDX-License-Identifier: GPL-2.0-or-later
//! BCMA reset-wrapper access and bounded core reset sequencing.
//! Register/ordering reference: Linux drivers/bcma/core.c, bcma_regs.h.
//! Activation routines are prepared for bring-up; boot only reads a snapshot.
pub const IOCTL: u16 = 0x408;
pub const IOST: u16 = 0x500;
pub const RESET_CTL: u16 = 0x800;
pub const RESET_ST: u16 = 0x804;
const CLK: u32 = 1;
const FGC: u32 = 2;
const CORE_FLAGS: u32 = 0x3ffc;
const MAX_POLLS: usize = 30_000; // at most 300 ms with a 10 us backend delay
pub trait Registers {
    fn read(&mut self, offset: u16) -> Result<u32, &'static str>;
    fn write(&mut self, offset: u16, value: u32) -> Result<(), &'static str>;
    fn delay_us(&mut self, us: u32);
}
fn read(r: &mut impl Registers, offset: u16) -> Result<u32, &'static str> {
    let value = r.read(offset)?;
    if value == u32::MAX { return Err("BCMA wrapper is not responding"); }
    Ok(value)
}
#[derive(Clone, Copy, Debug)]
pub struct Snapshot { pub ioctl: u32, pub iost: u32, pub reset: u32, pub busy: u32 }
impl Snapshot {
    pub fn enabled(self) -> bool { self.ioctl & (CLK | FGC) == CLK && self.reset & 1 == 0 }
    pub fn state(self) -> &'static str {
        if self.reset & 1 != 0 { "reset" } else if self.enabled() { "running" } else { "clock-stopped" }
    }
    pub fn dma64(self) -> bool { self.iost & 0x1000 != 0 }
}
pub fn snapshot(r: &mut impl Registers) -> Result<Snapshot, &'static str> {
    Ok(Snapshot { ioctl: read(r, IOCTL)?, iost: read(r, IOST)?, reset: read(r, RESET_CTL)?, busy: read(r, RESET_ST)? })
}
fn wait(r: &mut impl Registers, offset: u16, mask: u32, expected: u32) -> Result<(), &'static str> {
    for attempt in 0..=MAX_POLLS {
        if read(r, offset)? & mask == expected { return Ok(()); }
        if attempt < MAX_POLLS { r.delay_us(10); }
    }
    Err("BCMA reset timed out")
}
fn write_flush(r: &mut impl Registers, offset: u16, value: u32) -> Result<(), &'static str> {
    r.write(offset, value)?;
    read(r, offset)?; // flush posted PCIe writes before delays or later writes
    Ok(())
}
pub fn disable(r: &mut impl Registers, flags: u32) -> Result<(), &'static str> {
    if flags & !CORE_FLAGS != 0 { return Err("unsupported BCMA core flags"); }
    if read(r, RESET_CTL)? & 1 != 0 { return Ok(()); }
    wait(r, RESET_ST, u32::MAX, 0)?; // a timeout must not proceed to reset
    write_flush(r, RESET_CTL, 1)?;
    wait(r, RESET_CTL, 1, 1)?;
    r.delay_us(1);
    write_flush(r, IOCTL, flags)?;
    r.delay_us(10);
    Ok(())
}
pub fn enable(r: &mut impl Registers, flags: u32) -> Result<(), &'static str> {
    disable(r, flags)?;
    write_flush(r, IOCTL, CLK | FGC | flags)?;
    write_flush(r, RESET_CTL, 0)?;
    wait(r, RESET_CTL, 1, 0)?;
    r.delay_us(1);
    write_flush(r, IOCTL, CLK | flags)?;
    r.delay_us(1);
    if !snapshot(r)?.enabled() { return Err("BCMA clock did not enable"); }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[derive(Default)]
    struct Fake { ioctl: u32, reset: u32, busy_reads: usize, stuck: bool, unreadable: bool, ignore_reset: bool, events: Vec<(char, u16, u32)>, delayed: u32 }
    impl Registers for Fake {
        fn read(&mut self, o: u16) -> Result<u32, &'static str> {
            self.events.push(('r', o, 0));
            if self.unreadable { return Ok(u32::MAX); }
            Ok(match o { IOCTL => self.ioctl, IOST => 0x1000, RESET_CTL => self.reset, RESET_ST => {
                if self.stuck { 1 } else if self.busy_reads != 0 { self.busy_reads -= 1; 1 } else { 0 }
            }, _ => panic!("unexpected register") })
        }
        fn write(&mut self, o: u16, v: u32) -> Result<(), &'static str> {
            self.events.push(('w', o, v));
            match o { IOCTL => self.ioctl = v, RESET_CTL => if !self.ignore_reset { self.reset = v; }, _ => panic!("unexpected write") }
            Ok(())
        }
        fn delay_us(&mut self, us: u32) { self.delayed += us; self.events.push(('d', 0, us)); }
    }
    #[test] fn diagnostics_are_read_only_and_distinguish_reset_and_clock() {
        let mut r = Fake { ioctl: CLK, ..Fake::default() };
        let s = snapshot(&mut r).unwrap(); assert!(s.enabled()); assert!(s.dma64());
        assert!(r.events.iter().all(|e| e.0 == 'r'));
        r.reset = 1; assert_eq!(snapshot(&mut r).unwrap().state(), "reset");
        r.reset = 0; r.ioctl = CLK | FGC; assert_eq!(snapshot(&mut r).unwrap().state(), "clock-stopped");
    }
    #[test] fn enables_in_order_with_readback_after_every_write() {
        let mut r = Fake { ioctl: CLK, busy_reads: 2, ..Fake::default() };
        enable(&mut r, 8).unwrap();
        let writes: Vec<_> = r.events.iter().filter(|e| e.0 == 'w').copied().collect();
        assert_eq!(writes, [('w', RESET_CTL, 1), ('w', IOCTL, 8), ('w', IOCTL, 11), ('w', RESET_CTL, 0), ('w', IOCTL, 9)]);
        for (i, e) in r.events.iter().enumerate().filter(|(_, e)| e.0 == 'w') { assert_eq!(r.events[i + 1], ('r', e.1, 0)); }
        assert!(snapshot(&mut r).unwrap().enabled()); assert_eq!(r.delayed, 33);
    }
    #[test] fn busy_timeout_leaves_hardware_untouched() {
        let mut r = Fake { stuck: true, ..Fake::default() };
        assert_eq!(enable(&mut r, 0), Err("BCMA reset timed out"));
        assert!(!r.events.iter().any(|e| e.0 == 'w')); assert_eq!(r.delayed, 300_000);
    }
    #[test] fn absent_device_and_bad_flags_never_write() {
        let mut r = Fake { unreadable: true, ..Fake::default() };
        assert!(enable(&mut r, 0).is_err()); assert!(snapshot(&mut r).is_err());
        r.unreadable = false; assert!(enable(&mut r, CLK).is_err());
        assert!(!r.events.iter().any(|e| e.0 == 'w'));
    }
    #[test] fn reset_readback_timeout_does_not_enable_clock() {
        let mut r = Fake { ignore_reset: true, ..Fake::default() };
        assert!(enable(&mut r, 0).is_err());
        assert_eq!(r.events.iter().filter(|e| e.0 == 'w').count(), 1);
        assert_eq!(r.ioctl, 0);
    }
    #[test] fn already_in_reset_skips_busy_wait() {
        let mut r = Fake { reset: 1, stuck: true, ..Fake::default() };
        enable(&mut r, 0).unwrap(); assert_eq!(r.delayed, 2);
    }
}
