// SPDX-License-Identifier: GPL-2.0-or-later
//! GDI drawing to the boot framebuffer, composited window backing stores,
//! and owned memory bitmaps. Coordinates are DC-relative; window damage is
//! delivered to the existing display server without changing its effects.
//! Only the explicitly implemented color/SRCCOPY subset is supported.

use crate::gdi_memory::{BrushState, PenState};
use spin::{Mutex, Once};
static MEMORY: Mutex<crate::gdi_memory::Objects> = Mutex::new(crate::gdi_memory::Objects::new());
fn owner() -> u64 {
    crate::process::current_pid()
}
fn is_memory(h: u64) -> bool {
    h >= 0x4000_0000_0000_0000
}
pub fn cleanup_process(pid: u64) {
    let mut m = MEMORY.lock();
    crate::window::reset_fonts(|h| m.fonts.get(&h).is_some_and(|f| f.owner == pid));
    {
        let mut state = SCREEN_TEXT.lock();
        if m.fonts.get(&state.font).is_some_and(|f| f.owner == pid) {
            state.font = crate::gdi_memory::DEFAULT_FONT;
        }
    }
    crate::window::reset_pens(|h| m.pens.get(&h).is_some_and(|p| p.owner == pid));
    {
        let mut state = SCREEN_PEN.lock();
        if m.pens.get(&state.selected).is_some_and(|p| p.owner == pid) {
            *state = PenState::new();
        }
    }
    m.cleanup_process(pid);
}
pub fn cleanup_thread(tid: u64) {
    MEMORY.lock().cleanup_thread(tid);
}

struct FbInfo {
    virt: u64,
    phys: u64,
    width: u32,
    height: u32,
    pitch: u32,
    r_shift: u8,
    g_shift: u8,
    b_shift: u8,
}

static FB: Once<FbInfo> = Once::new();

/// Tag bit OR-ed into a window's `hwnd` to make a `GetDC(hwnd)` handle —
/// distinguishes a window DC from the fixed screen DC (`1`) in every
/// drawing call. `hwnd`s are small (`window::NEXT_HWND` starts at 1), so
/// this bit is always free.
pub const WINDOW_DC_TAG: u64 = 0x8000_0000;

// The one legacy screen DC needs only one colour. Window DC selection lives
// in Window and disappears automatically on destruction/process cleanup.
static SCREEN_BRUSH: Mutex<BrushState> = Mutex::new(BrushState::new());
static SCREEN_PEN: Mutex<PenState> = Mutex::new(PenState::new());
static SCREEN_TEXT: Mutex<crate::gdi_memory::TextState> =
    Mutex::new(crate::gdi_memory::TextState::new());

pub const SM_CXSCREEN: i64 = 0;
pub const SM_CYSCREEN: i64 = 1;

/// Map the boot framebuffer into THOS's own page tables — `vmm::map_mmio`
/// derives the virtual address from the same HHDM offset Limine used for
/// `fb.address()`, so this lands at the identical VA, just backed by THOS's
/// own PTEs — and record its geometry. Call once, after `vmm::init` (needs
/// `vmm::kernel_pml4_phys`). Only 32bpp is supported so far — that is what
/// every target THOS boots on today (QEMU, real hardware EFI GOP) reports.
pub fn init(fb: &limine::framebuffer::Framebuffer, hhdm: u64) {
    if fb.bpp != 32 {
        crate::kprintln!(
            "THOS: gdi FAIL         framebuffer is {}bpp, only 32bpp supported",
            fb.bpp
        );
        return;
    }
    let phys = fb.address() as u64 - hhdm;
    let len = fb.pitch * fb.height;
    let virt = crate::vmm::map_framebuffer(phys, len);
    FB.call_once(|| FbInfo {
        virt,
        phys,
        width: fb.width as u32,
        height: fb.height as u32,
        pitch: fb.pitch as u32,
        r_shift: fb.red_mask_shift,
        g_shift: fb.green_mask_shift,
        b_shift: fb.blue_mask_shift,
    });
    crate::kprintln!(
        "THOS: gdi ok           {}x{} @ 32bpp mapped for GDI32/User32",
        fb.width,
        fb.height
    );
}

fn fb() -> Option<&'static FbInfo> {
    FB.get()
}

/// Geometry of the boot framebuffer for `/dev/fb0`: `(width, height, pitch, r_shift, g_shift, b_shift)`.
pub fn fb_geometry() -> Option<(u32, u32, u32, u8, u8, u8)> {
    let f = fb()?;
    Some((f.width, f.height, f.pitch, f.r_shift, f.g_shift, f.b_shift))
}

/// `(physical base, length)` of the framebuffer, for `mmap` of `/dev/fb0`.
pub fn fb_phys() -> Option<(u64, u64)> {
    let f = fb()?;
    Some((f.phys, f.pitch as u64 * f.height as u64))
}

