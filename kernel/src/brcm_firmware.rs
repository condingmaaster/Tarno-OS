// SPDX-License-Identifier: GPL-2.0-or-later
//! brcmsmac's little-endian (offset, length, tag) firmware index.
//! Format and MIMO16 tags: Linux brcmsmac/mac80211_if.c and ucode_loader.c.
//! Pure parser, shared by the kernel, image builder and host tests.
pub const MAX_BIN: usize = 1024 * 1024;
pub const MAX_INDEX: usize = 64 * 12;
#[derive(Debug)]
pub struct Firmware<'a> { binary: &'a [u8], index: &'a [u8] }
fn le32(bytes: &[u8]) -> u32 { u32::from_le_bytes(bytes[..4].try_into().unwrap()) }
impl<'a> Firmware<'a> {
    pub fn parse(binary: &'a [u8], index: &'a [u8]) -> Result<Self, &'static str> {
        if binary.is_empty() || binary.len() > MAX_BIN { return Err("invalid firmware size"); }
        if index.is_empty() || index.len() > MAX_INDEX || index.len() % 12 != 0 { return Err("invalid firmware index size"); }
        for (i, record) in index.chunks_exact(12).enumerate() {
            let offset = le32(record) as usize;
            let len = le32(&record[4..]) as usize;
            let tag = le32(&record[8..]);
            let end = offset.checked_add(len).ok_or("firmware range overflow")?;
            if len == 0 || end > binary.len() { return Err("firmware section out of bounds"); }
            if tag == 0 { return Err("invalid firmware tag"); }
            for previous in index[..i * 12].chunks_exact(12) {
                if le32(&previous[8..]) == tag { return Err("duplicate firmware tag"); }
                let po = le32(previous) as usize;
                let pe = po + le32(&previous[4..]) as usize; // already validated
                if offset < pe && po < end { return Err("overlapping firmware sections"); }
            }
        }
        Ok(Self { binary, index })
    }
    pub fn section(&self, tag: u32) -> Result<&'a [u8], &'static str> {
        let r = self.index.chunks_exact(12).find(|r| le32(&r[8..]) == tag).ok_or("required firmware tag missing")?;
        let o = le32(r) as usize;
        Ok(&self.binary[o..o + le32(&r[4..]) as usize])
    }
    /// Validate the profile needed before a future D11 MIMO microcode upload.
    /// This does not upload firmware or establish hardware compatibility.
    pub fn mimo16_bytes(&self) -> Result<usize, &'static str> {
        let code = self.section(10)?;
        let size = self.section(11)?;
        if size.len() != 4 || le32(size) as usize != code.len() || code.len() % 8 != 0 { return Err("invalid MIMO microcode size"); }
        for tag in [7, 8, 9] {
            let values = self.section(tag)?;
            if values.len() % 8 != 0 || values[values.len() - 8..values.len() - 6] != [0xff, 0xff] { return Err("invalid MIMO initialization table"); }
        }
        if self.section(14)?.len() != 4 || self.section(15)?.len() != 4 { return Err("invalid firmware version fields"); }
        Ok(code.len())
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn rec(o: u32, len: u32, tag: u32) -> Vec<u8> { [o, len, tag].into_iter().flat_map(u32::to_le_bytes).collect() }
    #[test] fn slices_valid_sections_and_rejects_truncation() {
        let b = [0u8; 32]; let h = rec(8, 24, 10);
        assert_eq!(Firmware::parse(&b, &h).unwrap().section(10).unwrap(), &b[8..]);
        for end in 0..h.len() { assert!(Firmware::parse(&b, &h[..end]).is_err()); }
        assert!(Firmware::parse(&b[..31], &h).is_err());
        assert!(Firmware::parse(&b, &rec(u32::MAX, u32::MAX, 1)).is_err());
    }
    #[test] fn rejects_ambiguous_and_empty_entries() {
        let b = [0; 32]; let mut h = rec(0, 8, 1); h.extend(rec(8, 8, 1));
        assert!(Firmware::parse(&b, &h).is_err());
        let mut h = rec(0, 8, 1); h.extend(rec(4, 8, 2)); assert!(Firmware::parse(&b, &h).is_err());
        assert!(Firmware::parse(&b, &rec(0, 0, 1)).is_err());
    }
    #[test] fn supplied_firmware_and_corrupted_scalar() {
        let binary = include_bytes!("../../assets/firmware/brcm/bcm43xx-0.fw");
        let index = include_bytes!("../../assets/firmware/brcm/bcm43xx_hdr-0.fw");
        assert_eq!(Firmware::parse(binary, index).unwrap().mimo16_bytes().unwrap(), 39864);
        let mut bad = binary.to_vec(); let size_record = index.chunks_exact(12).find(|r| le32(&r[8..]) == 11).unwrap();
        bad[le32(size_record) as usize] ^= 1;
        assert!(Firmware::parse(&bad, index).unwrap().mimo16_bytes().is_err());
    }
    #[test] fn arbitrary_indices_never_panic() {
        let mut seed = 3u32; let b = [0; 256];
        for _ in 0..2000 { let mut h = [0; 120]; for v in &mut h { seed = seed.wrapping_mul(1664525).wrapping_add(1013904223); *v = (seed >> 16) as u8; } let _ = Firmware::parse(&b, &h); }
    }
}
