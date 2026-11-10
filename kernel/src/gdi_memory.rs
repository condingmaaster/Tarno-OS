// SPDX-License-Identifier: GPL-2.0-or-later
//! Owned color bitmaps and memory DCs. Handles are never reused. The stock
//! monochrome bitmap is a valid selection token, but monochrome rasterization
//! is deliberately unsupported (calls fail rather than claiming color output).
use crate::gdi_blit::Shape;
use alloc::collections::BTreeMap;
#[cfg(test)]
use alloc::vec::Vec;
pub const STOCK_BITMAP: u64 = 0x7000_0000;
const FIRST_HANDLE: u64 = 0x4000_0000_0000_0000;
const MAX_BYTES: usize = 64 * 1024 * 1024;
const MAX_OBJECTS: usize = 4096;
// Pixel pages do not consume the fixed 96 MiB kernel heap. Only the frame
// list is heap metadata; partial physical allocation is reclaimed on failure.
#[cfg(not(test))]
pub struct PixelStore(pub alloc::sync::Arc<crate::process::Section>);
#[cfg(test)]
pub struct PixelStore(Vec<u32>);
impl PixelStore {
    fn zeroed(n: usize) -> Option<Self> {
        #[cfg(not(test))]
        {
            Some(Self(alloc::sync::Arc::new(
                crate::process::Section::try_zeroed(n.checked_mul(4)?)?,
            )))
        }
        #[cfg(test)]
        {
            let mut v = Vec::new();
            v.try_reserve_exact(n).ok()?;
            v.resize(n, 0);
            Some(Self(v))
        }
    }
    pub fn len(&self) -> usize {
        #[cfg(not(test))]
        {
            self.0.size / 4
        }
        #[cfg(test)]
        {
            self.0.len()
        }
    }
    pub fn ptr_at(&self, i: usize) -> Option<*mut u32> {
        if i >= self.len() {
            return None;
        }
        #[cfg(not(test))]
        {
            self.0.ptr_at(i * 4).map(|p| p as *mut u32)
        }
        #[cfg(test)]
        {
            Some(unsafe { self.0.as_ptr().add(i) as *mut u32 })
        }
    }
    pub fn read(&self, i: usize) -> u32 {
        unsafe { self.ptr_at(i).unwrap().read_volatile() }
    }
    pub fn write(&mut self, i: usize, v: u32) {
        unsafe { self.ptr_at(i).unwrap().write_volatile(v) }
    }
    pub fn fill(&mut self, start: usize, len: usize, v: u32) {
        let mut done = 0;
        while done < len {
            let off = start + done;
            let n = (len - done).min(1024 - off % 1024);
            let p = self.ptr_at(off).unwrap();
            for i in 0..n {
                unsafe {
                    p.add(i).write_volatile(v);
                }
            }
            done += n;
        }
    }
}
pub const SOLID_BRUSH_TAG: u64 = 0x9000_0000;
pub const DC_BRUSH: u64 = 0x7000_0012;
pub const NULL_BRUSH: u64 = 0x7000_0005;
#[derive(Clone, Copy, Debug)]
pub struct BrushState {
    pub selected: u64,
    pub dc_color: u32,
}
impl BrushState {
    pub const fn new() -> Self {
        Self {
            selected: SOLID_BRUSH_TAG | 0xffffff,
            dc_color: 0xffffff,
        }
    }
    pub fn accepts(h: u64) -> bool {
        h == DC_BRUSH || h == NULL_BRUSH || h & !0xffffff == SOLID_BRUSH_TAG
    }
    pub fn color(self) -> Option<u32> {
        if self.selected == NULL_BRUSH {
            None
        } else if self.selected == DC_BRUSH {
            Some(self.dc_color)
        } else {
            Some(self.selected as u32 & 0xffffff)
        }
    }
    pub fn select(&mut self, h: u64) -> Option<u64> {
        if !Self::accepts(h) {
            return None;
        }
        Some(core::mem::replace(&mut self.selected, h))
    }
    pub fn set_color(&mut self, color: u32) -> u32 {
        core::mem::replace(&mut self.dc_color, color & 0xffffff)
    }
}
pub const PEN_TAG: u64 = 0xa000_0000;
pub const DC_PEN: u64 = 0x7000_0013;
pub const NULL_PEN: u64 = 0x7000_0008;
#[derive(Clone, Copy, Debug)]
pub struct PenState {
    pub selected: u64,
    pub dc_color: u32,
    pub solid_color: u32,
}
impl PenState {
    pub const fn new() -> Self {
        Self {
            selected: PEN_TAG,
            dc_color: 0,
            solid_color: 0,
        }
    }
    pub fn accepts(h: u64) -> bool {
        matches!(h, DC_PEN | NULL_PEN) || h == PEN_TAG || h == (PEN_TAG | 0xffffff)
    }
    pub fn color(self) -> Option<u32> {
        if self.selected == NULL_PEN {
            None
        } else if self.selected == DC_PEN {
            Some(self.dc_color)
        } else {
            Some(self.solid_color)
        }
    }
    pub fn select(&mut self, h: u64) -> Option<u64> {
        if !Self::accepts(h) {
            return None;
        }
        if h == PEN_TAG || h == (PEN_TAG | 0xffffff) {
            self.solid_color = h as u32 & 0xffffff;
        }
        Some(core::mem::replace(&mut self.selected, h))
    }
    pub fn select_owned(&mut self, h: u64, color: u32) -> u64 {
        self.solid_color = color & 0xffffff;
        core::mem::replace(&mut self.selected, h)
    }
    pub fn set_color(&mut self, color: u32) -> u32 {
        core::mem::replace(&mut self.dc_color, color & 0xffffff)
    }
}
pub const STOCK_FONT_BASE: u64 = 0x7000_0100;
pub const DEFAULT_FONT: u64 = STOCK_FONT_BASE + 13;
pub fn stock_font(h: u64) -> bool {
    matches!(h.wrapping_sub(STOCK_FONT_BASE), 12 | 13 | 14 | 17)
}
#[derive(Clone, Copy, Debug)]
pub struct TextState {
    pub font: u64,
    pub foreground: u32,
    pub background: u32,
    pub mode: u32,
}
impl TextState {
    pub const fn new() -> Self {
        Self {
            font: DEFAULT_FONT,
            foreground: 0,
            background: 0xffffff,
            mode: 2,
        }
    }
    pub fn set(&mut self, kind: u32, value: u32) -> Option<u32> {
        match kind {
            0 => Some(core::mem::replace(&mut self.foreground, value & 0xffffff)),
            1 => Some(core::mem::replace(&mut self.background, value & 0xffffff)),
            2 if matches!(value, 1 | 2) => Some(core::mem::replace(&mut self.mode, value)),
            _ => None,
        }
    }
}
pub struct PenObject {
    pub owner: u64,
    pub color: u32,
}
pub struct FontObject {
    pub owner: u64,
    pub index: usize,
}
pub struct Bitmap {
    pub owner: u64,
    pub shape: Shape,
    pub pixels: PixelStore,
}
pub struct MemoryDc {
    pub owner: u64,
    pub thread: Option<u64>,
    pub bitmap: u64,
    pub brush: BrushState,
    pub text: TextState,
    pub pen: PenState,
}
pub struct Objects {
    next: u64,
    pub bitmaps: BTreeMap<u64, Bitmap>,
    pub dcs: BTreeMap<u64, MemoryDc>,
    pub fonts: BTreeMap<u64, FontObject>,
    pub pens: BTreeMap<u64, PenObject>,
}
impl Objects {
    pub const fn new() -> Self {
        Self {
            next: FIRST_HANDLE,
            bitmaps: BTreeMap::new(),
            dcs: BTreeMap::new(),
            fonts: BTreeMap::new(),
            pens: BTreeMap::new(),
        }
    }
    fn handle(&mut self, owner: u64) -> Option<u64> {
        if self.bitmaps.values().filter(|b| b.owner == owner).count()
            + self.dcs.values().filter(|d| d.owner == owner).count()
            + self.fonts.values().filter(|f| f.owner == owner).count()
            + self.pens.values().filter(|p| p.owner == owner).count()
            >= MAX_OBJECTS
        {
            return None;
        }
        let h = self.next;
        self.next = self.next.checked_add(1)?;
        Some(h)
    }
    pub fn create_dc(&mut self, owner: u64, thread: Option<u64>) -> u64 {
        let Some(h) = self.handle(owner) else {
            return 0;
        };
        self.dcs.insert(
            h,
            MemoryDc {
                owner,
                thread,
                bitmap: STOCK_BITMAP,
                brush: BrushState::new(),
                text: TextState::new(),
                pen: PenState::new(),
            },
        );
        h
    }
    pub fn create_bitmap(&mut self, owner: u64, w: i32, h: i32) -> u64 {
        if w < 0 || h < 0 {
            return 0;
        }
        if w == 0 || h == 0 {
            return STOCK_BITMAP;
        }
        let Some(n) = (w as usize).checked_mul(h as usize) else {
            return 0;
        };
        let Some(bytes) = n.checked_mul(4) else {
            return 0;
        };
        let used: usize = self
            .bitmaps
            .values()
            .filter(|b| b.owner == owner)
            .map(|b| b.pixels.len() * 4)
            .sum();
        if bytes > MAX_BYTES.saturating_sub(used) {
            return 0;
        }
        let Some(handle) = self.handle(owner) else {
            return 0;
        };
        let Some(pixels) = PixelStore::zeroed(n) else {
            return 0;
        };
        self.bitmaps.insert(
            handle,
            Bitmap {
                owner,
                shape: Shape {
                    width: w as u32,
                    height: h as u32,
                    stride: w as usize,
                },
                pixels,
            },
        );
        handle
    }
    pub fn dc(&self, owner: u64, h: u64) -> Option<&MemoryDc> {
        self.dcs.get(&h).filter(|d| d.owner == owner)
    }
    pub fn dc_mut(&mut self, owner: u64, h: u64) -> Option<&mut MemoryDc> {
        self.dcs.get_mut(&h).filter(|d| d.owner == owner)
    }
    pub fn bitmap(&self, owner: u64, dc: u64) -> Option<&Bitmap> {
        let h = self.dc(owner, dc)?.bitmap;
        self.bitmaps.get(&h).filter(|b| b.owner == owner)
    }
    pub fn bitmap_mut(&mut self, owner: u64, dc: u64) -> Option<&mut Bitmap> {
        let h = self.dc(owner, dc)?.bitmap;
        self.bitmaps.get_mut(&h).filter(|b| b.owner == owner)
    }
    pub fn select_bitmap(&mut self, owner: u64, dc: u64, h: u64) -> u64 {
        let Some(old) = self.dc(owner, dc).map(|d| d.bitmap) else {
            return 0;
        };
        if h != STOCK_BITMAP {
            if !self.bitmaps.get(&h).is_some_and(|b| b.owner == owner) {
                return 0;
            }
            if self
                .dcs
                .iter()
                .any(|(other, d)| *other != dc && d.bitmap == h)
            {
                return 0;
            }
        }
        self.dc_mut(owner, dc).unwrap().bitmap = h;
        old
    }
    pub fn delete_dc(&mut self, owner: u64, dc: u64) -> bool {
        if self.dc(owner, dc).is_none() {
            return false;
        }
        self.dcs.remove(&dc);
        true
    }
    pub fn delete_bitmap(&mut self, owner: u64, h: u64) -> bool {
        if h == STOCK_BITMAP {
            return true;
        }
        if !self.bitmaps.get(&h).is_some_and(|b| b.owner == owner)
            || self.dcs.values().any(|d| d.bitmap == h)
        {
            return false;
        }
        self.bitmaps.remove(&h);
        true
    }
    pub fn create_pen(&mut self, owner: u64, color: u32) -> u64 {
        let Some(h) = self.handle(owner) else {
            return 0;
        };
        self.pens.insert(
            h,
            PenObject {
                owner,
                color: color & 0xffffff,
            },
        );
        h
    }
    pub fn pen_color(&self, owner: u64, h: u64) -> Option<u32> {
        self.pens
            .get(&h)
            .filter(|p| p.owner == owner)
            .map(|p| p.color)
    }
    pub fn delete_pen(&mut self, owner: u64, h: u64) -> bool {
        if self.pen_color(owner, h).is_none() || self.dcs.values().any(|d| d.pen.selected == h) {
            return false;
        }
        self.pens.remove(&h);
        true
    }
    pub fn create_font(&mut self, owner: u64, index: usize) -> u64 {
        let Some(h) = self.handle(owner) else {
            return 0;
        };
        self.fonts.insert(h, FontObject { owner, index });
        h
    }
    pub fn font_index(&self, owner: u64, h: u64) -> Option<usize> {
        if stock_font(h) {
            return Some(4);
        }
        self.fonts
            .get(&h)
            .filter(|f| f.owner == owner)
            .map(|f| f.index)
    }
    pub fn delete_font(&mut self, owner: u64, h: u64) -> bool {
        if stock_font(h) {
            return true;
        }
        if !self.fonts.get(&h).is_some_and(|f| f.owner == owner)
            || self.dcs.values().any(|d| d.text.font == h)
        {
            return false;
        }
        self.fonts.remove(&h);
        true
    }
    pub fn cleanup_process(&mut self, owner: u64) {
        self.dcs.retain(|_, d| d.owner != owner);
        self.bitmaps.retain(|_, b| b.owner != owner);
        self.fonts.retain(|_, f| f.owner != owner);
        self.pens.retain(|_, p| p.owner != owner);
    }
    pub fn cleanup_thread(&mut self, tid: u64) {
        self.dcs.retain(|_, d| d.thread != Some(tid));
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn owned_pen_lifetime_and_process_cleanup() {
        let mut t = Objects::new();
        let p = t.create_pen(1, 0x123456);
        let d = t.create_dc(1, None);
        assert_eq!(t.pen_color(2, p), None);
        assert_eq!(t.pen_color(1, p), Some(0x123456));
        t.dc_mut(1, d).unwrap().pen.select_owned(p, 0x123456);
        assert!(!t.delete_pen(1, p));
        t.dc_mut(1, d).unwrap().pen.select(PEN_TAG);
        assert!(t.delete_pen(1, p));
        assert!(!t.delete_pen(1, p));
        let p = t.create_pen(1, 7);
        t.cleanup_process(1);
        assert!(t.pens.is_empty());
        assert_eq!(t.pen_color(1, p), None);
    }
    #[test]
    fn pen_and_hollow_brush_state() {
        let mut p = PenState::new();
        assert_eq!(p.color(), Some(0));
        assert_eq!(p.set_color(0x123456), 0);
        assert_eq!(p.color(), Some(0));
        assert_eq!(p.select(DC_PEN), Some(PEN_TAG));
        assert_eq!(p.color(), Some(0x123456));
        assert_eq!(p.select(NULL_PEN), Some(DC_PEN));
        assert_eq!(p.color(), None);
        assert_eq!(p.select(SOLID_BRUSH_TAG), None);
        let mut b = BrushState::new();
        assert_eq!(b.select(NULL_BRUSH), Some(SOLID_BRUSH_TAG | 0xffffff));
        assert_eq!(b.color(), None);
        assert_eq!(b.set_color(9), 0xffffff);
        b.select(DC_BRUSH);
        assert_eq!(b.color(), Some(9));
    }
    #[test]
    fn font_ownership_selection_and_text_state() {
        let mut t = Objects::new();
        let f = t.create_font(1, 7);
        let dc = t.create_dc(1, None);
        assert_eq!(t.font_index(2, f), None);
        assert_eq!(t.font_index(1, f), Some(7));
        t.dc_mut(1, dc).unwrap().text.font = f;
        assert!(!t.delete_font(1, f));
        assert_eq!(t.dc_mut(1, dc).unwrap().text.set(0, 0x112233), Some(0));
        assert_eq!(t.dc_mut(1, dc).unwrap().text.set(2, 99), None);
        assert_eq!(t.dc(1, dc).unwrap().text.mode, 2);
        t.dc_mut(1, dc).unwrap().text.font = DEFAULT_FONT;
        assert!(t.delete_font(1, f));
        assert!(!t.delete_font(1, f));
        let f = t.create_font(1, 9);
        t.cleanup_process(1);
        assert!(t.fonts.is_empty());
        assert_eq!(t.font_index(1, f), None);
        assert!(t.font_index(2, DEFAULT_FONT).is_some());
    }
    #[test]
    fn dc_brush_color_survives_selection() {
        let mut a = BrushState::new();
        let mut b = BrushState::new();
        assert_eq!(a.set_color(0x123456), 0xffffff);
        assert_eq!(a.color(), Some(0xffffff));
        assert_eq!(a.select(DC_BRUSH), Some(SOLID_BRUSH_TAG | 0xffffff));
        assert_eq!(a.color(), Some(0x123456));
        assert_eq!(a.select(SOLID_BRUSH_TAG | 7), Some(DC_BRUSH));
        assert_eq!(a.set_color(0xffaabbcc), 0x123456);
        assert_eq!(a.color(), Some(7));
        a.select(DC_BRUSH);
        assert_eq!(a.color(), Some(0xaabbcc));
        assert_eq!(a.select(0), None);
        assert_eq!(a.color(), Some(0xaabbcc));
        b.select(DC_BRUSH);
        assert_eq!(b.color(), Some(0xffffff));
    }
    #[test]
    fn ownership_selection_lifetime() {
        let mut t = Objects::new();
        let a = t.create_dc(1, Some(11));
        let b = t.create_dc(1, None);
        let foreign = t.create_dc(2, None);
        let bm = t.create_bitmap(1, 8, 8);
        assert_eq!(t.select_bitmap(2, foreign, bm), 0);
        assert_eq!(t.select_bitmap(1, a, bm), STOCK_BITMAP);
        assert_eq!(t.select_bitmap(1, b, bm), 0);
        assert!(!t.delete_bitmap(1, bm));
        assert!(!t.delete_dc(2, a));
        assert_eq!(t.select_bitmap(1, a, STOCK_BITMAP), bm);
        assert_eq!(t.select_bitmap(1, b, bm), STOCK_BITMAP);
        t.cleanup_thread(11);
        assert!(t.dc(1, a).is_none());
        assert!(t.dc(1, b).is_some());
        assert!(t.delete_dc(1, b));
        assert!(t.delete_bitmap(1, bm));
        assert!(!t.delete_bitmap(1, bm));
        assert_ne!(t.create_bitmap(1, 8, 8), bm);
        t.cleanup_process(1);
        assert_eq!(t.bitmaps.len(), 0);
        assert_eq!(t.dcs.len(), 1);
    }
    #[test]
    fn budgets_and_invalid_dimensions() {
        let mut t = Objects::new();
        assert_eq!(t.create_bitmap(1, -1, 1), 0);
        assert_eq!(t.create_bitmap(1, i32::MAX, i32::MAX), 0);
        assert_eq!(t.create_bitmap(1, 0, 10), STOCK_BITMAP);
        assert_ne!(t.create_bitmap(1, 4096, 4096), 0);
        assert_eq!(t.create_bitmap(1, 1, 1), 0);
        t.cleanup_process(1);
        assert_ne!(t.create_bitmap(1, 1, 1), 0);
    }
    #[test]
    fn paged_store_and_reclaim() {
        let mut t = Objects::new();
        let dc = t.create_dc(1, None);
        let bm = t.create_bitmap(1, 37, 41);
        t.select_bitmap(1, dc, bm);
        let b = t.bitmap_mut(1, dc).unwrap();
        for i in 0..b.pixels.len() {
            b.pixels.write(i, i as u32);
        }
        b.pixels.fill(1000, 200, 0x123456);
        for i in 0..1517 {
            assert_eq!(
                b.pixels.read(i),
                if (1000..1200).contains(&i) {
                    0x123456
                } else {
                    i as u32
                }
            );
        }
        assert!(b.pixels.ptr_at(1517).is_none());
        t.cleanup_process(1);
        assert!(t.bitmaps.is_empty());
        assert!(t.dcs.is_empty());
    }
}