/// Copy `data` into the framebuffer at byte offset `off` (clipped to its size); bytes written.
pub fn fb_write(off: usize, data: &[u8]) -> usize {
    let Some(f) = fb() else { return 0 };
    let size = f.pitch as usize * f.height as usize;
    if off >= size {
        return 0;
    }
    let n = data.len().min(size - off);
    unsafe { core::ptr::copy_nonoverlapping(data.as_ptr(), (f.virt as usize + off) as *mut u8, n) };
    n
}

/// Copy framebuffer bytes at `off` into `out`; bytes read.
pub fn fb_read(off: usize, out: &mut [u8]) -> usize {
    let Some(f) = fb() else { return 0 };
    let size = f.pitch as usize * f.height as usize;
    if off >= size {
        return 0;
    }
    let n = out.len().min(size - off);
    unsafe {
        core::ptr::copy_nonoverlapping((f.virt as usize + off) as *const u8, out.as_mut_ptr(), n)
    };
    n
}

/// `GetSystemMetrics(SM_CXSCREEN|SM_CYSCREEN)`'s backing data.
pub fn screen_size() -> (u32, u32) {
    fb().map_or((0, 0), |f| (f.width, f.height))
}

/// `COLORREF` (`0x00BBGGRR`) → this framebuffer's native 32-bit pixel.
fn pack(colorref: u32) -> u32 {
    let Some(f) = fb() else { return 0 };
    let r = colorref & 0xFF;
    let g = (colorref >> 8) & 0xFF;
    let b = (colorref >> 16) & 0xFF;
    (r << f.r_shift) | (g << f.g_shift) | (b << f.b_shift)
}

/// The inverse of [`pack`] — native pixel → `COLORREF`, for `GetPixel`.
fn unpack(native: u32) -> u32 {
    let Some(f) = fb() else { return 0 };
    let r = (native >> f.r_shift) & 0xFF;
    let g = (native >> f.g_shift) & 0xFF;
    let b = (native >> f.b_shift) & 0xFF;
    r | (g << 8) | (b << 16)
}

fn ptr_at(x: u32, y: u32) -> Option<*mut u32> {
    let f = fb()?;
    if x >= f.width || y >= f.height {
        return None;
    }
    Some((f.virt + y as u64 * f.pitch as u64 + x as u64 * 4) as *mut u32)
}

/// A DC's resolved drawing bounds: where its own `(0, 0)` lands on screen,
/// and the screen rectangle drawing through it must stay inside (already
/// intersected with the screen, so callers never need to check separately).
struct DcBounds {
    origin: (i32, i32),
    clip: (i32, i32, i32, i32), // left, top, right, bottom — screen coords, exclusive
}

fn window_dc_id(dc: u64) -> Option<u32> {
    (dc <= u32::MAX as u64 && dc & WINDOW_DC_TAG != 0).then_some((dc & !WINDOW_DC_TAG) as u32)
}
fn resolve_dc(dc: u64) -> Option<DcBounds> {
    let (w, h) = screen_size();
    if dc == 1 {
        return Some(DcBounds {
            origin: (0, 0),
            clip: (0, 0, w as i32, h as i32),
        });
    }
    let hwnd = window_dc_id(dc)?;
    let (x, y, cw, ch) = crate::window::rect_of(hwnd)?;
    Some(DcBounds {
        origin: (x, y),
        clip: (
            x.max(0),
            y.max(0),
            (x + cw).min(w as i32),
            (y + ch).min(h as i32),
        ),
    })
}

/// `SetPixel(hdc, x, y, colorref)` — `x`/`y` are client-relative for a
/// window DC. `0xFFFF_FFFF` (`CLR_INVALID`) outside the DC's clip rect.
/// The backing store of a window DC while a display server composes (`window::store_of`).
fn store_dc(dc: u64) -> Option<(alloc::sync::Arc<crate::process::Section>, i32, i32, u32)> {
    let hwnd = window_dc_id(dc)?;
    let (s, w, h) = crate::window::store_of(hwnd)?;
    Some((s, w, h, hwnd))
}

pub fn set_pixel(dc: u64, x: i64, y: i64, colorref: u32) -> u32 {
    if is_memory(dc) {
        let mut m = MEMORY.lock();
        let Some(b) = m.bitmap_mut(owner(), dc) else {
            return u32::MAX;
        };
        if x < 0 || y < 0 || x >= b.shape.width as i64 || y >= b.shape.height as i64 {
            return u32::MAX;
        }
        b.pixels
            .write(y as usize * b.shape.stride + x as usize, pack(colorref));
        return colorref & 0xffffff;
    }
    if let Some((s, w, h, hwnd)) = store_dc(dc) {
        if x < 0 || y < 0 || x >= w as i64 || y >= h as i64 {
            return u32::MAX;
        }
        if let Some(p) = s.ptr_at((y as usize * w as usize + x as usize) * 4) {
            unsafe { (p as *mut u32).write_volatile(pack(colorref)) };
            crate::window::damage(hwnd, x as i32, y as i32, 1, 1);
        }
        return colorref;
    }
    let Some(b) = resolve_dc(dc) else {
        return u32::MAX;
    };
    let (sx, sy) = (b.origin.0 as i64 + x, b.origin.1 as i64 + y);
    let (l, t, r, bot) = b.clip;
    if sx < l as i64 || sy < t as i64 || sx >= r as i64 || sy >= bot as i64 {
        return u32::MAX;
    }
    match ptr_at(sx as u32, sy as u32) {
        Some(p) => {
            unsafe { p.write_volatile(pack(colorref)) };
            colorref
        }
        None => u32::MAX,
    }
}

