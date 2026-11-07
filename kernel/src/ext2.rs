// SPDX-License-Identifier: GPL-2.0-or-later
//! Phase 2 — ext2 (read + basic write).
//!
//! Mount the SATA disk, walk a path from the root inode, read a file's data
//! blocks (12 direct + single + double indirect). Write side: block / inode
//! bitmap allocators, `write_path` (create-or-overwrite a regular file),
//! `mkdir_path`, `unlink_path`, `rmdir_path`; the backup superblock + group
//! descriptors are re-synced from the primary after each change (`sparse_super`
//! honoured) so `e2fsck` stays clean on a multi-group filesystem. Still missing:
//! htree dirs, growing a directory past 12 direct blocks, 64-bit sizes,
//! journalling, timestamps, hard links.

use alloc::vec;
use alloc::collections::BTreeMap;
use alloc::vec::Vec;
use core::sync::atomic::{AtomicU64, Ordering};

use crate::ahci::{self, SECTOR};

const SB_OFFSET: u64 = 1024;

/// Disk LBA where the ext2 filesystem starts: 0 for a bare-image disk (the QEMU
/// test disk), the partition's first sector on a real MBR disk.
static PART_LBA: AtomicU64 = AtomicU64::new(0);
const EXT2_MAGIC: u16 = 0xEF53;
pub const ROOT_INO: u32 = 2;

/// Serialises every operation that changes the filesystem (allocation bitmaps, directories,
/// inodes are read-modify-write with no other protection). Threads and several CPUs can now
/// write at once; without this two of them could be handed the same free block.
static FS_WRITE: crate::io_mutex::IoMutex<u64> = crate::io_mutex::IoMutex::new(0);
fn write_lock()->crate::io_mutex::IoMutexGuard<'static,u64> {
    let mut guard=FS_WRITE.lock();*guard=guard.wrapping_add(1);guard
}

pub struct Ext2 {
    block_size: u32,
    inode_size: u32,
    inodes_per_group: u32,
    first_data_block: u32,
    blocks_per_group: u32,
    block_count: u32,
    /// `s_feature_incompat & FILETYPE` — dir entries carry a file-type byte.
    filetype: bool,
    /// `s_feature_ro_compat & SPARSE_SUPER` — backup SB/GDT only in groups
    /// 0, 1, and powers of 3/5/7, not every group.
    sparse_super: bool,
}

pub struct Inode {
    pub mode: u16,
    pub size: u64,
    pub block: [u32; 15],
    /// `i_atime` / `i_ctime` / `i_mtime`, seconds since the epoch.
    pub atime: u32,
    pub ctime: u32,
    pub mtime: u32,
    /// `i_uid`/`i_gid` — the classic 16-bit ext2 fields (not the Linux
    /// high-16-bits-in-`i_osd2` extension; THOS's own uid space is small
    /// enough that this doesn't matter yet). Every file THOS itself creates
    /// today (registry hives, the credential store, the integrity baseline,
    /// the write-path test fixtures) goes through `write_path`/`mkdir_path`
    /// without an explicit owner, so it's `0` — conceptually the system
    /// account, matching real Unix's read of an unset owner, and consistent
    /// with THOS having no interactive root login to actually confuse this
    /// with (see `cred.rs`).
    pub uid: u32,
    pub gid: u32,
}

impl Inode {
    /// Search/execute permission. Root still needs at least one execute bit on regular files.
    pub fn execute_ok(&self,uid:u32,gid:u32)->bool {
        if uid==0{return self.mode&0xf000==0x4000||self.mode&0o111!=0;}
        let bit=if uid==self.uid{0o100}else if gid==self.gid{0o010}else{0o001};
        self.mode&bit!=0
    }
    /// The DAC permission check every file open goes through: real Unix
    /// owner/group/other bits, three tiers. THOS's identity model has no
    /// supplementary groups (yet) — every task carries exactly one primary
    /// gid, the "user private group" scheme (gid == uid, same convention
    /// `cred::save` already uses naming `/home/<name>`'s owner) — so the
    /// group tier here is "does the caller's single primary gid match the
    /// file's gid", not a membership-list lookup. uid `0` (the system
    /// account) always passes, matching real Unix root semantics —
    /// consistent with THOS having no interactive root login to actually
    /// confuse this with (`cred.rs`).
    pub fn access_ok(&self, uid: u32, gid: u32, want_write: bool) -> bool {
        if uid == 0 {
            return true;
        }
        let perm = self.mode & 0o777;
        let bits = if uid == self.uid {
            (perm >> 6) & 0o7
        } else if gid == self.gid {
            (perm >> 3) & 0o7
        } else {
            perm & 0o7
        };
        let need = if want_write { 0o2 } else { 0o4 };
        bits & need == need
    }
}

/// A write-through cache of 512-byte sectors of the root filesystem. Without it every inode read,
/// bitmap read and directory lookup was a drive command (milliseconds each; creating one file took
/// ~85 of them). Reads fill it, writes update it (and still go to the drive), so it never holds data the
/// disk does not.
use crate::sector_cache::SectorCache;

static CACHE: spin::Mutex<SectorCache> = spin::Mutex::new(SectorCache::new());

/// Forget every cached sector and the partition offset: the root device changed (the cache is keyed by LBA only, so sectors of the previous
/// device, a SATA disk that turned out not to hold THOSROOT, would otherwise answer for the new one).
pub fn reset_cache() {
    *CACHE.lock() = SectorCache::new();
    PART_LBA.store(0, Ordering::Relaxed);
}

/// Read `sectors` sectors starting at ext2-relative `start_lba` through the cache.
fn read_sectors(start_lba: u64, sectors: usize) -> Vec<u8> {
    let part = PART_LBA.load(Ordering::Relaxed);
    let mut raw = vec![0u8; sectors * SECTOR];
    let mut missing: Vec<usize> = Vec::new();
    {
        let mut c = CACHE.lock();
        for i in 0..sectors {
            if !c.get(part + start_lba + i as u64, &mut raw[i * SECTOR..(i + 1) * SECTOR]) {
                missing.push(i);
            }
        }
    }
    // read the missing sectors in runs of consecutive ones (up to 64 per drive command)
    let mut k = 0;
    while k < missing.len() {
        let first = missing[k];
        let mut n = 1;
        while k + n < missing.len() && missing[k + n] == first + n && n < 64 {
            n += 1;
        }
        crate::blk::read(part + start_lba + first as u64, &mut raw[first * SECTOR..(first + n) * SECTOR]).expect("ext2 disk read");
        let mut c = CACHE.lock();
        for j in 0..n {
            c.put(part + start_lba + (first + j) as u64, &raw[(first + j) * SECTOR..(first + j + 1) * SECTOR], false);
        }
        k += n;
    }
    raw
}

/// Read an arbitrary byte range off the disk (sector-granular under the hood).
fn disk_range(off: u64, len: usize) -> Vec<u8> {
    let start_lba = off / SECTOR as u64;
    let end_lba = (off + len as u64 + SECTOR as u64 - 1) / SECTOR as u64;
    let raw = read_sectors(start_lba, (end_lba - start_lba) as usize);
    let skip = (off - start_lba * SECTOR as u64) as usize;
    raw[skip..skip + len].to_vec()
}

fn le32(b: &[u8]) -> u32 {
    u32::from_le_bytes(b[..4].try_into().unwrap())
}
fn le16(b: &[u8]) -> u16 {
    u16::from_le_bytes(b[..2].try_into().unwrap())
}

/// True when the root FS was found inside an MBR partition (a real disk), not a
/// bare QEMU test image. Fixed-LBA scratch tests must not run on such a disk.
pub fn on_partition() -> bool {
    PART_LBA.load(Ordering::Relaxed) != 0
}

/// A filesystem THOS may use: an ext2 superblock whose volume label is `THOSROOT`. Never mount (and write to) anything else —
/// on a real machine the first Linux partition on the disk is somebody's operating system.
fn is_ours(sb: &[u8]) -> bool {
    le16(&sb[56..]) == EXT2_MAGIC && &sb[120..128] == b"THOSROOT"
}

