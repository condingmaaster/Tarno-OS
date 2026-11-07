// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded caches for small kernel allocations, backed by the existing heap.
//! Cached blocks retain their class layout; large requests use the fallback directly.
//! Live small allocations are rounded up; the 511.5 KiB bound covers idle caches.
use core::alloc::{GlobalAlloc, Layout};
use core::ptr;
use linked_list_allocator::LockedHeap;
use spin::Mutex;

const CLASSES: [usize; 10] = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096];
const CACHE_LIMIT: usize = 64; // all classes together retain at most 511.5 KiB

struct FreeBlocks {
    head: *mut u8,
    count: usize,
}
// A free block belongs exclusively to this list while its mutex is held.
unsafe impl Send for FreeBlocks {}

pub struct SmallHeap {
    fallback: LockedHeap,
    caches: [Mutex<FreeBlocks>; 10],
}

impl SmallHeap {
    pub const fn empty() -> Self {
        Self {
            fallback: LockedHeap::empty(),
            caches: [const {
                Mutex::new(FreeBlocks {
                    head: ptr::null_mut(),
                    count: 0,
                })
            }; 10],
        }
    }

    /// The arena must be exclusively owned and outlive every allocation.
    pub unsafe fn init(&self, start: *mut u8, len: usize) {
        self.fallback.lock().init(start, len);
    }

    fn class(layout: Layout) -> Option<usize> {
        let need = layout.size().max(layout.align());
        CLASSES.iter().position(|&size| size >= need)
    }

    fn block_layout(index: usize) -> Layout {
        // Class sizes are powers of two and valid alignments.
        Layout::from_size_align(CLASSES[index], CLASSES[index]).unwrap()
    }

    /// Return idle cache blocks to the fallback, allowing other size classes
    /// and large allocations to reclaim this memory under pressure.
    pub fn trim(&self) {
        for (index, cache) in self.caches.iter().enumerate() {
            let mut head = {
                let mut list = cache.lock();
                let head = list.head;
                list.head = ptr::null_mut();
                list.count = 0;
                head
            };
            while !head.is_null() {
                unsafe {
                    let next = *(head as *mut *mut u8);
                    self.fallback.dealloc(head, Self::block_layout(index));
                    head = next;
                }
            }
        }
    }

    #[cfg(test)]
    fn fallback_used(&self) -> usize {
        self.fallback.lock().used()
    }
}