/// `GetPixel(hdc, x, y)` — the inverse of [`set_pixel`]'s coordinate mapping.
pub fn get_pixel(dc: u64, x: i64, y: i64) -> u32 {
    if is_memory(dc) {
        let m = MEMORY.lock();
        let Some(b) = m.bitmap(owner(), dc) else {
            return u32::MAX;
        };
        if x < 0 || y < 0 || x >= b.shape.width as i64 || y >= b.shape.height as i64 {
            return u32::MAX;
        }
        return unpack(b.pixels.read(y as usize * b.shape.stride + x as usize));
    }
    if let Some((s, w, h, _)) = store_dc(dc) {
        if x < 0 || y < 0 || x >= w as i64 || y >= h as i64 {
            return u32::MAX;
        }
        return s
            .ptr_at((y as usize * w as usize + x as usize) * 4)
            .map_or(u32::MAX, |p| {
                unpack(unsafe { (p as *mut u32).read_volatile() })
            });
    }
    let Some(b) = resolve_dc(dc) else {
        return u32::MAX;
    };
    let (sx, sy) = (b.origin.0 as i64 + x, b.origin.1 as i64 + y);
    let (l, t, r, bot) = b.clip;
    if sx < l as i64 || sy < t as i64 || sx >= r as i64 || sy >= bot as i64 {
        return u32::MAX;
    }
    match ptr_at(sx as u32, sy as u32) {
        Some(p) => unpack(unsafe { p.read_volatile() }),
        None => u32::MAX,
    }
}

/// `Rectangle(hdc, left, top, right, bottom)`: fill `[left,right) x
/// [top,bottom)`, client-relative for a window DC, with the DC's current
/// brush colour, clamped to its clip rect. `false` if the clamped rectangle
/// is empty — otherwise `true`, matching real GDI's BOOL.
pub fn fill_rect(dc: u64, left: i64, top: i64, right: i64, bottom: i64) -> bool {
    let Some(color) = brush_of(dc).and_then(|b| b.color()) else {
        return false;
    };
    fill_color_rect(dc, left, top, right, bottom, color)
}
fn fill_color_rect(dc: u64, left: i64, top: i64, right: i64, bottom: i64, color: u32) -> bool {
    let color = pack(color);
    if is_memory(dc) {
        let mut m = MEMORY.lock();
        let Some(b) = m.bitmap_mut(owner(), dc) else {
            return false;
        };
        let (l, t, r, bot) = (
            left.max(0),
            top.max(0),
            right.min(b.shape.width as i64),
            bottom.min(b.shape.height as i64),
        );
        if l >= r || t >= bot {
            return false;
        }
        for y in t..bot {
            b.pixels.fill(
                y as usize * b.shape.stride + l as usize,
                (r - l) as usize,
                color,
            );
        }
        return true;
    }
    if let Some((s, w, h, hwnd)) = store_dc(dc) {
        let (l, t) = (left.max(0), top.max(0));
        let (r, b) = (right.min(w as i64), bottom.min(h as i64));
        if l >= r || t >= b {
            return false;
        }
        for y in t..b {
            for x in l..r {
                if let Some(p) = s.ptr_at((y as usize * w as usize + x as usize) * 4) {
                    unsafe { (p as *mut u32).write_volatile(color) };
                }
            }
        }
        crate::window::damage(hwnd, l as i32, t as i32, (r - l) as i32, (b - t) as i32);
        return true;
    }
    if fb().is_none() {
        return false;
    }
    let Some(b) = resolve_dc(dc) else {
        return false;
    };
    let (ox, oy) = (b.origin.0 as i64, b.origin.1 as i64);
    let (cl, ct, cr, cb) = b.clip;
    let l = (ox + left).max(cl as i64).max(0) as u32;
    let t = (oy + top).max(ct as i64).max(0) as u32;
    let r = ((ox + right).min(cr as i64).max(0)) as u32;
    let bot = ((oy + bottom).min(cb as i64).max(0)) as u32;
    if l >= r || t >= bot {
        return false;
    }
    let f = fb().unwrap();
    for y in t..bot {
        let row = (f.virt + y as u64 * f.pitch as u64) as *mut u32;
        for x in l..r {
            unsafe { row.add(x as usize).write_volatile(color) };
        }
    }
    true
}

