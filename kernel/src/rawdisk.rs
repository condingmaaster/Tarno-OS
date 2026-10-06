// SPDX-License-Identifier: GPL-2.0-or-later
//! Raw block devices for userspace: `/dev/sda` (the SATA disk) and `/dev/sdb` (a USB mass-storage device),
//! plus their MBR partitions `/dev/sda1` ... `/dev/sda4`. They exist so the installer (and tools like `dd`,
//! `fdisk`, `mke2fs`) can lay THOS onto the internal disk when it was booted from a stick.
//!
//! Safety rules, all enforced here:
//!   * only root (uid 0) may read or write them;
//!   * the disk the running system booted from is read-only (writing under a mounted root would corrupt it).

use alloc::sync::Arc;
use spin::Mutex;

use crate::file::FileOps;

const SECTOR: u64 = 512;
const EACCES: i64 = -13;
const EIO: i64 = -5;
const EINVAL: i64 = -22;
const EROFS: i64 = -30;
const ENOSPC: i64 = -28;
const S_IFBLK: u32 = 0o060000;
/// Largest single transfer to the controller (sectors).
const CHUNK: usize = 64; // 32 KiB: the AHCI bounce buffer size

#[derive(Clone, Copy, PartialEq)]
enum Dev {
    Sata,
    Usb,
}

fn dev_sectors(d: Dev) -> u64 {
    match d {
        Dev::Sata => crate::ahci::capacity_sectors(),
        Dev::Usb => crate::EHCI.lock().as_ref().map_or(0, |e| e.msc_sectors()),
    }
}
fn dev_read(d: Dev, lba: u64, buf: &mut [u8]) -> Result<(), &'static str> {
    match d {
        Dev::Sata => crate::ahci::read(lba, buf),
        Dev::Usb => match crate::EHCI.lock().as_mut() {
            Some(e) => e.msc_read(lba, buf),
            None => Err("no USB storage"),
        },
    }
}
fn dev_write(d: Dev, lba: u64, buf: &[u8]) -> Result<(), &'static str> {
    match d {
        Dev::Sata => crate::ahci::write(lba, buf),
        Dev::Usb => match crate::EHCI.lock().as_mut() {
            Some(e) => e.msc_write(lba, buf),
            None => Err("no USB storage"),
        },
    }
}
/// Is this the disk the running system lives on?
fn is_boot_disk(d: Dev) -> bool {
    (d == Dev::Usb) == crate::blk::is_usb()
}

pub struct RawDisk {
    dev: Dev,
    base: u64,    // first sector of the view (0 for a whole disk)
    sectors: u64, // length of the view
    pos: Mutex<u64>,
}

fn is_root() -> bool {
    crate::sched::current().task().is_some_and(|t| t.uid == 0)
}

/// `/dev/sda`, `/dev/sdb`, `/dev/sda1` ... for an absolute device path; `None` if there is no such device.
pub fn open(path: &str) -> Option<Arc<dyn FileOps>> {
    let name = path.strip_prefix("/dev/sd")?;
    let mut ch = name.chars();
    let dev = match ch.next()? {
        'a' => Dev::Sata,
        'b' => Dev::Usb,
        _ => return None,
    };
    let total = dev_sectors(dev);
    if total == 0 {
        return None;
    }
    let rest = ch.as_str();
    if rest.is_empty() {
        return Some(Arc::new(RawDisk { dev, base: 0, sectors: total, pos: Mutex::new(0) }));
    }
    let n: usize = rest.parse().ok()?;
    if !(1..=4).contains(&n) {
        return None;
    }
    let mut s = [0u8; 512];
    dev_read(dev, 0, &mut s).ok()?;
    if s[510] != 0x55 || s[511] != 0xAA {
        return None;
    }
    let e = &s[0x1BE + (n - 1) * 16..0x1BE + n * 16];
    let start = u32::from_le_bytes([e[8], e[9], e[10], e[11]]) as u64;
    let count = u32::from_le_bytes([e[12], e[13], e[14], e[15]]) as u64;
    if e[4] == 0 || count == 0 || start + count > total {
        return None;
    }
    Some(Arc::new(RawDisk { dev, base: start, sectors: count, pos: Mutex::new(0) }))
}

