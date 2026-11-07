// SPDX-License-Identifier: GPL-2.0-or-later
//! Immutable antialiased glyphs baked from THOS's existing Lato faces.
//! This module is not registered as a Win32 font API on its own.
const DATA: &[u8] = include_bytes!("../font/gdi-lato.bin");
const HEADER: usize = 16;
const FONT_RECORD: usize = 32;
const GLYPH_RECORD: usize = 28;
fn u32_at(data: &[u8], off: usize) -> Option<u32> {
    Some(u32::from_le_bytes(
        data.get(off..off.checked_add(4)?)?.try_into().ok()?,
    ))
}
fn i32_at(data: &[u8], off: usize) -> Option<i32> {
    u32_at(data, off).map(|x| x as i32)
}
#[derive(Clone, Copy)]
pub struct Atlas<'a> {
    data: &'a [u8],
    pub fonts: usize,
    pub glyphs: usize,
    metadata_end: usize,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Metrics {
    pub pixels: i32,
    pub weight: i32,
    pub ascent: i32,
    pub descent: i32,
    pub line_height: i32,
    pub max_advance: i32,
    pub average_width: i32,
}
impl Metrics {
    pub fn height(self) -> i32 {
        self.ascent + self.descent
    }
}
#[derive(Clone, Copy)]
pub struct Font<'a> {
    atlas: Atlas<'a>,
    pub index: usize,
    pub metrics: Metrics,
    records: usize,
}
#[derive(Clone, Copy, Debug)]
pub struct Glyph<'a> {
    pub codepoint: u32,
    pub advance: i32,
    pub left: i32,
    pub top: i32,
    pub width: usize,
    pub height: usize,
    pub coverage: &'a [u8],
}
pub fn atlas() -> Option<Atlas<'static>> {
    Atlas::parse(DATA)
}
impl<'a> Atlas<'a> {
    pub fn parse(data: &'a [u8]) -> Option<Self> {
        if data.get(..4)? != b"THFA" || u32_at(data, 4)? != 1 {
            return None;
        }
        let fonts = u32_at(data, 8)? as usize;
        let glyphs = u32_at(data, 12)? as usize;
        if fonts == 0 || fonts > 64 || glyphs == 0 || glyphs > 1024 {
            return None;
        }
        let end = HEADER
            .checked_add(fonts.checked_mul(FONT_RECORD)?)?
            .checked_add(fonts.checked_mul(glyphs)?.checked_mul(GLYPH_RECORD)?)?;
        if end > data.len() {
            return None;
        }
        Some(Self {
            data,
            fonts,
            glyphs,
            metadata_end: end,
        })
    }
    pub fn font(self, index: usize) -> Option<Font<'a>> {
        if index >= self.fonts {
            return None;
        }
        let at = HEADER + index * FONT_RECORD;
        let metrics = Metrics {
            pixels: i32_at(self.data, at)?,
            weight: i32_at(self.data, at + 4)?,
            ascent: i32_at(self.data, at + 8)?,
            descent: i32_at(self.data, at + 12)?,
            line_height: i32_at(self.data, at + 16)?,
            max_advance: i32_at(self.data, at + 20)?,
            average_width: i32_at(self.data, at + 28)?,
        };
        if !(1..=128).contains(&metrics.pixels)
            || !matches!(metrics.weight, 400 | 700)
            || !(1..=256).contains(&metrics.ascent)
            || !(0..=256).contains(&metrics.descent)
            || !(1..=512).contains(&metrics.line_height)
            || !(0..=512).contains(&metrics.max_advance)
            || !(0..=512).contains(&metrics.average_width)
        {
            return None;
        }
        let records = u32_at(self.data, at + 24)? as usize;
        if records != HEADER + self.fonts * FONT_RECORD + index * self.glyphs * GLYPH_RECORD {
            return None;
        }
        Some(Font {
            atlas: self,
            index,
            metrics,
            records,
        })
    }
    /// Closest installed bitmap face, reporting its actual metrics. Negative
    /// LOGFONT height requests character pixels; positive requests cell height.
    pub fn choose(self, height: i32, weight: i32) -> Option<Font<'a>> {
        if !(0..=1000).contains(&weight) {
            return None;
        }
        let desired = if height == 0 {
            16
        } else {
            height.checked_abs()?
        };
        let weight = if weight >= 550 { 700 } else { 400 };
        let mut best = None;
        let mut distance = i64::MAX;
        for index in 0..self.fonts {
            let Some(font) = self.font(index) else {
                continue;
            };
            if font.metrics.weight != weight {
                continue;
            }
            let actual = if height > 0 {
                font.metrics.height()
            } else {
                font.metrics.pixels
            };
            let delta = (actual as i64 - desired as i64).abs();
            if delta < distance {
                best = Some(font);
                distance = delta;
            }
        }
        best
    }
}
impl<'a> Font<'a> {
    fn glyph_at(self, index: usize) -> Option<Glyph<'a>> {
        if index >= self.atlas.glyphs {
            return None;
        }
        let at = self.records + index * GLYPH_RECORD;
        let data = self.atlas.data;
        let codepoint = u32_at(data, at)?;
        let advance = i32_at(data, at + 4)?;
        let left = i32_at(data, at + 8)?;
        let top = i32_at(data, at + 12)?;
        let width = u32_at(data, at + 16)? as usize;
        let height = u32_at(data, at + 20)? as usize;
        let start = u32_at(data, at + 24)? as usize;
        if !(0..=512).contains(&advance)
            || !(-512..=512).contains(&left)
            || !(-512..=512).contains(&top)
            || width > 512
            || height > 512
            || start < self.atlas.metadata_end
        {
            return None;
        }
        let end = start.checked_add(width.checked_mul(height)?)?;
        let coverage = data.get(start..end)?;
        Some(Glyph {
            codepoint,
            advance,
            left,
            top,
            width,
            height,
            coverage,
        })
    }
    fn find(self, cp: u32) -> Option<Glyph<'a>> {
        let (mut low, mut high) = (0, self.atlas.glyphs);
        while low < high {
            let mid = low + (high - low) / 2;
            let glyph = self.glyph_at(mid)?;
            match glyph.codepoint.cmp(&cp) {
                core::cmp::Ordering::Less => low = mid + 1,
                core::cmp::Ordering::Greater => high = mid,
                core::cmp::Ordering::Equal => return Some(glyph),
            }
        }
        None
    }
    pub fn glyph(self, ch: char) -> Option<Glyph<'a>> {
        self.find(ch as u32).or_else(|| self.find('?' as u32))
    }
    pub fn measure(self, text: impl Iterator<Item = char>) -> Option<(i32, i32)> {
        let mut width = 0i32;
        for ch in text {
            width = width.checked_add(self.glyph(ch)?.advance)?;
        }
        Some((width, self.metrics.height()))
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn complete_asset_has_valid_ordered_glyphs() {
        let a = atlas().unwrap();
        assert_eq!((a.fonts, a.glyphs), (34, 200));
        let mut antialiased = 0;
        for i in 0..a.fonts {
            let f = a.font(i).unwrap();
            let mut last = 0;
            for g in 0..a.glyphs {
                let glyph = f.glyph_at(g).unwrap();
                assert!(glyph.codepoint > last);
                last = glyph.codepoint;
                assert_eq!(glyph.coverage.len(), glyph.width * glyph.height);
                antialiased += glyph
                    .coverage
                    .iter()
                    .filter(|p| **p > 0 && **p < 255)
                    .count();
            }
        }
        assert!(antialiased > 10000);
    }
    #[test]
    fn requested_faces_metrics_and_unicode_fallback() {
        let a = atlas().unwrap();
        let regular = a.choose(-16, 400).unwrap();
        let bold = a.choose(-56, 700).unwrap();
        assert_eq!((regular.metrics.pixels, regular.metrics.weight), (16, 400));
        assert_eq!((bold.metrics.pixels, bold.metrics.weight), (56, 700));
        assert_eq!(regular.glyph('ä').unwrap().codepoint, 0xe4);
        assert_eq!(regular.glyph('€').unwrap().codepoint, 0x20ac);
        assert_eq!(regular.glyph('😀').unwrap().codepoint, '?' as u32);
        let (w, h) = regular.measure("THOS ä€".chars()).unwrap();
        assert!(w > 16 && h == regular.metrics.height());
        assert_eq!(regular.measure("".chars()), Some((0, h)));
        assert!(a.choose(i32::MIN, 400).is_none());
        assert!(a.choose(16, -1).is_none());
    }
    #[test]
    fn truncated_or_corrupt_metadata_never_indexes_outside_asset() {
        for n in [0, 1, 4, 8, 12, 15, 16, 100, 1000] {
            assert!(Atlas::parse(&DATA[..n]).is_none());
        }
        let mut bytes = DATA.to_vec();
        for off in [4usize, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44] {
            let old = bytes[off..off + 4].to_vec();
            bytes[off..off + 4].fill(255);
            if let Some(a) = Atlas::parse(&bytes) {
                if let Some(f) = a.font(0) {
                    let _ = f.glyph('A');
                    let _ = f.measure("test".chars());
                }
            }
            bytes[off..off + 4].copy_from_slice(&old);
        }
        let f = atlas().unwrap().font(0).unwrap();
        assert!(f.glyph_at(200).is_none());
    }
}