fn brush_of(dc: u64) -> Option<BrushState> {
    if is_memory(dc) {
        return MEMORY.lock().dc(owner(), dc).map(|d| d.brush);
    }
    if dc == 1 {
        return Some(*SCREEN_BRUSH.lock());
    }
    crate::window::window_brush(window_dc_id(dc)?)
}
const BRUSH_TAG: u64 = crate::gdi_memory::SOLID_BRUSH_TAG;
pub fn create_solid_brush(colorref: u32) -> u64 {
    BRUSH_TAG | (colorref & 0xffffff) as u64
}
/// Implemented stock brushes and DEFAULT_BITMAP; unsupported object types
/// fail explicitly instead of returning a white brush under a different type.
pub fn get_stock_object(i: i64) -> u64 {
    match i {
        0 => create_solid_brush(0xffffff),
        1 => create_solid_brush(0xc0c0c0),
        2 => create_solid_brush(0x808080),
        3 => create_solid_brush(0x404040),
        4 => create_solid_brush(0),
        5 => crate::gdi_memory::NULL_BRUSH,
        6 => crate::gdi_memory::PEN_TAG | 0xffffff,
        7 => crate::gdi_memory::PEN_TAG,
        8 => crate::gdi_memory::NULL_PEN,
        19 => crate::gdi_memory::DC_PEN,
        12 | 13 | 14 | 17 => crate::gdi_memory::STOCK_FONT_BASE + i as u64,
        18 => crate::gdi_memory::DC_BRUSH,
        21 => crate::gdi_memory::STOCK_BITMAP,
        _ => 0,
    }
}
pub fn select_object(dc: u64, hobj: u64) -> u64 {
    {
        let mut m = MEMORY.lock();
        if let Some(color) = m.pen_color(owner(), hobj) {
            if is_memory(dc) {
                return m
                    .dc_mut(owner(), dc)
                    .map_or(0, |d| d.pen.select_owned(hobj, color));
            }
            if dc == 1 {
                return SCREEN_PEN.lock().select_owned(hobj, color);
            }
            return window_dc_id(dc)
                .and_then(|hwnd| crate::window::replace_window_owned_pen(hwnd, hobj, color))
                .unwrap_or(0);
        }
    }
    if PenState::accepts(hobj) {
        if is_memory(dc) {
            return MEMORY
                .lock()
                .dc_mut(owner(), dc)
                .and_then(|d| d.pen.select(hobj))
                .unwrap_or(0);
        }
        if dc == 1 {
            return SCREEN_PEN.lock().select(hobj).unwrap_or(0);
        }
        return window_dc_id(dc)
            .and_then(|hwnd| crate::window::replace_window_pen(hwnd, hobj))
            .unwrap_or(0);
    }
    {
        let mut m = MEMORY.lock();
        let pid = owner();
        if m.font_index(pid, hobj).is_some() {
            if is_memory(dc) {
                return m
                    .dc_mut(pid, dc)
                    .map_or(0, |d| core::mem::replace(&mut d.text.font, hobj));
            }
            if dc == 1 {
                return core::mem::replace(&mut SCREEN_TEXT.lock().font, hobj);
            }
            return window_dc_id(dc)
                .and_then(|hwnd| crate::window::replace_window_font(hwnd, hobj))
                .unwrap_or(0);
        }
    }
    if is_memory(dc) {
        let mut m = MEMORY.lock();
        let pid = owner();
        if BrushState::accepts(hobj) {
            return m
                .dc_mut(pid, dc)
                .and_then(|d| d.brush.select(hobj))
                .unwrap_or(0);
        }
        return m.select_bitmap(pid, dc, hobj);
    }
    if dc == 1 {
        return SCREEN_BRUSH.lock().select(hobj).unwrap_or(0);
    }
    window_dc_id(dc)
        .and_then(|hwnd| crate::window::replace_window_brush(hwnd, hobj))
        .unwrap_or(0)
}
pub fn get_dc_brush_color(dc: u64) -> u32 {
    brush_of(dc).map_or(u32::MAX, |b| b.dc_color)
}
pub fn set_dc_brush_color(dc: u64, color: u32) -> u32 {
    if is_memory(dc) {
        return MEMORY
            .lock()
            .dc_mut(owner(), dc)
            .map_or(u32::MAX, |d| d.brush.set_color(color));
    }
    if dc == 1 {
        return SCREEN_BRUSH.lock().set_color(color);
    }
    window_dc_id(dc)
        .and_then(|hwnd| crate::window::set_dc_brush_color(hwnd, color))
        .unwrap_or(u32::MAX)
}
/// FillRect's explicit brush does not change either selection or DC color.
pub fn fill_with_brush(dc: u64, rect: [i32; 4], brush: u64) -> bool {
    if !BrushState::accepts(brush) {
        return false;
    }
    if brush == crate::gdi_memory::NULL_BRUSH {
        return valid_dc(dc);
    }
    let Some(state) = brush_of(dc) else {
        return false;
    };
    let color = if brush == crate::gdi_memory::DC_BRUSH {
        state.dc_color
    } else {
        brush as u32 & 0xffffff
    };
    fill_color_rect(
        dc,
        rect[0] as i64,
        rect[1] as i64,
        rect[2] as i64,
        rect[3] as i64,
        color,
    )
}