impl RawDisk {
    fn len(&self) -> u64 {
        self.sectors * SECTOR
    }
    /// Transfer `len` bytes at byte offset `off`; `buf` is the user's data (read: filled, write: consumed).
    fn xfer(&self, off: u64, buf: &mut [u8], write: bool) -> i64 {
        let total = self.len();
        if off >= total {
            return if write && !buf.is_empty() { ENOSPC } else { 0 };
        }
        let n = buf.len().min((total - off) as usize);
        let mut done = 0usize;
        let mut bounce = alloc::vec![0u8; CHUNK * SECTOR as usize];
        while done < n {
            let pos = off + done as u64;
            let sec = pos / SECTOR;
            let within = (pos % SECTOR) as usize;
            let nsec = ((within + (n - done) + SECTOR as usize - 1) / SECTOR as usize).min(CHUNK);
            let bytes = (nsec * SECTOR as usize - within).min(n - done);
            let lba = self.base + sec;
            let b = &mut bounce[..nsec * SECTOR as usize];
            if write {
                if within != 0 || bytes != nsec * SECTOR as usize {
                    // partial sector(s): read-modify-write
                    if dev_read(self.dev, lba, b).is_err() {
                        return if done > 0 { done as i64 } else { EIO };
                    }
                }
                b[within..within + bytes].copy_from_slice(&buf[done..done + bytes]);
                if dev_write(self.dev, lba, b).is_err() {
                    return if done > 0 { done as i64 } else { EIO };
                }
            } else {
                if dev_read(self.dev, lba, b).is_err() {
                    return if done > 0 { done as i64 } else { EIO };
                }
                buf[done..done + bytes].copy_from_slice(&b[within..within + bytes]);
            }
            done += bytes;
        }
        done as i64
    }
}

impl FileOps for RawDisk {
    fn read(&self, buf: &mut [u8]) -> i64 {
        if !is_root() {
            return EACCES;
        }
        let mut p = self.pos.lock();
        let r = self.xfer(*p, buf, false);
        if r > 0 {
            *p += r as u64;
        }
        r
    }
    fn write(&self, buf: &[u8]) -> i64 {
        if !is_root() {
            return EACCES;
        }
        if is_boot_disk(self.dev) {
            return EROFS;
        }
        let mut p = self.pos.lock();
        let mut tmp = buf.to_vec();
        let r = self.xfer(*p, &mut tmp, true);
        if r > 0 {
            *p += r as u64;
        }
        r
    }
    fn seek(&self, offset: i64, whence: u32) -> i64 {
        let mut p = self.pos.lock();
        let base = match whence {
            0 => 0i64,
            1 => *p as i64,
            2 => self.len() as i64,
            _ => return EINVAL,
        };
        let np = base + offset;
        if np < 0 {
            return EINVAL;
        }
        *p = np as u64;
        np
    }
    fn stat(&self) -> (u32, u64) {
        (S_IFBLK | 0o660, self.len())
    }
    fn sync(&self) -> i64 {
        let r = match self.dev {
            Dev::Sata => crate::ahci::flush(),
            Dev::Usb => match crate::EHCI.lock().as_mut() {
                Some(e) => e.msc_flush(),
                None => Ok(()),
            },
        };
        if r.is_ok() { 0 } else { EIO }
    }
    fn is_tty(&self) -> bool {
        false
    }
    /// The block-device ioctls the e2fsprogs / util-linux tools ask for.
    fn tty_ioctl(&self, cmd: u64, arg: u64) -> Option<i64> {
        let put = |v: u64, n: usize| -> i64 {
            if !crate::usercopy::user_ok(arg, n, true) {
                return -14;
            }
            unsafe {
                if n == 8 { *(arg as *mut u64) = v } else { *(arg as *mut u32) = v as u32 }
            }
            0
        };
        Some(match cmd {
            0x8008_1272 => put(self.len(), 8),   // BLKGETSIZE64
            0x1260 => put(self.sectors, 8),      // BLKGETSIZE (long)
            0x1268 | 0x127b | 0x1278 | 0x1279 => put(512, 4), // BLKSSZGET, BLKPBSZGET, BLKIOMIN, BLKIOOPT
            0x1261 | 0x125f | 0x1262 => 0,       // BLKFLSBUF, BLKRRPART, BLKRASET
            _ => return None,
        })
    }
}