pub fn open() -> Result<Ext2, &'static str> {
    let mut sb = disk_range(SB_OFFSET, 1024);
    if !is_ours(&sb) {
        // Not a bare-image disk: look for a Linux partition in the MBR.
        let found = crate::mbr::entries()
            .into_iter()
            .filter(|e| e.ptype == crate::mbr::TYPE_LINUX)
            .find(|e| {
                PART_LBA.store(e.start_lba, Ordering::Relaxed);
                is_ours(&disk_range(SB_OFFSET, 1024))
            });
        match found {
            Some(e) => {
                crate::kprintln!("THOS: ext2 part        root FS in MBR partition at LBA {} ({} MiB)", e.start_lba, e.sectors / 2048);
                sb = disk_range(SB_OFFSET, 1024);
            }
            None => {
                PART_LBA.store(0, Ordering::Relaxed);
                return Err("ext2 magic not found");
            }
        }
    }
    let block_size = 1024u32 << le32(&sb[24..]);
    let rev = le32(&sb[76..]);
    let inode_size = if rev >= 1 { le16(&sb[88..]) as u32 } else { 128 };
    Ok(Ext2 {
        block_size,
        inode_size,
        inodes_per_group: le32(&sb[40..]),
        first_data_block: le32(&sb[20..]),
        blocks_per_group: le32(&sb[32..]),
        block_count: le32(&sb[4..]),
        filetype: le32(&sb[96..]) & 0x0002 != 0,      // FEATURE_INCOMPAT_FILETYPE
        sparse_super: le32(&sb[100..]) & 0x0001 != 0, // FEATURE_RO_COMPAT_SPARSE_SUPER
    })
}

/// Write an arbitrary byte range to the disk (sector-granular RMW under the hood; only the first and
/// last sector can be partial). Written through the cache without a per-write drive flush — the caller
/// ends its operation with `sync_backups`, which flushes once.
fn disk_write(off: u64, data: &[u8]) {
    if data.is_empty() {
        return;
    }
    let start_lba = off / SECTOR as u64;
    let end_lba = (off + data.len() as u64 + SECTOR as u64 - 1) / SECTOR as u64;
    let sectors = (end_lba - start_lba) as usize;
    let part = PART_LBA.load(Ordering::Relaxed);
    let skip = (off - start_lba * SECTOR as u64) as usize;
    let mut raw = vec![0u8; sectors * SECTOR];
    // sectors the new data only partly covers keep their other bytes
    if skip != 0 || (skip + data.len()) % SECTOR != 0 || sectors == 1 && data.len() < SECTOR {
        let first = read_sectors(start_lba, 1);
        raw[..SECTOR].copy_from_slice(&first);
        if sectors > 1 {
            let last = read_sectors(end_lba - 1, 1);
            raw[(sectors - 1) * SECTOR..].copy_from_slice(&last);
        }
    }
    raw[skip..skip + data.len()].copy_from_slice(data);
    let mut done = 0;
    while done < sectors {
        let n = (sectors - done).min(64);
        crate::blk::write_relaxed(part + start_lba + done as u64, &raw[done * SECTOR..(done + n) * SECTOR]).expect("ext2 write");
        done += n;
    }
    let mut c = CACHE.lock();
    for i in 0..sectors {
        c.put(part + start_lba + i as u64, &raw[i * SECTOR..(i + 1) * SECTOR], true);
    }
}

/// Make everything durable: refresh pending backup superblocks and flush the drive cache.
pub fn sync_all() {
    if let Ok(fs) = open() {
        fs.flush_backups();
    }
    let _ = crate::blk::flush();
}

static BACKUPS_DIRTY: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);
static LAST_BACKUP_NS: core::sync::atomic::AtomicU64 = core::sync::atomic::AtomicU64::new(0);

fn round4(n: usize) -> usize {
    (n + 3) & !3
}

/// `("/a/b/c") -> ("/a/b", "c")`, `("/c") -> ("/", "c")`.
fn split_parent(path: &str) -> Option<(&str, &str)> {
    let path = path.trim_end_matches('/');
    let idx = path.rfind('/')?;
    let name = &path[idx + 1..];
    if name.is_empty() {
        return None;
    }
    Some((if idx == 0 { "/" } else { &path[..idx] }, name))
}

/// Patch the managed fields of a raw inode buffer in place.
#[allow(clippy::too_many_arguments)]
fn set_inode(
    raw: &mut [u8],
    mode: u16,
    uid: u32,
    gid: u32,
    size: u64,
    links: u16,
    blocks512: u32,
    block: &[u32; 15],
) {
    raw[0..2].copy_from_slice(&mode.to_le_bytes());
    raw[2..4].copy_from_slice(&(uid as u16).to_le_bytes());
    raw[4..8].copy_from_slice(&(size as u32).to_le_bytes());
    raw[24..26].copy_from_slice(&(gid as u16).to_le_bytes());
    raw[26..28].copy_from_slice(&links.to_le_bytes());
    raw[28..32].copy_from_slice(&blocks512.to_le_bytes()); // i_blocks (512-byte units)
    // Timestamps: every inode write through here is a create or a content rewrite.
    let now = (crate::timer::unix_secs() as u32).to_le_bytes();
    raw[8..12].copy_from_slice(&now); // i_atime
    raw[12..16].copy_from_slice(&now); // i_ctime
    raw[16..20].copy_from_slice(&now); // i_mtime
    for (i, b) in block.iter().enumerate() {
        raw[40 + i * 4..44 + i * 4].copy_from_slice(&b.to_le_bytes());
    }
}

impl Ext2 {
    pub fn max_write_size(&self)->u64 {2*1024*1024*self.block_size as u64}
    fn block(&self, n: u32) -> Vec<u8> {
        disk_range(n as u64 * self.block_size as u64, self.block_size as usize)
    }

    pub fn read_inode(&self, ino: u32) -> Inode {
        let group = (ino - 1) / self.inodes_per_group;
        let idx = (ino - 1) % self.inodes_per_group;

        let bgd_off = (self.first_data_block + 1) as u64 * self.block_size as u64 + group as u64 * 32;
        let bgd = disk_range(bgd_off, 32);
        let inode_table = le32(&bgd[8..]); // bg_inode_table

        let off = inode_table as u64 * self.block_size as u64 + idx as u64 * self.inode_size as u64;
        let raw = disk_range(off, self.inode_size as usize);

        let mut block = [0u32; 15];
        for (i, b) in block.iter_mut().enumerate() {
            *b = le32(&raw[40 + i * 4..]);
        }
        Inode {
            mode: le16(&raw[0..]),
            size: le32(&raw[4..]) as u64,
            block,
            uid: le16(&raw[2..]) as u32,
            gid: le16(&raw[24..]) as u32,
            atime: le32(&raw[8..]),
            ctime: le32(&raw[12..]),
            mtime: le32(&raw[16..]),
        }
    }

    /// Every data-block number of `inode` in file order, capped at the file's
    /// block count. `0` = a sparse hole.
    fn block_map(&self, inode: &Inode) -> Vec<u32> {
        let bs = self.block_size as usize;
        let per = bs / 4;
        let nblocks = (inode.size as usize).div_ceil(bs).max(1);
        let mut out: Vec<u32> = Vec::with_capacity(nblocks);

        out.extend_from_slice(&inode.block[..12.min(nblocks)]);

        for depth in 1..=3 {
            crate::ext2_blocks::append_indirect(
                inode.block[11 + depth], depth as u8, per, nblocks, &mut out, &|bn| self.block(bn),
            );
        }
        out
    }

    pub fn read_file(&self, inode: &Inode) -> Vec<u8> {
        let total = inode.size as usize;
        let bs = self.block_size as usize;
        let blocks = self.block_map(inode);

        // Emit consecutive runs of block numbers in one disk read each.
        // Whole blocks are appended (the tail is cut off afterwards): reserve them up front, or the
        // last partial block makes the Vec double its capacity (a 7 MiB file asked for 14 MiB).
        let mut out = Vec::with_capacity(blocks.len() * bs);
        let mut i = 0;
        while i < blocks.len() {
            if blocks[i] == 0 {
                let j = blocks[i..].iter().take_while(|&&b| b == 0).count() + i;
                out.resize(out.len() + (j - i) * bs, 0);
                i = j;
            } else {
                let mut j = i + 1;
                while j < blocks.len() && blocks[j] == blocks[j - 1] + 1 {
                    j += 1;
                }
                out.extend_from_slice(&disk_range(blocks[i] as u64 * bs as u64, (j - i) * bs));
                i = j;
            }
        }
        out.truncate(total);
        out
    }

    /// The block numbers of a file (0 = hole), for [`Ext2::read_at`].
    pub fn file_blocks(&self, inode: &Inode) -> Vec<u32> {
        self.block_map(inode)
    }