/// Color-compatible DCs/bitmaps only. A stock monochrome selection can be
/// restored but is not rasterized or silently treated as a color bitmap.
pub fn create_compatible_dc(dc: u64) -> u64 {
    if dc != 0 && !valid_dc(dc) {
        return 0;
    }
    MEMORY.lock().create_dc(
        owner(),
        if dc == 0 {
            Some(crate::sched::current().id)
        } else {
            None
        },
    )
}
fn valid_dc(dc: u64) -> bool {
    if is_memory(dc) {
        MEMORY.lock().dc(owner(), dc).is_some()
    } else {
        resolve_dc(dc).is_some()
    }
}
pub fn create_compatible_bitmap(dc: u64, w: i32, h: i32) -> u64 {
    if !valid_dc(dc) {
        return 0;
    }
    let mut m = MEMORY.lock();
    if is_memory(dc) && m.bitmap(owner(), dc).is_none() && w != 0 && h != 0 {
        return 0;
    }
    m.create_bitmap(owner(), w, h)
}
pub fn delete_dc(dc: u64) -> bool {
    MEMORY.lock().delete_dc(owner(), dc)
}
pub fn delete_object(h: u64) -> bool {
    if BrushState::accepts(h) || PenState::accepts(h) || crate::gdi_memory::stock_font(h) {
        return true;
    }
    let mut m = MEMORY.lock();
    let pid = owner();
    if m.dc(pid, h).is_some() {
        return m.delete_dc(pid, h);
    }
    if m.pen_color(pid, h).is_some() {
        if crate::window::pen_selected(h) || SCREEN_PEN.lock().selected == h {
            return false;
        }
        return m.delete_pen(pid, h);
    }
    if m.font_index(pid, h).is_some() {
        if crate::window::font_selected(h) || SCREEN_TEXT.lock().font == h {
            return false;
        }
        return m.delete_font(pid, h);
    }
    m.delete_bitmap(pid, h)
}

// Captured surfaces keep window sections alive across a concurrent resize.
// Memory and window sections use the same page-aware copy path. Section
// accesses never cross a physical page boundary.
enum Pixels {
    Linear(*mut u32),
    Section(alloc::sync::Arc<crate::process::Section>),
}
struct Surface {
    shape: crate::gdi_blit::Shape,
    pixels: Pixels,
    hwnd: Option<u32>,
}
impl Surface {
    fn chunk(&self, x: usize, y: usize, backward: bool) -> (*mut u32, usize) {
        let off = y * self.shape.stride + x;
        match &self.pixels {
            Pixels::Linear(p) => (
                unsafe { p.add(off) },
                if backward {
                    x + 1
                } else {
                    self.shape.width as usize - x
                },
            ),
            Pixels::Section(s) => {
                let byte = off * 4;
                let p = s.ptr_at(byte).unwrap() as *mut u32;
                (
                    p,
                    if backward {
                        (byte % 4096) / 4 + 1
                    } else {
                        (4096 - byte % 4096) / 4
                    },
                )
            }
        }
    }
}
fn surface(dc: u64, m: &mut crate::gdi_memory::Objects) -> Option<Surface> {
    use crate::gdi_blit::Shape;
    if is_memory(dc) {
        let b = m.bitmap(owner(), dc)?;
        return Some(Surface {
            shape: b.shape,
            pixels: Pixels::Section(b.pixels.0.clone()),
            hwnd: None,
        });
    }
    if let Some((s, w, h, hwnd)) = store_dc(dc) {
        return Some(Surface {
            shape: Shape {
                width: w as u32,
                height: h as u32,
                stride: w as usize,
            },
            pixels: Pixels::Section(s),
            hwnd: Some(hwnd),
        });
    }
    if dc == 1 {
        let f = fb()?;
        return Some(Surface {
            shape: Shape {
                width: f.width,
                height: f.height,
                stride: f.pitch as usize / 4,
            },
            pixels: Pixels::Linear(f.virt as *mut u32),
            hwnd: None,
        });
    }
    None
}
/// Native-format SRCCOPY, with destination clipping, validated source and overlap-safe row
/// copies, no per-blit buffer allocation and one damage event for the region.
pub fn bit_blt(
    dst: u64,
    dx: i32,
    dy: i32,
    w: i32,
    h: i32,
    src: u64,
    sx: i32,
    sy: i32,
    rop: u32,
) -> bool {
    if rop != 0x00cc0020 || w < 0 || h < 0 {
        return false;
    }
    let mut m = MEMORY.lock();
    let Some(d) = surface(dst, &mut m) else {
        return false;
    };
    let Some(s) = surface(src, &mut m) else {
        return false;
    };
    let r = match crate::gdi_blit::clip_destination(d.shape, s.shape, dx, dy, sx, sy, w, h) {
        Ok(Some(r)) => r,
        Ok(None) => return true,
        Err(()) => return false,
    };
    let same = match (&d.pixels, &s.pixels) {
        (Pixels::Linear(a), Pixels::Linear(b)) => a == b,
        (Pixels::Section(a), Pixels::Section(b)) => alloc::sync::Arc::ptr_eq(a, b),
        _ => false,
    };
    let backwards = same && (r.dy > r.sy || (r.dy == r.sy && r.dx > r.sx));
    unsafe {
        crate::gdi_blit::copy_paged(
            r,
            backwards,
            |x, y, back| d.chunk(x, y, back),
            |x, y, back| s.chunk(x, y, back),
        );
    }
    drop(m);
    if let Some(hwnd) = d.hwnd {
        crate::window::damage(
            hwnd,
            r.dx as i32,
            r.dy as i32,
            r.width as i32,
            r.height as i32,
        );
    }
    true
}

