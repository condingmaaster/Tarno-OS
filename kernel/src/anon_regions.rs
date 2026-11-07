// SPDX-License-Identifier: GPL-2.0-or-later
//! Sorted protections for anonymous address-space reservations.
extern crate alloc;
use alloc::vec::Vec;
#[derive(Clone, Default)]
pub struct Regions {
    ranges: Vec<Range>,
}
#[derive(Clone, Copy)]
struct Range {
    start: u64,
    end: u64,
    prot: u64,
}
impl Regions {
    fn index(&self, addr: u64) -> Option<usize> {
        let i = self
            .ranges
            .partition_point(|r| r.start <= addr)
            .checked_sub(1)?;
        (addr < self.ranges[i].end).then_some(i)
    }
    pub fn protection(&self, addr: u64) -> Option<u64> {
        self.index(addr).map(|i| self.ranges[i].prot)
    }
    pub fn end_at(&self, addr: u64) -> Option<u64> {
        self.index(addr).map(|i| self.ranges[i].end)
    }
    pub fn intersections(&self, start: u64, end: u64) -> Vec<(u64, u64)> {
        if start >= end {
            return Vec::new();
        }
        let first = self.ranges.partition_point(|r| r.end <= start);
        self.ranges[first..]
            .iter()
            .take_while(|r| r.start < end)
            .map(|r| (r.start.max(start), r.end.min(end)))
            .collect()
    }
    pub fn reserve(&mut self, start: u64, end: u64, prot: u64) {
        if start >= end {
            return;
        }
        self.remove(start, end);
        let i = self.ranges.partition_point(|r| r.start < start);
        self.ranges.insert(i, Range { start, end, prot });
        self.merge();
    }
    pub fn remove(&mut self, start: u64, end: u64) {
        self.edit(start, end, None);
    }
    pub fn protect(&mut self, start: u64, end: u64, prot: u64) {
        self.edit(start, end, Some(prot));
    }
    fn split_at(&mut self, addr: u64) {
        let Some(i) = self.index(addr) else {
            return;
        };
        if self.ranges[i].start == addr {
            return;
        }
        let right = Range {
            start: addr,
            ..self.ranges[i]
        };
        self.ranges[i].end = addr;
        self.ranges.insert(i + 1, right);
    }
    fn edit(&mut self, start: u64, end: u64, prot: Option<u64>) {
        if start >= end {
            return;
        }
        self.split_at(start);
        self.split_at(end);
        if let Some(p) = prot {
            let first = self.ranges.partition_point(|r| r.end <= start);
            for r in self.ranges[first..]
                .iter_mut()
                .take_while(|r| r.start < end)
            {
                r.prot = p;
            }
        } else {
            self.ranges.retain(|r| r.end <= start || r.start >= end);
        }
        self.merge();
    }
    fn merge(&mut self) {
        // Edits retain sorted, disjoint ranges. Compact in place instead of
        // reallocating the whole vector for every mmap/munmap/mprotect call.
        let mut used = 0;
        for read in 0..self.ranges.len() {
            let r = self.ranges[read];
            if used > 0
                && self.ranges[used - 1].end == r.start
                && self.ranges[used - 1].prot == r.prot
            {
                self.ranges[used - 1].end = r.end;
            } else {
                self.ranges[used] = r;
                used += 1;
            }
        }
        self.ranges.truncate(used);
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn partial_protection_preserves_both_sides() {
        let mut r = Regions::default();
        r.reserve(0x1000, 0x9000, 3);
        r.protect(0x3000, 0x5000, 0);
        assert_eq!(r.protection(0x2000), Some(3));
        assert_eq!(r.protection(0x3000), Some(0));
        assert_eq!(r.protection(0x5000), Some(3));
        r.protect(0x3000, 0x5000, 3);
        assert_eq!(r.ranges.len(), 1);
    }
    #[test]
    fn holes_remain_unmapped_after_protection_changes() {
        let mut r = Regions::default();
        r.reserve(0x1000, 0x9000, 3);
        r.remove(0x3000, 0x5000);
        r.protect(0x1000, 0x9000, 1);
        assert_eq!(r.protection(0x3000), None);
        assert_eq!(r.protection(0x5000), Some(1));
    }
    #[test]
    fn fixed_reservation_replaces_only_overlap() {
        let mut r = Regions::default();
        r.reserve(0x1000, 0x9000, 3);
        r.reserve(0x3000, 0x6000, 4);
        assert_eq!(r.protection(0x2000), Some(3));
        assert_eq!(r.protection(0x4000), Some(4));
        assert_eq!(r.protection(0x6000), Some(3));
    }
    #[test]
    fn mixed_operations_match_independent_address_grid() {
        let mut r = Regions::default();
        let mut grid = [None; 128];
        let mut seed = 73u32;
        for step in 0..6000 {
            seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
            let a = (seed as usize >> 8) % 128;
            seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
            let b = (a + 1 + (seed as usize >> 8) % 32).min(128);
            let p = (step % 8) as u64;
            match step % 3 {
                0 => {
                    r.reserve(a as u64, b as u64, p);
                    grid[a..b].fill(Some(p));
                }
                1 => {
                    r.remove(a as u64, b as u64);
                    grid[a..b].fill(None);
                }
                _ => {
                    r.protect(a as u64, b as u64, p);
                    for v in &mut grid[a..b] {
                        if v.is_some() {
                            *v = Some(p);
                        }
                    }
                }
            }
            for (i, &expected) in grid.iter().enumerate() {
                assert_eq!(r.protection(i as u64), expected, "step={step}, address={i}");
            }
            assert_eq!(r.protection(128), None);
            for pair in r.ranges.windows(2) {
                assert!(pair[0].end <= pair[1].start);
                assert!(pair[0].end != pair[1].start || pair[0].prot != pair[1].prot);
            }
            let mut covered = [false; 128];
            for (start, end) in r.intersections(a as u64, b as u64) {
                for v in &mut covered[start as usize..end as usize] {
                    *v = true;
                }
            }
            for i in 0..128 {
                assert_eq!(covered[i], i >= a && i < b && grid[i].is_some());
            }
        }
    }
    #[test]
    fn empty_edits_preserve_boundaries_and_end_lookup() {
        let mut r = Regions::default();
        r.reserve(10, 20, 0);
        r.reserve(30, 40, 3);
        r.protect(15, 15, 7);
        r.remove(35, 35);
        r.reserve(50, 50, 1);
        assert_eq!(r.end_at(9), None);
        assert_eq!(r.end_at(10), Some(20));
        assert_eq!(r.end_at(19), Some(20));
        assert_eq!(r.end_at(20), None);
        assert_eq!(r.end_at(30), Some(40));
        assert_eq!(r.intersections(15, 35), [(15, 20), (30, 35)]);
        assert!(r.intersections(20, 20).is_empty());
    }
}