unsafe impl GlobalAlloc for SmallHeap {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let backing = match Self::class(layout) {
            Some(index) => {
                let mut list = self.caches[index].lock();
                if !list.head.is_null() {
                    let block = list.head;
                    list.head = *(block as *mut *mut u8);
                    list.count -= 1;
                    return block;
                }
                // Release the class lock before entering the fallback allocator.
                Self::block_layout(index)
            }
            None => layout,
        };
        let block = self.fallback.alloc(backing);
        if !block.is_null() {
            return block;
        }
        self.trim();
        self.fallback.alloc(backing)
    }

    unsafe fn dealloc(&self, block: *mut u8, layout: Layout) {
        let backing = match Self::class(layout) {
            Some(index) => {
                let mut list = self.caches[index].lock();
                if list.count < CACHE_LIMIT {
                    *(block as *mut *mut u8) = list.head;
                    list.head = block;
                    list.count += 1;
                    return;
                }
                Self::block_layout(index)
            }
            None => layout,
        };
        self.fallback.dealloc(block, backing);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    extern crate std;
    use std::{vec, vec::Vec};

    fn fixture() -> (Vec<u64>, SmallHeap) {
        let mut arena = vec![0u64; (4 << 20) / 8];
        let heap = SmallHeap::empty();
        unsafe {
            heap.init(arena.as_mut_ptr().cast(), arena.len() * 8);
        }
        (arena, heap)
    }

    #[test]
    fn alignment_contents_zeroing_and_reclaim() {
        let (_arena, heap) = fixture();
        let mut blocks = Vec::new();
        for size in [1, 7, 8, 9, 31, 65, 257, 1023, 4095, 4097, 32768] {
            for align in [1, 8, 64, 4096, 8192] {
                let layout = Layout::from_size_align(size, align).unwrap();
                let p = unsafe { heap.alloc(layout) };
                assert!(!p.is_null());
                assert_eq!(p as usize % align, 0);
                unsafe {
                    ptr::write_bytes(p, 0x71, size);
                }
                blocks.push((p, layout));
            }
        }
        for &(p, layout) in &blocks {
            assert!(unsafe { core::slice::from_raw_parts(p, layout.size()) }
                .iter()
                .all(|&b| b == 0x71));
        }
        for (p, layout) in blocks.into_iter().rev() {
            unsafe {
                heap.dealloc(p, layout);
            }
        }
        let layout = Layout::from_size_align(123, 64).unwrap();
        let p = unsafe { heap.alloc_zeroed(layout) };
        assert!(unsafe { core::slice::from_raw_parts(p, 123) }
            .iter()
            .all(|&b| b == 0));
        unsafe {
            heap.dealloc(p, layout);
        }
        heap.trim();
        assert_eq!(heap.fallback_used(), 0);
    }

    #[test]
    fn cached_blocks_do_not_cause_spurious_oom() {
        let (_arena, heap) = fixture();
        let layout = Layout::from_size_align(4096, 4096).unwrap();
        let mut blocks = Vec::new();
        loop {
            let p = unsafe { heap.alloc(layout) };
            if p.is_null() {
                break;
            }
            blocks.push(p);
        }
        assert!(blocks.len() > 900);
        for p in blocks {
            unsafe {
                heap.dealloc(p, layout);
            }
        }
        // Fits only after the cached blocks are returned and coalesced.
        let large = Layout::from_size_align((4 << 20) - 16384, 8).unwrap();
        let p = unsafe { heap.alloc(large) };
        assert!(!p.is_null());
        unsafe {
            heap.dealloc(p, large);
        }
        heap.trim();
        assert_eq!(heap.fallback_used(), 0);
    }

    #[test]
    fn realloc_keeps_data_across_size_classes() {
        let (_arena, heap) = fixture();
        let initial = Layout::from_size_align(257, 64).unwrap();
        unsafe {
            let p = heap.alloc(initial);
            assert!(!p.is_null());
            ptr::write_bytes(p, 0x39, 257);
            let grown = heap.realloc(p, initial, 4097);
            assert!(!grown.is_null());
            assert!(core::slice::from_raw_parts(grown, 257)
                .iter()
                .all(|&b| b == 0x39));
            let reduced = heap.realloc(grown, Layout::from_size_align(4097, 64).unwrap(), 17);
            assert!(!reduced.is_null());
            assert!(core::slice::from_raw_parts(reduced, 17)
                .iter()
                .all(|&b| b == 0x39));
            heap.dealloc(reduced, Layout::from_size_align(17, 64).unwrap());
        }
        heap.trim();
        assert_eq!(heap.fallback_used(), 0);
    }

    #[test]
    fn concurrent_classes_keep_live_payloads_and_unique_ownership() {
        let (_arena, heap) = fixture();
        std::thread::scope(|scope| {
            for worker in 0..8 {
                let heap = &heap;
                scope.spawn(move || {
                    let mut live = Vec::new();
                    for round in 0..6000 {
                        let size = 1 + ((round * 71 + worker * 19) % 6000);
                        let layout = Layout::from_size_align(size, 1 << (round % 8)).unwrap();
                        let p = unsafe { heap.alloc(layout) };
                        assert!(!p.is_null());
                        unsafe {
                            ptr::write_bytes(p, worker as u8 + 1, size);
                        }
                        live.push((p, layout));
                        if live.len() > 12 {
                            let (p, layout) = live.remove((round * 7) % live.len());
                            assert!(unsafe { core::slice::from_raw_parts(p, layout.size()) }
                                .iter()
                                .all(|&b| b == worker as u8 + 1));
                            unsafe {
                                heap.dealloc(p, layout);
                            }
                        }
                    }
                    for (p, layout) in live {
                        assert!(unsafe { core::slice::from_raw_parts(p, layout.size()) }
                            .iter()
                            .all(|&b| b == worker as u8 + 1));
                        unsafe {
                            heap.dealloc(p, layout);
                        }
                    }
                });
            }
        });
        heap.trim();
        assert_eq!(heap.fallback_used(), 0);
    }
}