/// Constant-alpha AC_SRC_OVER for equally sized native color surfaces.
/// Scaling and per-pixel alpha are explicitly unsupported. Validate all
/// parameters before mutation, including the full source and self overlap.
pub fn alpha_blend(
    dst: u64,
    dx: i32,
    dy: i32,
    w: i32,
    h: i32,
    src: u64,
    sx: i32,
    sy: i32,
    sw: i32,
    sh: i32,
    blend: u32,
) -> bool {
    if blend & 0xff00ffff != 0 || w <= 0 || h <= 0 || sw != w || sh != h {
        return false;
    }
    let mut m = MEMORY.lock();
    let Some(d) = surface(dst, &mut m) else {
        return false;
    };
    let Some(s) = surface(src, &mut m) else {
        return false;
    };
    if sx < 0
        || sy < 0
        || sx as i64 + sw as i64 > s.shape.width as i64
        || sy as i64 + sh as i64 > s.shape.height as i64
    {
        return false;
    }
    let same = match (&d.pixels, &s.pixels) {
        (Pixels::Linear(a), Pixels::Linear(b)) => a == b,
        (Pixels::Section(a), Pixels::Section(b)) => alloc::sync::Arc::ptr_eq(a, b),
        _ => false,
    };
    if same
        && (dx as i64) < sx as i64 + w as i64
        && (sx as i64) < dx as i64 + w as i64
        && (dy as i64) < sy as i64 + h as i64
        && (sy as i64) < dy as i64 + h as i64
    {
        return false;
    }
    let r = match crate::gdi_blit::clip_destination(d.shape, s.shape, dx, dy, sx, sy, w, h) {
        Ok(Some(r)) => r,
        Ok(None) => return true,
        Err(()) => return false,
    };
    let alpha = (blend >> 16) as u8;
    if alpha == 0 {
        return true;
    }
    if alpha == 255 {
        unsafe {
            crate::gdi_blit::copy_paged(
                r,
                false,
                |x, y, back| d.chunk(x, y, back),
                |x, y, back| s.chunk(x, y, back),
            );
        }
    } else {
        for row in 0..r.height {
            let mut done = 0;
            while done < r.width {
                let (dp, dn) = d.chunk(r.dx + done, r.dy + row, false);
                let (sp, sn) = s.chunk(r.sx + done, r.sy + row, false);
                let n = (r.width - done).min(dn).min(sn);
                for i in 0..n {
                    unsafe {
                        let dc = unpack(dp.add(i).read_volatile());
                        let sc = unpack(sp.add(i).read_volatile());
                        dp.add(i)
                            .write_volatile(pack(crate::gdi_blit::blend_rgb(dc, sc, alpha)));
                    }
                }
                done += n;
            }
        }
    }
    drop(m);
    if let Some(hwnd) = d.hwnd {
        crate::window::damage(
            hwnd,
            r.dx as i32,
            r.dy as i32,
            r.width as i32,
            r.height as i32,
        );
    }
    true
}