    /// Read up to `out.len()` bytes at byte offset `off` of a file of `size` bytes whose
    /// block map is `blocks` — only the blocks that are needed, in runs of consecutive disk
    /// blocks (at most 64 per disk read). Returns the number of bytes read (0 at EOF).
    pub fn read_at(&self, blocks: &[u32], size: u64, off: u64, out: &mut [u8]) -> usize {
        if off >= size {
            return 0;
        }
        let n = out.len().min((size - off) as usize);
        let bs = self.block_size as u64;
        let mut done = 0usize;
        while done < n {
            let pos = off + done as u64;
            let bi = (pos / bs) as usize;
            let in_off = (pos % bs) as usize;
            let remaining = n - done;
            if bi >= blocks.len() || blocks[bi] == 0 {
                // a hole (or past the map): reads as zeros up to the end of this block
                let take = (bs as usize - in_off).min(remaining);
                out[done..done + take].fill(0);
                done += take;
                continue;
            }
            let mut j = bi + 1;
            while j < blocks.len()
                && j - bi < 64
                && blocks[j] == blocks[j - 1] + 1
                && (j - bi) * (bs as usize) < in_off + remaining
            {
                j += 1;
            }
            let take = ((j - bi) * bs as usize - in_off).min(remaining);
            let data = disk_range(blocks[bi] as u64 * bs + in_off as u64, take);
            out[done..done + take].copy_from_slice(&data);
            done += take;
        }
        n
    }

    fn lookup(&self, dir_ino: u32, name: &str) -> Option<u32> {
        let data = self.read_file(&self.read_inode(dir_ino));
        let mut off = 0;
        while off + 8 <= data.len() {
            let ino = le32(&data[off..]);
            let rec_len = le16(&data[off + 4..]) as usize;
            let name_len = data[off + 6] as usize;
            if rec_len == 0 {
                break;
            }
            if ino != 0 && off + 8 + name_len <= data.len() && &data[off + 8..off + 8 + name_len] == name.as_bytes() {
                return Some(ino);
            }
            off += rec_len;
        }
        None
    }

    /// Every live entry of a directory as `(inode, d_type, name)` with `d_type`
    /// already translated to the Linux `DT_*` namespace (`getdents64`). Falls
    /// back to the entry inode's `i_mode` when the on-disk filetype byte is 0.
    pub fn read_dir(&self, dir_ino: u32) -> Vec<(u32, u8, alloc::string::String)> {
        let data = self.read_file(&self.read_inode(dir_ino));
        let mut out = Vec::new();
        let mut off = 0;
        while off + 8 <= data.len() {
            let ino = le32(&data[off..]);
            let rec_len = le16(&data[off + 4..]) as usize;
            let name_len = data[off + 6] as usize;
            let ftype = data[off + 7];
            if rec_len == 0 {
                break;
            }
            if ino != 0 && off + 8 + name_len <= data.len() {
                if let Ok(name) = core::str::from_utf8(&data[off + 8..off + 8 + name_len]) {
                    let dt = match ftype {
                        1 => 8,  // reg
                        2 => 4,  // dir
                        3 => 2,  // chr
                        4 => 6,  // blk
                        5 => 1,  // fifo
                        6 => 12, // sock
                        7 => 10, // symlink
                        _ => (self.read_inode(ino).mode >> 12) as u8,
                    };
                    out.push((ino, dt, name.into()));
                }
            }
            off += rec_len;
        }
        out
    }

    /// Resolve `path` to an inode, following symbolic links everywhere (at most 8 hops).
    pub fn path_lookup(&self, path: &str) -> Option<u32> {
        self.walk(path, true)
    }

    /// Like [`Self::path_lookup`] but a symlink as the *last* component is returned itself (`lstat`).
    pub fn path_lookup_nofollow(&self, path: &str) -> Option<u32> {
        self.walk(path, false)
    }

    fn walk(&self, path: &str, follow_last: bool) -> Option<u32> {
        let mut stack: Vec<u32> = vec![ROOT_INO];
        let mut todo: Vec<alloc::string::String> =
            path.split('/').filter(|s| !s.is_empty()).rev().map(Into::into).collect();
        let mut hops = 0;
        while let Some(c) = todo.pop() {
            let cur = *stack.last()?;
            if c == "." {
                continue;
            }
            if c == ".." {
                if stack.len() > 1 {
                    stack.pop();
                }
                continue;
            }
            let ino = self.lookup(cur, &c)?;
            let node = self.read_inode(ino);
            let last = todo.is_empty();
            if node.mode & 0xF000 == 0xA000 && (!last || follow_last) {
                hops += 1;
                if hops > 8 {
                    return None; // ELOOP
                }
                let target = self.read_link(ino)?;
                if target.starts_with('/') {
                    stack.truncate(1);
                }
                for comp in target.split('/').filter(|s| !s.is_empty()).rev() {
                    todo.push(comp.into());
                }
                continue;
            }
            if !last && node.mode & 0xF000 != 0x4000 {
                return None; // ENOTDIR
            }
            stack.push(ino);
        }
        stack.last().copied()
    }

    /// The target text of symlink inode `ino` (fast symlinks keep it inside the inode).
    pub fn read_link(&self, ino: u32) -> Option<alloc::string::String> {
        let node = self.read_inode(ino);
        if node.mode & 0xF000 != 0xA000 {
            return None;
        }
        let bytes: Vec<u8> = if node.size < 60 {
            node.block.iter().flat_map(|w| w.to_le_bytes()).take(node.size as usize).collect()
        } else {
            self.read_file(&node).into_iter().take(node.size as usize).collect()
        };
        alloc::string::String::from_utf8(bytes).ok()
    }

    /// Follow a symlink in the *last* component of `path` until it names something else
    /// (what `open(O_CREAT)` needs so it writes to the target). Absolute result.
    pub fn resolve_final(&self, path: &str) -> alloc::string::String {
        let mut cur = alloc::string::String::from(path);
        for _ in 0..8 {
            let Some(ino) = self.path_lookup_nofollow(&cur) else { break };
            let Some(target) = self.read_link(ino) else { break };
            let joined = if target.starts_with('/') {
                target
            } else {
                match split_parent(&cur) {
                    Some((dir, _)) => alloc::format!("{}/{}", dir.trim_end_matches('/'), target),
                    None => target,
                }
            };
            let mut comps: Vec<&str> = Vec::new();
            for p in joined.split('/') {
                match p {
                    "" | "." => {}
                    ".." => {
                        comps.pop();
                    }
                    c => comps.push(c),
                }
            }
            cur = alloc::format!("/{}", comps.join("/"));
        }
        cur
    }

