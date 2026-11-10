// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded BCMA EROM decoder. Register format: Linux drivers/bcma/scan.h.
//! No MMIO here: malformed hardware data is tested on the host.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Core { pub manufacturer: u16, pub id: u16, pub revision: u8, pub address: u32, pub wrapper: u32 }
#[derive(Debug)]
pub struct Scan { pub cores: [Core; 32], pub count: usize }
fn word(words: &[u32], pos: &mut usize) -> Result<u32, &'static str> {
    let v = *words.get(*pos).ok_or("truncated EROM")?;
    *pos += 1;
    if v == u32::MAX { return Err("unreadable EROM"); }
    Ok(v)
}
pub fn parse(words: &[u32]) -> Result<Scan, &'static str> {
    let mut out = Scan { cores: [Core::default(); 32], count: 0 };
    let mut pos = 0;
    while pos < words.len() {
        let cia = word(words, &mut pos)?;
        if cia == 15 { return Ok(out); }
        if cia & 1 == 0 { continue; }
        if cia & 14 != 0 { return Err("expected core descriptor"); }
        let cib = word(words, &mut pos)?;
        if cib & 15 != 1 { return Err("missing second core descriptor"); }
        let mut c = Core { manufacturer: (cia >> 20) as u16, id: ((cia >> 8) & 0xfff) as u16, revision: (cib >> 24) as u8, ..Core::default() };
        let masters = (cib >> 4) & 31;
        let slave_ports = (cib >> 9) & 31;
        let master_wraps = (cib >> 14) & 31;
        let slave_wraps = (cib >> 19) & 31;
        for _ in 0..masters {
            if word(words, &mut pos)? & 15 != 3 { return Err("bad master port descriptor"); }
        }
        let mut bridge = false;
        while pos < words.len() {
            let desc = words[pos];
            if desc == 15 || (desc & 15 == 1) { break; }
            let desc = word(words, &mut pos)?;
            if desc & 1 == 0 { continue; }
            if desc & 6 != 4 { return Err("bad address descriptor"); }
            let mut addr = (desc & 0xfffff000) as u64;
            if desc & 8 != 0 { addr |= (word(words, &mut pos)? as u64) << 32; }
            if desc & 0x30 == 0x30 {
                let size_word = word(words, &mut pos)?;
                let mut size = (size_word & 0xfffff000) as u64;
                if size_word & 8 != 0 { size |= (word(words, &mut pos)? as u64) << 32; }
                // A 4 GiB PCI bridge region has a zero low size word and
                // a nonzero SG32 high word. It is not an empty region.
                if size == 0 { return Err("empty address region"); }
            }
            let kind = desc & 0xc0;
            let port = (desc >> 8) & 15;
            if kind == 0x40 { bridge = true; }
            if kind == 0 && port == 0 && c.address == 0 {
                c.address = u32::try_from(addr).map_err(|_| "64-bit core address unsupported")?;
            }
            if c.wrapper == 0 && ((master_wraps != 0 && kind == 0xc0 && port == 0) || (master_wraps == 0 && slave_wraps != 0 && kind == 0x80 && port == if slave_ports == 1 { 0 } else { 1 })) {
                c.wrapper = u32::try_from(addr).map_err(|_| "64-bit wrapper address unsupported")?;
            }
        }
        // Ignore bridges, router components and components without register space.
        if !bridge && slave_ports != 0 && c.address != 0 {
            if out.count == out.cores.len() { return Err("too many BCMA cores"); }
            out.cores[out.count] = c; out.count += 1;
        }
    }
    Err("EROM has no end marker")
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn decodes_chipcommon_d11_and_wrappers() {
        let w = [0x4bf80001, (22 << 24) | (1 << 19) | (1 << 9) | 1, 0x18000005, 0x18100085,
                 0x4bf81201, (16 << 24) | (1 << 14) | (1 << 9) | 1, 0x18001005, 0x181010c5, 15];
        let s = parse(&w).unwrap(); assert_eq!(s.count, 2);
        assert_eq!(s.cores[1], Core { manufacturer: 0x4bf, id: 0x812, revision: 16, address: 0x18001000, wrapper: 0x18101000 });
        for end in 0..w.len() { assert!(parse(&w[..end]).is_err()); }
    }
    #[test] fn consumes_extended_words_without_treating_them_as_tags() {
        let w = [0x4bf81201, (1 << 9) | 1, 0x1800103d, 0, 0x2008, 0, 15];
        assert_eq!(parse(&w).unwrap().cores[0].address, 0x18001000);
        let mut high = w; high[3] = 1; assert!(parse(&high).is_err());
    }
    #[test] fn rejects_bad_words_and_missing_end() {
        assert!(parse(&[u32::MAX]).is_err());
        assert!(parse(&[0x4bf81201, 3]).is_err());
        assert!(parse(&[0; 1024]).is_err());
    }
    #[test] fn accepts_4g_bridge_region_then_finds_wifi_core() {
        let w = [0x4bf82001, (1 << 9) | 1, 0x00000075, 8, 1,
                 0x4bf81201, (16 << 24) | (1 << 14) | (1 << 9) | 1, 0x18001005, 0x181010c5, 15];
        let s = parse(&w).unwrap();
        assert_eq!(s.count, 1); assert_eq!(s.cores[0].id, 0x812);
        for n in 0..w.len() { assert!(parse(&w[..n]).is_err()); }
        let mut empty = w; empty[4] = 0;
        assert_eq!(parse(&empty).unwrap_err(), "empty address region");
    }
    #[test] fn arbitrary_input_never_panics() {
        let mut seed = 7u32;
        for _ in 0..2000 { let mut words = [0; 128]; for w in &mut words { seed = seed.wrapping_mul(1664525).wrapping_add(1013904223); *w = seed; } let _ = parse(&words); }
    }
}
