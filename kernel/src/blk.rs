// SPDX-License-Identifier: GPL-2.0-or-later
//! The block device the root filesystem lives on: the SATA disk (AHCI) or, when the machine booted from a
//! stick, a USB mass-storage device behind an EHCI controller. Everything above (MBR, ext2) talks to this.

use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

pub const SECTOR: usize = crate::ahci::SECTOR;

static USB: AtomicBool = AtomicBool::new(false);
/// The root file system as a module Limine loaded into memory (the stick's boot partition holds it): no USB driver needed to reach it.
static RAM: AtomicBool = AtomicBool::new(false);
static RAM_ADDR: AtomicU64 = AtomicU64::new(0);
static RAM_LEN: AtomicU64 = AtomicU64::new(0);

/// Switch the root device to a memory image at virtual address `addr` of `len` bytes (writes go to memory and are lost at power-off).
pub fn use_ram(addr: u64, len: u64) {
    RAM_ADDR.store(addr, Ordering::Release);
    RAM_LEN.store(len, Ordering::Release);
    RAM.store(true, Ordering::Release);
}

pub fn is_ram() -> bool {
    RAM.load(Ordering::Acquire)
}

fn ram_span(lba: u64, len: usize) -> Result<*mut u8, &'static str> {
    let (a, l) = (RAM_ADDR.load(Ordering::Acquire), RAM_LEN.load(Ordering::Acquire));
    let off = lba.checked_mul(SECTOR as u64).ok_or("ram: lba overflow")?;
    if off + len as u64 > l {
        return Err("ram: read past the end of the image");
    }
    Ok((a + off) as *mut u8)
}

/// Switch the root device to the USB mass-storage device found by `ehci::init`.
pub fn use_usb() {
    USB.store(true, Ordering::Release);
}

pub fn is_usb() -> bool {
    USB.load(Ordering::Acquire)
}

fn usb<R>(f: impl FnOnce(&mut crate::ehci::Ehci) -> Result<R, &'static str>) -> Result<R, &'static str> {
    match crate::EHCI.lock().as_mut() {
        Some(e) => f(e),
        None => Err("USB storage not initialised"),
    }
}

pub fn read(lba: u64, buf: &mut [u8]) -> Result<(), &'static str> {
    if is_ram() {
        let p = ram_span(lba, buf.len())?;
        unsafe { core::ptr::copy_nonoverlapping(p, buf.as_mut_ptr(), buf.len()) };
        return Ok(());
    }
    if is_usb() { usb(|e| e.msc_read(lba, buf)) } else { crate::ahci::read(lba, buf) }
}

pub fn write(lba: u64, buf: &[u8]) -> Result<(), &'static str> {
    if is_ram() {
        let p = ram_span(lba, buf.len())?;
        unsafe { core::ptr::copy_nonoverlapping(buf.as_ptr(), p, buf.len()) };
        return Ok(());
    }
    if is_usb() { usb(|e| e.msc_write(lba, buf)) } else { crate::ahci::write(lba, buf) }
}

pub fn write_relaxed(lba: u64, buf: &[u8]) -> Result<(), &'static str> {
    if is_ram() {
        return write(lba, buf);
    }
    if is_usb() { usb(|e| e.msc_write(lba, buf)) } else { crate::ahci::write_relaxed(lba, buf) }
}

pub fn flush() -> Result<(), &'static str> {
    if is_ram() {
        return Ok(());
    }
    if is_usb() { usb(|e| e.msc_flush()) } else { crate::ahci::flush() }
}
