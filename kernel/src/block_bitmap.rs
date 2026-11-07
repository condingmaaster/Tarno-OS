// SPDX-License-Identifier: GPL-2.0-or-later
//! Find a free valid block without rescanning used bits individually.
pub fn find_free(bytes:&[u8],start:usize,count:usize)->Option<usize> {
    if start>=count||count>bytes.len()*8{return None;}
    for index in start/8..count.div_ceil(8) {
        let mut used=bytes[index];
        if index==start/8 {used|=((1u16<<(start%8))-1) as u8;}
        if used!=255 {
            let bit=index*8+(!used).trailing_zeros() as usize;
            if bit<count{return Some(bit);}
        }
    }
    None
}
#[cfg(test)] mod tests {
    use super::*;
    #[test] fn last_group_and_cursor_are_respected() {
        assert_eq!(find_free(&[255,0],8,9),Some(8));
        assert_eq!(find_free(&[255,1],8,9),None);
        assert_eq!(find_free(&[0],5,8),Some(5));assert_eq!(find_free(&[0],8,8),None);
        assert_eq!(find_free(&[0],0,9),None);
    }
    #[test] fn randomized_bitmaps_match_valid_free_blocks() {
        let mut state=7u32;
        for _ in 0..4096 {
            state=state.wrapping_mul(1664525).wrapping_add(1013904223);
            let bytes=state.to_le_bytes();let count=(state as usize>>8)%33;
            for start in 0..=count {
                let expected=(start..count).find(|&b|bytes[b/8]&(1<<(b%8))==0);
                assert_eq!(find_free(&bytes,start,count),expected);
            }
        }
    }
}