fn text_state(dc: u64, m: &crate::gdi_memory::Objects) -> Option<crate::gdi_memory::TextState> {
    if is_memory(dc) {
        return m.dc(owner(), dc).map(|d| d.text);
    }
    if dc == 1 {
        return Some(*SCREEN_TEXT.lock());
    }
    crate::window::window_text(window_dc_id(dc)?)
}
pub fn selected_font(dc: u64) -> Option<crate::gdi_font::Font<'static>> {
    let m = MEMORY.lock();
    let state = text_state(dc, &m)?;
    let index = m.font_index(owner(), state.font)?;
    crate::gdi_font::atlas()?.font(index)
}
pub fn create_font(height: i32, weight: i32) -> u64 {
    let Some(font) = crate::gdi_font::atlas().and_then(|a| a.choose(height, weight)) else {
        return 0;
    };
    MEMORY.lock().create_font(owner(), font.index)
}
pub fn get_text_property(dc: u64, kind: u32) -> Option<u32> {
    let m = MEMORY.lock();
    let s = text_state(dc, &m)?;
    match kind {
        0 => Some(s.foreground),
        1 => Some(s.background),
        2 => Some(s.mode),
        _ => None,
    }
}
pub fn set_text_property(dc: u64, kind: u32, value: u32) -> Option<u32> {
    if is_memory(dc) {
        return MEMORY.lock().dc_mut(owner(), dc)?.text.set(kind, value);
    }
    if dc == 1 {
        return SCREEN_TEXT.lock().set(kind, value);
    }
    crate::window::set_text_property(window_dc_id(dc)?, kind, value)
}
pub fn current_object(dc: u64, kind: u32) -> u64 {
    let m = MEMORY.lock();
    match kind {
        1 => {
            if is_memory(dc) {
                m.dc(owner(), dc).map_or(0, |d| d.pen.selected)
            } else if dc == 1 {
                SCREEN_PEN.lock().selected
            } else {
                window_dc_id(dc)
                    .and_then(crate::window::window_pen)
                    .map_or(0, |p| p.selected)
            }
        }
        6 => text_state(dc, &m).map_or(0, |s| s.font),
        7 => m.dc(owner(), dc).map_or(0, |d| d.bitmap),
        2 => {
            if is_memory(dc) {
                m.dc(owner(), dc).map_or(0, |d| d.brush.selected)
            } else if dc == 1 {
                SCREEN_BRUSH.lock().selected
            } else {
                window_dc_id(dc)
                    .and_then(crate::window::window_brush)
                    .map_or(0, |b| b.selected)
            }
        }
        _ => 0,
    }
}
fn text_chars(bytes: &[u8]) -> impl Iterator<Item = char> + '_ {
    core::char::decode_utf16(
        bytes
            .chunks_exact(2)
            .map(|b| u16::from_le_bytes([b[0], b[1]])),
    )
    .map(|c| c.unwrap_or('\u{fffd}'))
}
pub fn text_extent(dc: u64, bytes: &[u8]) -> Option<(i32, i32)> {
    let font = selected_font(dc)?;
    if bytes.is_empty() {
        return Some((0, 0));
    }
    font.measure(text_chars(bytes))
}
fn add_dirty(dirty: &mut Option<[i32; 4]>, r: [i32; 4]) {
    if let Some(old) = dirty {
        old[0] = old[0].min(r[0]);
        old[1] = old[1].min(r[1]);
        old[2] = old[2].max(r[2]);
        old[3] = old[3].max(r[3]);
    } else {
        *dirty = Some(r);
    }
}
impl Surface {
    fn clipped_box(&self, l: i64, t: i64, r: i64, b: i64) -> Option<[i32; 4]> {
        let (l, t, r, b) = (
            l.max(0),
            t.max(0),
            r.min(self.shape.width as i64),
            b.min(self.shape.height as i64),
        );
        if l >= r || t >= b {
            return None;
        }
        Some([l as i32, t as i32, r as i32, b as i32])
    }
    fn fill_box(&self, l: i64, t: i64, r: i64, b: i64, color: u32) -> Option<[i32; 4]> {
        let rect = self.clipped_box(l, t, r, b)?;
        let [l, t, r, b] = rect;
        for y in t..b {
            let mut x = l as usize;
            while x < r as usize {
                let (p, n) = self.chunk(x, y as usize, false);
                let n = n.min(r as usize - x);
                for i in 0..n {
                    unsafe {
                        p.add(i).write_volatile(color);
                    }
                }
                x += n;
            }
        }
        Some(rect)
    }
    fn glyph(&self, x: i64, y: i64, g: crate::gdi_font::Glyph<'_>, color: u32) -> Option<[i32; 4]> {
        let rect = self.clipped_box(x, y, x + g.width as i64, y + g.height as i64)?;
        let [l, t, r, b] = rect;
        for py in t..b {
            let mut px = l as usize;
            while px < r as usize {
                let (p, n) = self.chunk(px, py as usize, false);
                let n = n.min(r as usize - px);
                for i in 0..n {
                    let alpha = g.coverage
                        [(py as i64 - y) as usize * g.width + (px as i64 + i as i64 - x) as usize];
                    if alpha == 0 {
                        continue;
                    }
                    unsafe {
                        let pixel = p.add(i);
                        if alpha == 255 {
                            pixel.write_volatile(color);
                        } else {
                            pixel.write_volatile(pack(crate::gdi_blit::blend_rgb(
                                unpack(pixel.read_volatile()),
                                unpack(color),
                                alpha,
                            )));
                        }
                    }
                }
                px += n;
            }
        }
        Some(rect)
    }
}
/// Counted UTF16 text, antialiased installed bitmap face, integer coverage
/// blend and one damage event. Layout is left/top, identity mapping only.
pub fn text_out(dc: u64, x: i32, y: i32, bytes: &[u8]) -> bool {
    let mut m = MEMORY.lock();
    let Some(state) = text_state(dc, &m) else {
        return false;
    };
    let Some(index) = m.font_index(owner(), state.font) else {
        return false;
    };
    let Some(font) = crate::gdi_font::atlas().and_then(|a| a.font(index)) else {
        return false;
    };
    let Some((width, height)) = font.measure(text_chars(bytes)) else {
        return false;
    };
    let Some(s) = surface(dc, &mut m) else {
        return false;
    };
    if bytes.is_empty() {
        return true;
    }
    let (mut cx, cy) = (x as i64, y as i64);
    let mut dirty = None;
    if state.mode == 2 {
        if let Some(r) = s.fill_box(
            cx,
            cy,
            cx + width as i64,
            cy + height as i64,
            pack(state.background),
        ) {
            add_dirty(&mut dirty, r);
        }
    }
    for ch in text_chars(bytes) {
        let Some(g) = font.glyph(ch) else {
            return false;
        };
        if let Some(r) = s.glyph(
            cx + g.left as i64,
            cy + font.metrics.ascent as i64 - g.top as i64,
            g,
            pack(state.foreground),
        ) {
            add_dirty(&mut dirty, r);
        }
        cx += g.advance as i64;
    }
    drop(m);
    if let (Some(hwnd), Some([l, t, r, b])) = (s.hwnd, dirty) {
        crate::window::damage(hwnd, l, t, r - l, b - t);
    }
    true
}