    /// `symlink(2)`: create `path` as a symbolic link to `target`.
    pub fn symlink_path(&self, target: &str, path: &str, uid: u32, gid: u32) -> Result<(), &'static str> {
        let _fs = write_lock();
        let (parent, name) = split_parent(path).ok_or("bad path")?;
        let parent_ino = self.path_lookup(parent).ok_or("parent dir missing")?;
        if self.lookup(parent_ino, name).is_some() {
            return Err("already exists");
        }
        if target.is_empty() || target.len() > 1000 {
            return Err("bad target");
        }
        let (block, blocks512) = if target.len() < 60 {
            let mut b = [0u32; 15];
            for (i, chunk) in target.as_bytes().chunks(4).enumerate() {
                let mut w = [0u8; 4];
                w[..chunk.len()].copy_from_slice(chunk);
                b[i] = u32::from_le_bytes(w);
            }
            (b, 0)
        } else {
            self.lay_out_data(target.as_bytes()).ok_or("no space")?
        };
        let ino = self.alloc_inode(false).ok_or("no free inode")?;
        self.patch_inode(ino, |raw| {
            set_inode(raw, 0o120_777, uid, gid, target.len() as u64, 1, blocks512, &block);
        });
        self.dir_insert_ft(parent_ino, name, ino, 7)?;
        self.sync_backups();
        Ok(())
    }

    pub fn read_path(&self, path: &str) -> Option<Vec<u8>> {
        let ino = self.path_lookup(path)?;
        Some(self.read_file(&self.read_inode(ino)))
    }

    // ---- write side ----

    fn write_block(&self, n: u32, data: &[u8]) {
        disk_write(n as u64 * self.block_size as u64, data);
    }

    fn group_count(&self) -> u32 {
        (self.block_count - self.first_data_block).div_ceil(self.blocks_per_group)
    }

    fn bgd_off(&self, g: u32) -> u64 {
        (self.first_data_block + 1) as u64 * self.block_size as u64 + g as u64 * 32
    }
    fn read_bgd(&self, g: u32) -> Vec<u8> {
        disk_range(self.bgd_off(g), 32)
    }

    /// Add `delta` to a little-endian u32 field at `SB_OFFSET + off`.
    fn sb_add32(&self, off: u64, delta: i64) {
        let cur = le32(&disk_range(SB_OFFSET + off, 4)) as i64;
        disk_write(SB_OFFSET + off, &((cur + delta) as u32).to_le_bytes());
    }
    /// Add `delta` to a little-endian u16 field at `bgd(g) + off`.
    fn bgd_add16(&self, g: u32, off: u64, delta: i64) {
        let o = self.bgd_off(g) + off;
        let cur = le16(&disk_range(o, 2)) as i64;
        disk_write(o, &((cur + delta) as u16).to_le_bytes());
    }

    /// First clear bit in `bitmap_blk` below `count`; sets it and writes back.
    fn take_bit(&self, bitmap_blk: u32, count: u32) -> Option<u32> {
        let mut bm = self.block(bitmap_blk);
        for i in 0..count as usize {
            if bm[i / 8] & (1 << (i % 8)) == 0 {
                bm[i / 8] |= 1 << (i % 8);
                self.write_block(bitmap_blk, &bm);
                return Some(i as u32);
            }
        }
        None
    }

    /// `(block size, blocks, free blocks, inodes, free inodes)` from the superblock, for `statfs`.
    pub fn stats(&self) -> (u32, u32, u32, u32, u32) {
        let sb = disk_range(SB_OFFSET, 1024);
        (self.block_size, le32(&sb[4..]), le32(&sb[12..]), le32(&sb[0..]), le32(&sb[16..]))
    }

    /// Start a batched block-allocation transaction (see [`BlockTx`]).
    fn tx(&self) -> BlockTx<'_> {
        BlockTx { fs: self, bitmaps: BTreeMap::new(), free: BTreeMap::new(), delta: BTreeMap::new(), next:BTreeMap::new() }
    }

    /// Allocate one zeroed data block; updates the group + superblock counts.
    fn alloc_block(&self) -> Option<u32> {
        let groups = self.group_count();
        for g in 0..groups {
            let bgd = self.read_bgd(g);
            if le16(&bgd[12..]) == 0 {
                continue;
            }
            let in_group = if g == groups - 1 {
                self.block_count - self.first_data_block - g * self.blocks_per_group
            } else {
                self.blocks_per_group
            };
            if let Some(i) = self.take_bit(le32(&bgd[0..]), in_group) {
                self.bgd_add16(g, 12, -1);
                self.sb_add32(12, -1);
                let bno = self.first_data_block + g * self.blocks_per_group + i;
                self.write_block(bno, &vec![0u8; self.block_size as usize]);
                return Some(bno);
            }
        }
        None
    }

    fn free_block(&self, bno: u32) {
        let rel = bno - self.first_data_block;
        let g = rel / self.blocks_per_group;
        let i = (rel % self.blocks_per_group) as usize;
        let bgd = self.read_bgd(g);
        let bmb = le32(&bgd[0..]);
        let mut bm = self.block(bmb);
        if bm[i / 8] & (1 << (i % 8)) != 0 {
            bm[i / 8] &= !(1 << (i % 8));
            self.write_block(bmb, &bm);
            self.bgd_add16(g, 12, 1);
            self.sb_add32(12, 1);
        }
    }

    /// Allocate one inode; zeroes its table slot, bumps `bg_used_dirs_count`
    /// when `is_dir`.
    fn alloc_inode(&self, is_dir: bool) -> Option<u32> {
        for g in 0..self.group_count() {
            let bgd = self.read_bgd(g);
            if le16(&bgd[14..]) == 0 {
                continue;
            }
            if let Some(i) = self.take_bit(le32(&bgd[4..]), self.inodes_per_group) {
                self.bgd_add16(g, 14, -1);
                self.sb_add32(16, -1);
                if is_dir {
                    self.bgd_add16(g, 16, 1);
                }
                let ino = g * self.inodes_per_group + i + 1;
                self.patch_inode(ino, |raw| raw.fill(0));
                return Some(ino);
            }
        }
        None
    }

    fn inode_off(&self, ino: u32) -> u64 {
        let group = (ino - 1) / self.inodes_per_group;
        let idx = (ino - 1) % self.inodes_per_group;
        let table = le32(&self.read_bgd(group)[8..]);
        table as u64 * self.block_size as u64 + idx as u64 * self.inode_size as u64
    }

    fn patch_inode(&self, ino: u32, f: impl FnOnce(&mut [u8])) {
        let off = self.inode_off(ino);
        let mut raw = disk_range(off, self.inode_size as usize);
        f(&mut raw);
        disk_write(off, &raw);
    }

    fn bump_links(&self, ino: u32, delta: i32) {
        self.patch_inode(ino, |raw| {
            let v = (le16(&raw[26..]) as i32 + delta) as u16;
            raw[26..28].copy_from_slice(&v.to_le_bytes());
        });
    }

    /// Free every data / indirect / double-indirect block an inode owns.
    fn free_all_blocks(&self, node: &Inode) {
        let mut tx=self.tx();
        for &b in &node.block[..12] {if b!=0 {tx.release(b);}}
        self.release_indirect(node,true,&mut tx);
        tx.commit();
    }

    fn release_indirect(&self,node:&Inode,include_data:bool,tx:&mut BlockTx<'_>) {
        for depth in 1..=3 {
            crate::ext2_blocks::visit_indirect(node.block[11+depth],depth as u8,include_data,&|b|self.block(b),&mut |b|tx.release(b));
        }
    }

    /// Build direct and all three indirect levels in a private allocation transaction.
    fn build_ptrs(&self, tx: &mut BlockTx<'_>, blocks: &[u32]) -> Option<([u32; 15], u32)> {
        let per=self.block_size as usize/4;
        let max=12+per+per*per+per*per*per;
        if blocks.len()>max {return None;}
        let mut ptrs=[0u32;15];let direct=blocks.len().min(12);
        ptrs[..direct].copy_from_slice(&blocks[..direct]);
        let mut next=direct;let mut meta=0;
        for depth in 1..=3 {
            ptrs[11+depth]=crate::ext2_blocks::build_indirect(blocks,depth as u8,per,&mut next,&mut |bytes| {
                let b=tx.alloc()?;self.write_block(b,&bytes);meta+=1;Some(b)
            })?;
        }
        Some((ptrs,meta))
    }

    pub fn metadata_blocks(&self,old:&Inode)->u32 {
        let mut count=0;for depth in 1..=3 {crate::ext2_blocks::visit_indirect(old.block[11+depth],depth as u8,false,&|b|self.block(b),&mut |_|count+=1);}count
    }

    /// Replace only metadata paths covering newly allocated data pointers.
    fn rewrite_ptrs(&self,tx:&mut BlockTx<'_>,old:&Inode,blocks:&[u32],old_meta:u32,first:usize,end:usize)->Option<([u32;15],u32,Vec<u32>)> {
        let per=self.block_size as usize/4;let mut ptrs=old.block;let direct=blocks.len().min(12);
        ptrs[..direct].copy_from_slice(&blocks[..direct]);let mut base=12;let mut added=0;let mut retired=Vec::new();
        for depth in 1..=3 {
            let cap=per.pow(depth);let lo=base.min(blocks.len());let hi=(base+cap).min(blocks.len());
            ptrs[11+depth as usize]=crate::ext2_blocks::patch_indirect(old.block[11+depth as usize],&blocks[lo..hi],depth as u8,per,base,first,end,&|b|self.block(b),&mut |bytes| {
                let b=tx.alloc()?;self.write_block(b,&bytes);added+=1;Some(b)
            },&mut retired)?;base+=cap;
        }
        let meta=(old_meta+added).checked_sub(retired.len() as u32)?;
        Some((ptrs,meta,retired))
    }

    /// Incrementally update an existing regular file to `data`, of which only the byte range
    /// `[lo, hi)` changed since the file on disk was last in sync: the blocks of that range are
    /// overwritten in place, new blocks are appended when the file grew, and only the (few)
    /// indirect blocks are rebuilt. Unchanged data blocks are not touched. `Err` when an
    /// incremental update is not possible (the file shrank, it is not a regular file, no
    /// space) — the caller then rewrites the whole file with [`Ext2::write_path`].
    ///
    /// Crash order, as in `write_path_owned`: new data and indirect blocks first, the inode
    /// (one atomic sector write) second, the old indirect blocks freed last.
    pub fn write_at(&self, ino: u32, data: &[u8], lo: usize, hi: usize) -> Result<(), &'static str> {
        let _fs = write_lock();
        if self.read_inode(ino).mode & 0xF000 != 0x8000 {
            return Err("not a regular file");
        }
        self.write_at_locked(ino, data, lo, hi)
    }

    /// [`Self::write_at`] for a caller that holds `FS_WRITE` (and has checked the inode type).
    fn write_at_locked(&self, ino: u32, data: &[u8], lo: usize, hi: usize) -> Result<(), &'static str> {
        let old = self.read_inode(ino);
        let bs = self.block_size as usize;
        let new_size = data.len();
        let old_size = old.size as usize;
        if new_size < old_size {
            return Err("file shrank");
        }
        let old_blocks: Vec<u32> = if old_size == 0 { Vec::new() } else { self.block_map(&old) };
        let needed = new_size.div_ceil(bs);
        let mut blocks = old_blocks.clone();
        blocks.resize(needed, 0);
        let (first, last) = if hi > lo { (lo / bs, (hi - 1) / bs) } else { (usize::MAX, 0) };
        let mut tx = self.tx();
        let mut write = alloc::vec![false; needed];
        for b in 0..needed {
            let dirty = b >= first && b <= last;
            let fresh = b >= old_blocks.len();
            if blocks[b] == 0 && (fresh || dirty) {
                blocks[b] = tx.alloc().ok_or("no space")?;
                write[b] = true;
            } else if dirty {
                write[b] = true;
            }
        }
        // Data: whole blocks from the in-memory buffer, consecutive blocks in one disk write.
        let mut b = 0;
        while b < needed {
            if !write[b] || blocks[b] == 0 {
                b += 1;
                continue;
            }
            let mut e = b + 1;
            while e < needed && write[e] && blocks[e] == blocks[e - 1] + 1 && (e - b) < 256 {
                e += 1;
            }
            let mut run = alloc::vec![0u8; (e - b) * bs];
            let from = b * bs;
            let to = (e * bs).min(new_size);
            run[..to - from].copy_from_slice(&data[from..to]);
            disk_write(blocks[b] as u64 * bs as u64, &run);
            b = e;
        }
        let (ptrs, meta) = self.build_ptrs(&mut tx, &blocks).ok_or("no space / file too large")?;
        tx.commit();
        let used = blocks.iter().filter(|&&x| x != 0).count() as u32 + meta;
        let blocks512 = used * (self.block_size / 512);
        let (mode, uid, gid) = (old.mode, old.uid, old.gid);
        self.patch_inode(ino, |raw| {
            let links = le16(&raw[26..]);
            set_inode(raw, mode, uid, gid, new_size as u64, links, blocks512, &ptrs);
        });
        // The old indirect blocks are garbage now (the data blocks live on).
        let mut gone = self.tx();
        self.release_indirect(&old,false,&mut gone);
        gone.commit();
        self.sync_backups();
        Ok(())
    }

    /// Allocate + fill data blocks for `data`. Returns `(block[15], i_blocks)`
    /// where `i_blocks` counts data + indirect blocks in 512-byte units.
    fn lay_out_data(&self, data: &[u8]) -> Option<([u32; 15], u32)> {
        let bs=self.block_size as usize;let needed=data.len().div_ceil(bs);
        let mut tx=self.tx();let mut blocks=Vec::with_capacity(needed);
        for _ in 0..needed {blocks.push(tx.alloc()?);}
        let (ptrs,meta)=self.build_ptrs(&mut tx,&blocks)?;
        for (i,&b) in blocks.iter().enumerate() {
            let start=i*bs;let mut bytes=vec![0u8;bs];
            let end=(start+bs).min(data.len());bytes[..end-start].copy_from_slice(&data[start..end]);
            self.write_block(b,&bytes);
        }
        tx.commit();Some((ptrs,(needed as u32+meta)*(self.block_size/512)))
    }

    /// Write a bounded caller range without keeping the whole file in memory.
    /// Allocate all required data/metadata before changing existing data.
    pub fn write_range(&self,ino:u32,off:u64,data:&[u8],cache:Option<(u64,[u32;15],&[u32],u32,u64)>)->Result<(u64,[u32;15],Vec<u32>,u32,u64),&'static str> {
        let _guard=write_lock();let old=self.read_inode(ino);
        if old.mode&0xF000!=0x8000{return Err("not a regular file");}
        let end=off.checked_add(data.len() as u64).ok_or("file too large")?;
        let bs=self.block_size as usize;let per=bs/4;
        if end>((12+per+per*per+per*per*per) as u64)*bs as u64 {return Err("file too large");}
        let size=old.size.max(end);let needed=(size as usize).div_ceil(bs);
        if needed>2*1024*1024 {return Err("file too large");}
        let cached=cache.filter(|(size,roots,_,_,epoch)|*size==old.size&&*roots==old.block&&epoch.wrapping_add(1)==*_guard);
        let (mut blocks,old_meta)=if let Some((_,_,map,meta,_))=cached {(map.to_vec(),meta)} else {(if old.size==0 {Vec::new()}else{self.block_map(&old)},self.metadata_blocks(&old))};
        blocks.resize(needed,0);let first=off as usize/bs;let last=(end as usize-1)/bs;
        let mut tx=self.tx();
        let mut fresh=alloc::vec![false;last-first+1];let mut dirty_first=usize::MAX;let mut dirty_end=0;
        for b in first..=last {if blocks[b]==0 {blocks[b]=tx.alloc().ok_or("no space")?;fresh[b-first]=true;dirty_first=dirty_first.min(b);dirty_end=b+1;}}
        let (ptrs,meta,retired)=self.rewrite_ptrs(&mut tx,&old,&blocks,old_meta,dirty_first,dirty_end).ok_or("no space")?;
        let mut b=first;
        while b<=last {
            let mut e=b+1;while e<=last&&e-b<256&&blocks[e]==blocks[e-1]+1 {e+=1;}
            let mut run=alloc::vec![0u8;(e-b)*bs];
            for i in b..e {
                let start=i*bs;let lo=(off as usize).max(start);let hi=(end as usize).min(start+bs);
                let row=&mut run[(i-b)*bs..(i-b+1)*bs];
                if (lo>start||hi<start+bs)&&!fresh[i-first] {row.copy_from_slice(&self.block(blocks[i]));}
                row[lo-start..hi-start].copy_from_slice(&data[lo-off as usize..hi-off as usize]);
            }
            disk_write(blocks[b] as u64*bs as u64,&run);b=e;
        }
        tx.commit();let used=blocks.iter().filter(|&&b|b!=0).count() as u32+meta;
        self.patch_inode(ino,|raw| {let links=le16(&raw[26..]);set_inode(raw,old.mode,old.uid,old.gid,size,links,used*(self.block_size/512),&ptrs);});
        let mut gone=self.tx();for b in retired {gone.release(b);}gone.commit();self.sync_backups();Ok((size,ptrs,blocks,meta,*_guard))
    }

    /// Resize an inode sparsely; shrinking clears the retained block's tail.
    pub fn truncate_inode(&self,ino:u32,size:u64)->Result<(),&'static str> {
        let _guard=write_lock();let old=self.read_inode(ino);
        if old.mode&0xF000!=0x8000{return Err("not a regular file");}
        let bs=self.block_size as usize;let per=bs/4;
        if size>((12+per+per*per+per*per*per) as u64)*bs as u64 {return Err("file too large");}
        let needed=(size as usize).div_ceil(bs);
        if needed>2*1024*1024||old.size.div_ceil(bs as u64)>2*1024*1024{return Err("file too large");}
        let old_blocks=if old.size==0 {Vec::new()}else{self.block_map(&old)};
        let mut blocks=old_blocks.clone();blocks.resize(needed,0);
        let mut tx=self.tx();let (ptrs,meta)=self.build_ptrs(&mut tx,&blocks).ok_or("no space")?;
        if size<old.size&&size%bs as u64!=0&&blocks.last().is_some_and(|&b|b!=0) {
            let b=*blocks.last().unwrap();let mut bytes=self.block(b);bytes[size as usize%bs..].fill(0);self.write_block(b,&bytes);
        }
        tx.commit();let used=blocks.iter().filter(|&&b|b!=0).count() as u32+meta;
        self.patch_inode(ino,|raw| {let links=le16(&raw[26..]);set_inode(raw,old.mode,old.uid,old.gid,size,links,used*(self.block_size/512),&ptrs);});
        let mut gone=self.tx();for &b in old_blocks.iter().skip(needed) {if b!=0 {gone.release(b);}}
        self.release_indirect(&old,false,&mut gone);gone.commit();self.sync_backups();Ok(())
    }

    /// Add a `(name -> child)` entry to directory `dir_ino` (direct blocks only).
    fn dir_insert(&self, dir_ino: u32, name: &str, child: u32, is_dir: bool) -> Result<(), &'static str> {
        self.dir_insert_ft(dir_ino, name, child, if is_dir { 2 } else { 1 })
    }

    /// [`Self::dir_insert`] with an explicit `EXT2_FT_*` code (1 file, 2 dir, 7 symlink).
    fn dir_insert_ft(&self, dir_ino: u32, name: &str, child: u32, ftype: u8) -> Result<(), &'static str> {
        let ft: u8 = if !self.filetype { 0 } else { ftype };
        let need = round4(8 + name.len());
        let bs = self.block_size as usize;
        let dir = self.read_inode(dir_ino);
        let dir_blocks = self.block_map(&dir); // all blocks, indirect ones included
        let mut data = self.read_file(&dir); // the whole directory in a few large reads
        data.resize(dir_blocks.len() * bs, 0);

        for (bi, &bno) in dir_blocks.iter().enumerate() {
            if bno == 0 {
                continue;
            }
            let blk = &mut data[bi * bs..(bi + 1) * bs];
            let mut off = 0;
            while off + 8 <= bs {
                let ino = le32(&blk[off..]);
                let rec_len = le16(&blk[off + 4..]) as usize;
                let name_len = blk[off + 6] as usize;
                if rec_len == 0 || off + rec_len > bs {
                    break;
                }
                let used = if ino == 0 { 0 } else { round4(8 + name_len) };
                if rec_len - used >= need {
                    let no = off + used;
                    let nrec = rec_len - used;
                    if used != 0 {
                        blk[off + 4..off + 6].copy_from_slice(&(used as u16).to_le_bytes());
                    }
                    blk[no..no + 4].copy_from_slice(&child.to_le_bytes());
                    blk[no + 4..no + 6].copy_from_slice(&(nrec as u16).to_le_bytes());
                    blk[no + 6] = name.len() as u8;
                    blk[no + 7] = ft;
                    blk[no + 8..no + 8 + name.len()].copy_from_slice(name.as_bytes());
                    self.write_block(bno, blk);
                    return Ok(());
                }
                off += rec_len;
            }
        }

        // No gap anywhere. Past the direct blocks the directory grows like a file: one more block
        // appended with `write_at_locked` (only the new block and the indirect blocks are written).
        let Some(slot) = (0..12).find(|&s| dir.block[s] == 0) else {
            let old_len = data.len();
            let mut blk = vec![0u8; bs];
            blk[0..4].copy_from_slice(&child.to_le_bytes());
            blk[4..6].copy_from_slice(&(bs as u16).to_le_bytes());
            blk[6] = name.len() as u8;
            blk[7] = ft;
            blk[8..8 + name.len()].copy_from_slice(name.as_bytes());
            data.extend_from_slice(&blk);
            return self.write_at_locked(dir_ino, &data, old_len, old_len + bs);
        };
        let bno = self.alloc_block().ok_or("no free block for dir")?;
        let mut blk = vec![0u8; bs];
        blk[0..4].copy_from_slice(&child.to_le_bytes());
        blk[4..6].copy_from_slice(&(bs as u16).to_le_bytes());
        blk[6] = name.len() as u8;
        blk[7] = ft;
        blk[8..8 + name.len()].copy_from_slice(name.as_bytes());
        self.write_block(bno, &blk);

        let mut nb = dir.block;
        nb[slot] = bno;
        let add512 = self.block_size / 512;
        self.patch_inode(dir_ino, |raw| {
            let size = le32(&raw[4..]) + bs as u32;
            raw[4..8].copy_from_slice(&size.to_le_bytes());
            let blk512 = le32(&raw[28..]) + add512;
            raw[28..32].copy_from_slice(&blk512.to_le_bytes());
            for (i, b) in nb.iter().enumerate() {
                raw[40 + i * 4..44 + i * 4].copy_from_slice(&b.to_le_bytes());
            }
        });
        Ok(())
    }

    /// Create `path` (or overwrite it) as a regular file holding `data`,
    /// owned by the system account (`0`/`0`) — every existing kernel-internal
    /// caller (registry hives, credential store, integrity baseline, the
    /// ext2 write tests) wants exactly that, unchanged. [`Self::write_path_owned`]
    /// is the version a real syscall-driven creation (`open(O_CREAT, ...)`)
    /// goes through instead, to give the creating task's own uid.
    pub fn write_path(&self, path: &str, data: &[u8]) -> Result<(), &'static str> {
        self.write_path_owned(path, data, 0, 0)
    }

    /// [`Self::write_path`], but a newly *created* file is owned by
    /// `uid`/`gid` instead of always `0`/`0`. Overwriting an *existing* file
    /// never changes its owner — matches real Unix: truncating a file you
    /// have write access to doesn't let you take it over.
    pub fn write_path_owned(&self, path: &str, data: &[u8], uid: u32, gid: u32) -> Result<(), &'static str> {
        self.write_path_owned_inner(path, data, uid, gid)
    }

    fn write_path_owned_inner(&self, path: &str, data: &[u8], uid: u32, gid: u32) -> Result<(), &'static str> {
        let _fs = write_lock();
        let (parent, name) = split_parent(path).ok_or("bad path")?;
        let parent_ino = self.path_lookup(parent).ok_or("parent dir missing")?;
        if name.len() > 255 {
            return Err("name too long");
        }
        let (block, blocks512) = self.lay_out_data(data).ok_or("no space / file too large")?;

        match self.lookup(parent_ino, name) {
            Some(ino) => {
                let old = self.read_inode(ino);
                let (mode, old_uid, old_gid) = (old.mode, old.uid, old.gid);
                // Commit the new blocks *before* freeing the old ones — the
                // inode patch below is a single-sector write (one ext2 inode
                // never straddles a sector: `inode_size` divides `SECTOR`
                // evenly, and offsets are inode-aligned), so it's the one
                // truly atomic step in this whole operation: a crash before
                // it, the file is still exactly its old self; a crash after
                // it, exactly its new self. Freeing first (the old order)
                // opened a real corruption window — a crash between "mark
                // old blocks free" and "repoint the inode" left the bitmap
                // and the inode disagreeing about who owns those blocks,
                // not just "lost this write". The only residual cost of the
                // new order is a leaked (never-freed) set of blocks if a
                // crash lands between the two writes below — recoverable by
                // fsck, not corruption.
                self.patch_inode(ino, |raw| {
                    let links = le16(&raw[26..]);
                    set_inode(raw, mode, old_uid, old_gid, data.len() as u64, links, blocks512, &block);
                });
                #[cfg(feature = "regcrashtest")]
                if path == "/regcrash-test.bin" {
                    crate::kprintln!(
                        "THOS: regcrash         simulating a crash: inode committed, blocks not yet freed"
                    );
                    crate::exit_qemu(crate::ExitCode::Success);
                    crate::hcf();
                }
                self.free_all_blocks(&old);
            }
            None => {
                let ino = self.alloc_inode(false).ok_or("no free inode")?;
                self.patch_inode(ino, |raw| {
                    set_inode(raw, 0o100_644, uid, gid, data.len() as u64, 1, blocks512, &block);
                });
                self.dir_insert(parent_ino, name, ino, false)?;
            }
        }
        self.sync_backups();
        Ok(())
    }

    /// [`Self::mkdir_path`], owned by the system account — see
    /// `write_path`/`write_path_owned`'s split for why.
    pub fn mkdir_path(&self, path: &str) -> Result<(), &'static str> {
        self.mkdir_path_owned(path, 0, 0)
    }

    /// Create directory `path`, owned by `uid`/`gid`. The parent must
    /// exist; `path` must not.
    pub fn mkdir_path_owned(&self, path: &str, uid: u32, gid: u32) -> Result<(), &'static str> {
        let _fs = write_lock();
        let (parent, name) = split_parent(path).ok_or("bad path")?;
        let parent_ino = self.path_lookup(parent).ok_or("parent dir missing")?;
        if self.lookup(parent_ino, name).is_some() {
            return Err("already exists");
        }
        let ino = self.alloc_inode(true).ok_or("no free inode")?;
        let bno = self.alloc_block().ok_or("no free block")?;
        let bs = self.block_size as usize;
        let dft: u8 = if self.filetype { 2 } else { 0 };

        let mut blk = vec![0u8; bs];
        blk[0..4].copy_from_slice(&ino.to_le_bytes()); // "."
        blk[4..6].copy_from_slice(&12u16.to_le_bytes());
        blk[6] = 1;
        blk[7] = dft;
        blk[8] = b'.';
        blk[12..16].copy_from_slice(&parent_ino.to_le_bytes()); // ".."
        blk[16..18].copy_from_slice(&((bs - 12) as u16).to_le_bytes());
        blk[18] = 2;
        blk[19] = dft;
        blk[20] = b'.';
        blk[21] = b'.';
        self.write_block(bno, &blk);

        let mut block = [0u32; 15];
        block[0] = bno;
        self.patch_inode(ino, |raw| {
            set_inode(raw, 0o040_755, uid, gid, bs as u64, 2, self.block_size / 512, &block);
        });
        self.dir_insert(parent_ino, name, ino, true)?;
        self.bump_links(parent_ino, 1); // the child's ".."
        self.sync_backups();
        Ok(())
    }

    /// `chmod(2)`: replace `path`'s permission bits (the low 12 bits —
    /// permissions plus setuid/setgid/sticky) with `perm`, leaving the file
    /// type (the upper nibble `read_inode`/`Inode::access_ok` key off) alone.
    /// The caller (`syscall::sys_chmod`) is responsible for the "only the
    /// owner or root may do this" check — this just writes the bits.
    pub fn chmod_path(&self, path: &str, perm: u16) -> Result<(), &'static str> {
        let _fs = write_lock();
        let ino = self.path_lookup(path).ok_or("no such file")?;
        let file_type = self.read_inode(ino).mode & 0xF000;
        self.patch_inode(ino, |raw| {
            let mode = file_type | (perm & 0o7777);
            raw[0..2].copy_from_slice(&mode.to_le_bytes());
        });
        Ok(())
    }

    /// `chown(2)`: replace `path`'s owner/group. Same split as `chmod_path`
    /// — the permission check (real `chown` is stricter: owner alone isn't
    /// enough, it's root-only, matching modern Unix) lives in the caller.
    pub fn chown_path(&self, path: &str, uid: u32, gid: u32) -> Result<(), &'static str> {
        let _fs = write_lock();
        let ino = self.path_lookup(path).ok_or("no such file")?;
        self.patch_inode(ino, |raw| {
            raw[2..4].copy_from_slice(&(uid as u16).to_le_bytes());
            raw[24..26].copy_from_slice(&(gid as u16).to_le_bytes());
        });
        Ok(())
    }

    /// Remove `name` from directory `dir_ino`: splice its record out (merge into
    /// the previous entry, or tombstone it with `inode = 0` if it is first in
    /// its block). Returns the child inode number.
    fn dir_remove(&self, dir_ino: u32, name: &str) -> Option<u32> {
        let bs = self.block_size as usize;
        let dir = self.read_inode(dir_ino);
        let blocks = self.block_map(&dir);
        let mut data = self.read_file(&dir); // all blocks in a few large reads
        data.resize(blocks.len() * bs, 0);
        for (bi, &bno) in blocks.iter().enumerate() {
            if bno == 0 {
                continue;
            }
            let blk = &mut data[bi * bs..(bi + 1) * bs];
            let mut off = 0;
            let mut prev: Option<usize> = None;
            while off + 8 <= bs {
                let ino = le32(&blk[off..]);
                let rec_len = le16(&blk[off + 4..]) as usize;
                let name_len = blk[off + 6] as usize;
                if rec_len == 0 || off + rec_len > bs {
                    break;
                }
                if ino != 0
                    && name_len == name.len()
                    && &blk[off + 8..off + 8 + name_len] == name.as_bytes()
                {
                    match prev {
                        Some(p) => {
                            let merged = le16(&blk[p + 4..]) as usize + rec_len;
                            blk[p + 4..p + 6].copy_from_slice(&(merged as u16).to_le_bytes());
                        }
                        None => blk[off..off + 4].copy_from_slice(&0u32.to_le_bytes()),
                    }
                    self.write_block(bno, blk);
                    return Some(ino);
                }
                prev = Some(off);
                off += rec_len;
            }
        }
        None
    }

    /// Mark an inode free in the bitmap + counts, and stamp it deleted.
    fn free_inode(&self, ino: u32, is_dir: bool) {
        let g = (ino - 1) / self.inodes_per_group;
        let i = ((ino - 1) % self.inodes_per_group) as usize;
        let bmb = le32(&self.read_bgd(g)[4..]);
        let mut bm = self.block(bmb);
        if bm[i / 8] & (1 << (i % 8)) != 0 {
            bm[i / 8] &= !(1 << (i % 8));
            self.write_block(bmb, &bm);
            self.bgd_add16(g, 14, 1); // free inodes ++
            self.sb_add32(16, 1);
            if is_dir {
                self.bgd_add16(g, 16, -1); // used dirs --
            }
        }
        self.patch_inode(ino, |raw| {
            raw[26..28].copy_from_slice(&0u16.to_le_bytes()); // i_links_count
            // i_dtime: a plausible timestamp (2025-01-01). A tiny value like 1
            // is mistaken by e2fsck for an orphan-list "next inode" pointer.
            raw[20..24].copy_from_slice(&1_735_689_600u32.to_le_bytes());
            raw[28..32].copy_from_slice(&0u32.to_le_bytes()); // i_blocks
        });
    }

    /// Unlink a regular file: drop its last link and free it.
    pub fn unlink_path(&self, path: &str) -> Result<(), &'static str> {
        let _fs = write_lock();
        self.unlink_inner(path)
    }

    fn unlink_inner(&self, path: &str) -> Result<(), &'static str> {
        let (parent, name) = split_parent(path).ok_or("bad path")?;
        let parent_ino = self.path_lookup(parent).ok_or("parent dir missing")?;
        let ino = self.lookup(parent_ino, name).ok_or("no such file")?;
        let node = self.read_inode(ino);
        if node.mode & 0xF000 == 0x4000 {
            return Err("is a directory");
        }
        self.dir_remove(parent_ino, name).ok_or("dirent vanished")?;
        let links = le16(&disk_range(self.inode_off(ino) + 26, 2)); // i_links_count @ 26
        if links <= 1 {
            if !(node.mode & 0xF000 == 0xA000 && node.size < 60) {
                self.free_all_blocks(&node); // (a fast symlink's "block pointers" are its text)
            }
            self.free_inode(ino, false);
        } else {
            self.bump_links(ino, -1);
        }
        self.sync_backups();
        Ok(())
    }

    /// `link(2)`: a new directory entry for an existing regular file.
    pub fn link_path(&self, old: &str, new: &str) -> Result<(), &'static str> {
        let _fs = write_lock();
        let ino = self.path_lookup(old).ok_or("no such file")?;
        if self.read_inode(ino).mode & 0xF000 == 0x4000 {
            return Err("is a directory");
        }
        let (np, nname) = split_parent(new).ok_or("bad path")?;
        let nparent = self.path_lookup(np).ok_or("parent dir missing")?;
        if self.lookup(nparent, nname).is_some() {
            return Err("already exists");
        }
        self.dir_insert(nparent, nname, ino, false)?;
        self.bump_links(ino, 1);
        self.sync_backups();
        Ok(())
    }

    /// `rename(2)`: move/rename `from` to `to` (replacing an existing file, or an empty directory
    /// by a directory). Hard links are not involved — the same inode gets a new directory entry.
    pub fn rename_path(&self, from: &str, to: &str) -> Result<(), &'static str> {
        let _fs = write_lock();
        if from == to {
            return Ok(());
        }
        let (fp, fname) = split_parent(from).ok_or("bad path")?;
        let (tp, tname) = split_parent(to).ok_or("bad path")?;
        let fparent = self.path_lookup(fp).ok_or("parent dir missing")?;
        let tparent = self.path_lookup(tp).ok_or("parent dir missing")?;
        let ino = self.lookup(fparent, fname).ok_or("no such file")?;
        let is_dir = self.read_inode(ino).mode & 0xF000 == 0x4000;
        if is_dir && (to.starts_with(from) && to[from.len()..].starts_with('/')) {
            return Err("invalid");
        }
        if let Some(dst) = self.lookup(tparent, tname) {
            if dst == ino {
                return Ok(());
            }
            let dir_dst = self.read_inode(dst).mode & 0xF000 == 0x4000;
            match (is_dir, dir_dst) {
                (true, true) => self.rmdir_inner(to)?,
                (false, false) => self.unlink_inner(to)?,
                (false, true) => return Err("is a directory"),
                (true, false) => return Err("not a directory"),
            }
        }
        self.dir_insert(tparent, tname, ino, is_dir)?;
        self.dir_remove(fparent, fname).ok_or("dirent vanished")?;
        if is_dir && fparent != tparent {
            // the moved directory's ".." now names the new parent
            let node = self.read_inode(ino);
            let mut blk = self.block(node.block[0]);
            blk[12..16].copy_from_slice(&tparent.to_le_bytes());
            self.write_block(node.block[0], &blk);
            self.bump_links(fparent, -1);
            self.bump_links(tparent, 1);
        }
        self.sync_backups();
        Ok(())
    }

    /// Remove an empty directory.
    pub fn rmdir_path(&self, path: &str) -> Result<(), &'static str> {
        let _fs = write_lock();
        self.rmdir_inner(path)
    }

    fn rmdir_inner(&self, path: &str) -> Result<(), &'static str> {
        let (parent, name) = split_parent(path).ok_or("bad path")?;
        let parent_ino = self.path_lookup(parent).ok_or("parent dir missing")?;
        let ino = self.lookup(parent_ino, name).ok_or("no such directory")?;
        let node = self.read_inode(ino);
        if node.mode & 0xF000 != 0x4000 {
            return Err("not a directory");
        }
        let data = self.read_file(&node);
        let mut off = 0;
        while off + 8 <= data.len() {
            let e_ino = le32(&data[off..]);
            let rl = le16(&data[off + 4..]) as usize;
            let nl = data[off + 6] as usize;
            if rl == 0 {
                break;
            }
            let nm = &data[off + 8..(off + 8 + nl).min(data.len())];
            if e_ino != 0 && nm != b"." && nm != b".." {
                return Err("directory not empty");
            }
            off += rl;
        }
        self.dir_remove(parent_ino, name).ok_or("dirent vanished")?;
        self.free_all_blocks(&node);
        self.free_inode(ino, true);
        self.bump_links(parent_ino, -1); // the child's ".." is gone
        self.sync_backups();
        Ok(())
    }

    /// Does group `g` (>= 1) hold a backup superblock + GDT? (`sparse_super`.)
    fn has_backup(&self, g: u32) -> bool {
        if !self.sparse_super || g <= 1 {
            return true;
        }
        [3u32, 5, 7].iter().any(|&base| {
            let mut p = base;
            while p < g {
                match p.checked_mul(base) {
                    Some(v) => p = v,
                    None => return false,
                }
            }
            p == g
        })
    }

    /// Re-write every backup superblock + group-descriptor table from the
    /// primary, so `e2fsck` stays happy after a mutation on a multi-group fs.
    fn sync_backups(&self) {
        // The end of every mutating operation: everything it wrote becomes durable here (one cache
        // flush instead of one per block).
        let _ = crate::blk::flush();
        let groups = self.group_count();
        if groups <= 1 {
            return;
        }
        // The backup superblocks/descriptor tables only matter to fsck: refresh them at most every few
        // seconds, and at shutdown (`flush_backups`), not after every created file.
        let now = crate::timer::monotonic_ns();
        if now.saturating_sub(LAST_BACKUP_NS.load(Ordering::Relaxed)) < 5_000_000_000 {
            BACKUPS_DIRTY.store(true, Ordering::Release);
            return;
        }
        LAST_BACKUP_NS.store(now, Ordering::Relaxed);
        BACKUPS_DIRTY.store(false, Ordering::Release);
        self.write_backups();
    }

    /// Write out pending backup refreshes now (shutdown, end of the self-tests).
    pub fn flush_backups(&self) {
        if BACKUPS_DIRTY.swap(false, Ordering::AcqRel) {
            self.write_backups();
        }
    }

    fn write_backups(&self) {
        let groups = self.group_count();
        let sb = disk_range(SB_OFFSET, 1024);
        let gdt = disk_range(self.bgd_off(0), groups as usize * 32);
        for g in 1..groups {
            if !self.has_backup(g) {
                continue;
            }
            let sb_blk = self.first_data_block + g * self.blocks_per_group;
            let mut sbc = sb.clone();
            sbc[90..92].copy_from_slice(&(g as u16).to_le_bytes()); // s_block_group_nr
            disk_write(sb_blk as u64 * self.block_size as u64, &sbc);
            disk_write((sb_blk + 1) as u64 * self.block_size as u64, &gdt);
        }
        let _ = crate::blk::flush();
    }
}


