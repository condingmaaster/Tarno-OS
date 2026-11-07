// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded four-way sector cache. No allocation, tree traversal or sorting on
//! replacement: file I/O must not grow an eviction workspace on a kernel stack.
extern crate alloc;
use alloc::vec::Vec;

pub const SECTOR: usize = 512;
const WAYS: usize = 4;
const SETS: usize = 2048;

#[derive(Clone)]
struct Slot { key: u64, stamp: u64, valid: bool, bytes: [u8; SECTOR] }

pub struct SectorCache { slots: Vec<Slot>, tick: u64 }
impl SectorCache {
    pub const fn new() -> Self { Self { slots: Vec::new(), tick: 0 } }
    fn set(key: u64) -> usize { ((key ^ (key >> 11) ^ (key >> 22)) as usize & (SETS-1))*WAYS }
    fn next_stamp(&mut self) -> u64 { self.tick = self.tick.wrapping_add(1); self.tick }
    pub fn get(&mut self,key: u64,out: &mut [u8]) -> bool {
        assert_eq!(out.len(),SECTOR);
        if self.slots.is_empty() {return false;}
        let stamp=self.next_stamp();let base=Self::set(key);
        for slot in &mut self.slots[base..base+WAYS] {
            if slot.valid && slot.key==key {out.copy_from_slice(&slot.bytes);slot.stamp=stamp;return true;}
        }
        false
    }
    /// A read finishing after a concurrent write must preserve the writer's
    /// newer cached bytes (`overwrite=false`). Writes pass `overwrite=true`.
    pub fn put(&mut self,key: u64,data: &[u8],overwrite: bool) {
        assert_eq!(data.len(),SECTOR);
        if self.slots.is_empty() {
            self.slots=alloc::vec![Slot{key:0,stamp:0,valid:false,bytes:[0;SECTOR]};SETS*WAYS];
        }
        let stamp=self.next_stamp();let base=Self::set(key);
        let set=&mut self.slots[base..base+WAYS];
        if let Some(slot)=set.iter_mut().find(|s|s.valid && s.key==key) {
            if overwrite {slot.bytes.copy_from_slice(data);}
            slot.stamp=stamp;return;
        }
        let i=set.iter().position(|s|!s.valid).unwrap_or_else(|| {
            set.iter().enumerate().min_by_key(|(_,s)|s.stamp).unwrap().0
        });
        let slot=&mut set[i];slot.key=key;slot.stamp=stamp;slot.valid=true;slot.bytes.copy_from_slice(data);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn delayed_read_preserves_newer_write() {
        let mut c=SectorCache::new();let mut b=[0;SECTOR];
        assert!(!c.get(3,&mut b));c.put(3,&[7;SECTOR],true);c.put(3,&[1;SECTOR],false);
        assert!(c.get(3,&mut b));assert_eq!(b,[7;SECTOR]);
        c.put(3,&[9;SECTOR],true);assert!(c.get(3,&mut b));assert_eq!(b,[9;SECTOR]);
    }
    #[test] fn replacement_retains_recent_collision() {
        let keys:Vec<_>=(0..100_000).filter(|&k|SectorCache::set(k)==0).take(5).collect();
        let mut c=SectorCache::new();let mut b=[0;SECTOR];
        for (i,k) in keys[..4].iter().enumerate(){c.put(*k,&[i as u8;SECTOR],true);}
        assert!(c.get(keys[0],&mut b));c.put(keys[4],&[4;SECTOR],true);
        assert!(c.get(keys[0],&mut b));assert!(!c.get(keys[1],&mut b));
        assert!(c.get(keys[4],&mut b));assert_eq!(b,[4;SECTOR]);
    }
    #[test] fn repeated_full_cache_keeps_bytes_and_allocation_bounded() {
        let mut c=SectorCache::new();let mut b=[0;SECTOR];
        c.put(0,&b,true);let pointer=c.slots.as_ptr();let cap=c.slots.capacity();
        for k in 0u64..65_536 {
            let v=(k.wrapping_mul(17)) as u8;c.put(k,&[v;SECTOR],true);
            assert!(c.get(k,&mut b));assert_eq!(b,[v;SECTOR]);
            assert_eq!(c.slots.as_ptr(),pointer);assert_eq!(c.slots.capacity(),cap);
        }
        assert_eq!(c.slots.len(),SETS*WAYS);
    }
}
