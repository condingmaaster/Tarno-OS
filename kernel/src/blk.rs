// SPDX-License-Identifier: GPL-2.0-or-later
//! The block device the root filesystem lives on: the SATA disk (AHCI) or, when the machine booted from a
//! stick, a USB mass-storage device behind an EHCI controller. Everything above (MBR, ext2) talks to this.

use core::sync::atomic::{AtomicBool, Ordering};

pub const SECTOR: usize = crate::ahci::SECTOR;

static USB: AtomicBool = AtomicBool::new(false);

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
    if is_usb() { usb(|e| e.msc_read(lba, buf)) } else { crate::ahci::read(lba, buf) }
}

pub fn write(lba: u64, buf: &[u8]) -> Result<(), &'static str> {
    if is_usb() { usb(|e| e.msc_write(lba, buf)) } else { crate::ahci::write(lba, buf) }
}

pub fn write_relaxed(lba: u64, buf: &[u8]) -> Result<(), &'static str> {
    if is_usb() { usb(|e| e.msc_write(lba, buf)) } else { crate::ahci::write_relaxed(lba, buf) }
}

pub fn flush() -> Result<(), &'static str> {
    if is_usb() { usb(|e| e.msc_flush()) } else { crate::ahci::flush() }
}
