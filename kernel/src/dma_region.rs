// SPDX-License-Identifier: GPL-2.0-or-later
//! Select a page-aligned DMA arena addressable by 32-bit PCI devices.
pub fn choose_low_region(regions:impl Iterator<Item=(u64,u64)>,needed:u64)->Option<u64> {
    regions.filter_map(|(base,len)| {
        let start=base.checked_add(4095)?&!4095;
        let end=base.saturating_add(len).min(1u64<<32)&!4095;
        let usable=end.checked_sub(start)?;
        (usable>=needed).then_some((start,usable))
    }).max_by_key(|&(_,len)|len).map(|(base,_)|base)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn more_ram_above_four_gib_does_not_move_dma_out_of_reach() {
        assert_eq!(choose_low_region([(0x100000,0xbff00000),(0x100000000,0x140000000)].into_iter(),0x200000),Some(0x100000));
    }
    #[test]
    fn straddling_range_is_clipped_and_page_aligned() {
        assert_eq!(choose_low_region([(0xff000001,0x2000000)].into_iter(),0x200000),Some(0xff001000));
    }
    #[test]
    fn high_only_or_too_small_memory_is_rejected() {
        assert_eq!(choose_low_region([(0x100000000,0x20000000),(0x100000,0x1000)].into_iter(),0x200000),None);
    }
}