/// A batch of block allocations / frees. The bitmaps and free counters each group needs are
/// read **once**, edited in memory and written **once** at [`BlockTx::commit`] — instead of
/// a read-modify-write of the bitmap, the group descriptor and the superblock for every single
/// block, which made writing a megabyte take dozens of seconds. Dropping the transaction
/// without committing leaves the disk's bitmaps untouched (blocks a failed allocation had
/// picked simply stay free).
struct BlockTx<'a> {
    fs: &'a Ext2,
    /// group -> (bitmap block number, bitmap bytes, modified)
    bitmaps: BTreeMap<u32, (u32, Vec<u8>, bool)>,
    /// group -> free-block count as read from its descriptor
    free: BTreeMap<u32, i64>,
    /// group -> change in free blocks to write back
    delta: BTreeMap<u32, i64>,
    next: BTreeMap<u32,usize>,
}

impl BlockTx<'_> {
    fn load(&mut self, g: u32) {
        if !self.bitmaps.contains_key(&g) {
            let bgd = self.fs.read_bgd(g);
            let bmb = le32(&bgd[0..]);
            self.free.insert(g, le16(&bgd[12..]) as i64);
            self.bitmaps.insert(g, (bmb, self.fs.block(bmb), false));
        }
    }

    /// The first free block (first-fit over the groups), marked used in the cached bitmap.
    fn alloc(&mut self) -> Option<u32> {
        let fs = self.fs;
        let groups = fs.group_count();
        for g in 0..groups {
            self.load(g);
            if self.free[&g] + self.delta.get(&g).copied().unwrap_or(0) <= 0 {
                continue;
            }
            let in_group = if g == groups - 1 {
                fs.block_count - fs.first_data_block - g * fs.blocks_per_group
            } else {
                fs.blocks_per_group
            };
            let bm = self.bitmaps.get_mut(&g).unwrap();
            let start=self.next.get(&g).copied().unwrap_or(0);
            if let Some(i)=crate::block_bitmap::find_free(&bm.1,start,in_group as usize) {
                bm.1[i/8]|=1<<(i%8);bm.2=true;self.next.insert(g,i+1);
                *self.delta.entry(g).or_insert(0)-=1;
                return Some(fs.first_data_block+g*fs.blocks_per_group+i as u32);
            }
        }
        None
    }

    /// Mark a block free (a no-op if its bit is already clear).
    fn release(&mut self, bno: u32) {
        let rel = bno - self.fs.first_data_block;
        let g = rel / self.fs.blocks_per_group;
        let i = (rel % self.fs.blocks_per_group) as usize;
        self.load(g);
        let bm = self.bitmaps.get_mut(&g).unwrap();
        if bm.1[i / 8] & (1 << (i % 8)) != 0 {
            self.next.entry(g).and_modify(|n|*n=(*n).min(i)).or_insert(i);
            bm.1[i / 8] &= !(1 << (i % 8));
            bm.2 = true;
            *self.delta.entry(g).or_insert(0) += 1;
        }
    }

    /// Write the changed bitmaps and counters.
    fn commit(self) {
        let mut total = 0i64;
        for (g, (bmb, bytes, dirty)) in &self.bitmaps {
            if *dirty {
                self.fs.write_block(*bmb, bytes);
            }
            let d = self.delta.get(g).copied().unwrap_or(0);
            if d != 0 {
                self.fs.bgd_add16(*g, 12, d);
                total += d;
            }
        }
        if total != 0 {
            self.fs.sb_add32(12, total);
        }
    }
}
