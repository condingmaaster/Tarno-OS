// SPDX-License-Identifier: GPL-2.0-or-later
//! Pure framebuffer MTRR range planning and RAM-overlap checks.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Range { pub base: u64, pub size: u64 }
pub fn plan(phys: u64, len: u64, physical_bits: u32) -> Result<Range, &'static str> {
    if len == 0 || !(12..=52).contains(&physical_bits) { return Err("invalid framebuffer range"); }
    let limit = 1u64 << physical_bits;
    let end = phys.checked_add(len).filter(|&e| e <= limit).ok_or("framebuffer exceeds physical address space")?;
    let size = len.checked_next_power_of_two().ok_or("framebuffer range overflow")?.max(1 << 20);
    let base = phys & !(size - 1);
    // Do not mark only part of a misaligned framebuffer WC, or enlarge the
    // range into unrelated device regions without aperture information.
    if end > base.checked_add(size).filter(|&e| e <= limit).ok_or("MTRR range overflow")? { return Err("framebuffer crosses an MTRR alignment boundary"); }
    Ok(Range { base, size })
}
pub fn overlaps_ram(range: Range, base: u64, len: u64) -> bool {
    if len == 0 { return false; }
    let Some(end) = base.checked_add(len) else { return true; };
    range.base < end && base < range.base + range.size
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn pci_hole_below_high_ram_is_eligible() {
        let r = plan(0xd0000000, 1280 * 800 * 4, 36).unwrap();
        assert_eq!(r, Range { base: 0xd0000000, size: 4 * 1024 * 1024 });
        assert!(!overlaps_ram(r, 0, 0xc0000000));
        assert!(!overlaps_ram(r, 0x100000000, 0x100000000));
        assert!(r.base < 0x200000000); // old ram_top=8GiB check rejected this
    }
    #[test] fn rejects_ram_and_alignment_padding_overlaps() {
        let r = plan(0x100000000, 4096, 36).unwrap();
        assert!(overlaps_ram(r, 0x100000000, 0x100000000));
        let r = plan(0xc0020000, 4096, 36).unwrap();
        assert!(overlaps_ram(r, 0xc0000000, 0x10000));
        assert!(!overlaps_ram(r, r.base + r.size, 4096));
        assert!(!overlaps_ram(r, r.base - 4096, 4096));
        assert!(overlaps_ram(r, u64::MAX - 100, 200));
    }
    #[test] fn validates_full_coverage_and_overflow() {
        assert!(plan(0xd00ff000, 1024 * 1024, 36).is_err());
        assert!(plan(0, 0, 36).is_err());
        assert!(plan(u64::MAX - 1, 4096, 52).is_err());
        assert!(plan(1 << 36, 4096, 36).is_err());
        assert!(plan(0, 4096, 0).is_err());
    }
    #[test] fn accepted_ranges_cover_the_whole_framebuffer() {
        let mut seed = 7u64;
        for _ in 0..30_000 {
            seed = seed.wrapping_mul(6364136223846793005).wrapping_add(1);
            let p = seed & ((1 << 40) - 1); let n = ((seed >> 40) & 0xffffff) + 1;
            if let Ok(r) = plan(p, n, 40) {
                assert!(r.size.is_power_of_two()); assert_eq!(r.base & (r.size - 1), 0);
                assert!(r.base <= p && p + n <= r.base + r.size);
            }
        }
    }
}
