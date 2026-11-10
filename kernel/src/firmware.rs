// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded boot-time reads of device firmware from the THOS root filesystem.
use alloc::vec::Vec;
use sha2::{Digest, Sha256};
pub fn check(name: &str, bytes: usize, sha256: &str) -> Result<(), &'static str> {
    let data = read(name, bytes)?;
    if data.len() != bytes || alloc::format!("{:x}", Sha256::digest(&data)) != sha256 { return Err("firmware checksum mismatch"); }
    Ok(())
}
pub fn read(name: &str, limit: usize) -> Result<Vec<u8>, &'static str> {
    let fs = crate::ext2::open().map_err(|_| "firmware filesystem unavailable")?;
    let path = alloc::format!("/lib/firmware/{name}");
    let ino = fs.path_lookup(&path).ok_or("firmware file missing")?;
    let inode = fs.read_inode(ino);
    if inode.mode & 0xf000 != 0x8000 || inode.size == 0 || inode.size > limit as u64 { return Err("invalid firmware file size or type"); }
    let data = fs.read_file(&inode);
    if data.len() as u64 != inode.size { return Err("firmware file truncated"); }
    Ok(data)
}