fn pen_of(dc: u64) -> Option<PenState> {
    if is_memory(dc) {
        return MEMORY.lock().dc(owner(), dc).map(|d| d.pen);
    }
    if dc == 1 {
        return Some(*SCREEN_PEN.lock());
    }
    crate::window::window_pen(window_dc_id(dc)?)
}
pub fn create_pen(style: i32, width: i32, color: u32) -> u64 {
    if width < 0 {
        return 0;
    }
    if style == 5 {
        return crate::gdi_memory::NULL_PEN;
    }
    if style != 0 || width > 1 {
        return 0;
    }
    MEMORY.lock().create_pen(owner(), color)
}
pub fn get_dc_pen_color(dc: u64) -> u32 {
    pen_of(dc).map_or(u32::MAX, |p| p.dc_color)
}
pub fn set_dc_pen_color(dc: u64, color: u32) -> u32 {
    if is_memory(dc) {
        return MEMORY
            .lock()
            .dc_mut(owner(), dc)
            .map_or(u32::MAX, |d| d.pen.set_color(color));
    }
    if dc == 1 {
        return SCREEN_PEN.lock().set_color(color);
    }
    window_dc_id(dc)
        .and_then(|hwnd| crate::window::set_dc_pen_color(hwnd, color))
        .unwrap_or(u32::MAX)
}
/// Identity-mapped Rectangle with a solid/hollow brush and one-pixel/null
/// pen. Stroke positions are computed before clipping, not at clipped edges.
pub fn rectangle(dc: u64, left: i32, top: i32, right: i32, bottom: i32) -> bool {
    let (l, t, r, b) = (left as i64, top as i64, right as i64, bottom as i64);
    if l >= r || t >= b {
        return false;
    }
    let Some(brush) = brush_of(dc) else {
        return false;
    };
    let Some(pen) = pen_of(dc) else {
        return false;
    };
    let (fill, stroke) = (brush.color(), pen.color());
    if fill.is_none() && stroke.is_none() {
        return true;
    }
    let mut m = MEMORY.lock();
    let surface = surface(dc, &mut m);
    if let Some(s) = surface {
        let mut dirty = None;
        if let Some(color) = fill {
            let inset = if stroke.is_some() { 1 } else { 0 };
            if let Some(boxed) = s.fill_box(l + inset, t + inset, r - inset, b - inset, pack(color))
            {
                add_dirty(&mut dirty, boxed);
            }
        }
        if let Some(color) = stroke {
            let color = pack(color);
            let mut line = |l, t, r, b| {
                if let Some(boxed) = s.fill_box(l, t, r, b, color) {
                    add_dirty(&mut dirty, boxed);
                }
            };
            line(l, t, r, t + 1);
            if b - t > 1 {
                line(l, b - 1, r, b);
            }
            if b - t > 2 {
                line(l, t + 1, l + 1, b - 1);
                if r - l > 1 {
                    line(r - 1, t + 1, r, b - 1);
                }
            }
        }
        drop(m);
        if let (Some(hwnd), Some([l, t, r, b])) = (s.hwnd, dirty) {
            crate::window::damage(hwnd, l, t, r - l, b - t);
        }
        return dirty.is_some();
    }
    drop(m);
    // Preserve the headless framebuffer-relative window DC backend. There is
    // no compositor to notify here; normal composed/memory DCs used one event.
    if is_memory(dc) || resolve_dc(dc).is_none() {
        return false;
    }
    let mut painted = false;
    if let Some(color) = fill {
        let inset = if stroke.is_some() { 1 } else { 0 };
        painted |= fill_color_rect(dc, l + inset, t + inset, r - inset, b - inset, color);
    }
    if let Some(color) = stroke {
        painted |= fill_color_rect(dc, l, t, r, t + 1, color);
        if b - t > 1 {
            painted |= fill_color_rect(dc, l, b - 1, r, b, color);
        }
        if b - t > 2 {
            painted |= fill_color_rect(dc, l, t + 1, l + 1, b - 1, color);
            if r - l > 1 {
                painted |= fill_color_rect(dc, r - 1, t + 1, r, b - 1, color);
            }
        }
    }
    painted
}
