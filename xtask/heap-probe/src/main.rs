#[path = "../../../kernel/src/small_heap.rs"]
mod small_heap;
use std::alloc::{GlobalAlloc, Layout};
use std::time::Instant;
fn churn<A: GlobalAlloc>(a: &A) -> u128 {
    let mut live = Vec::new();
    let mut seed = 17usize;
    for i in 0..4096 {
        let n = 8 + ((i * 71) % 2040);
        let l = Layout::from_size_align(n, 8).unwrap();
        let p = unsafe { a.alloc(l) };
        assert!(!p.is_null());
        unsafe {
            p.write(i as u8);
        }
        live.push((p, l, i as u8));
    }
    let start = Instant::now();
    for i in 0..200000usize {
        seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
        let index = (seed >> 8) % live.len();
        let (p, l, v) = live[index];
        unsafe {
            assert_eq!(*p, v);
            a.dealloc(p, l);
        }
        let n = 8 + ((i * 97) % 2040);
        let l = Layout::from_size_align(n, 8).unwrap();
        let p = unsafe { a.alloc(l) };
        assert!(!p.is_null());
        unsafe {
            p.write(i as u8);
        }
        live[index] = (p, l, i as u8);
    }
    let time = start.elapsed().as_micros();
    for (p, l, v) in live {
        unsafe {
            assert_eq!(*p, v);
            a.dealloc(p, l);
        }
    }
    time
}
fn main() {
    let mut arena = vec![0u64; (64 << 20) / 8];
    let old = linked_list_allocator::LockedHeap::empty();
    unsafe {
        old.lock().init(arena.as_mut_ptr().cast(), arena.len() * 8);
    }
    let baseline = churn(&old);
    assert_eq!(old.lock().used(), 0);
    drop(old);
    let cached = small_heap::SmallHeap::empty();
    unsafe {
        cached.init(arena.as_mut_ptr().cast(), arena.len() * 8);
    }
    let improved = churn(&cached);
    cached.trim();
    println!("fragmented mixed-size churn, 200000 replacements: linked_us={baseline} cached_us={improved} ratio={:.2}",baseline as f64/improved as f64);
}
