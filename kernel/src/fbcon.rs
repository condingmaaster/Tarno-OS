// SPDX-License-Identifier: GPL-2.0-or-later
//! Framebuffer text console — a mirror of the serial output, with scrollback,
//! keyboard selection and a clipboard source.
//!
//! The Acer has no serial port, so without this THOS would be invisible there.
//! Everything written through `serial` is also drawn here with an embedded 8x16
//! PSF1 font. Handles `\n \r \t \b` and the few CSI sequences BusyBox's line
//! editor emits (`K`, `J`, `H`, `C`, `D`); other escapes are swallowed.
//!
//! The console keeps a **text model**: a fixed ring of [`MAX_LINES`] rows of
//! `cols` cells (Latin-15 bytes). The visible screen is the newest `rows` rows;
//! older rows are scrollback. The ring is allocated once at `init` — `write`
//! never allocates, so a panic or fault message can always reach the screen even
//! if the heap lock is held by whatever just crashed.
//!
//! Writes go straight to the Limine framebuffer address, which is only mapped
//! while Limine's tables are live and again after `gdi::init` re-maps it, so
//! `suspend()`/`resume()` bracket the page-table switch in `kmain`.
//!
//! Keyboard-only text selection (there is no mouse yet): [`select_all`] or
//! [`mark_begin`] + [`mark_move`], then [`copy_selection`]. Selected cells are
//! drawn inverted. Any new output clears the selection and snaps the view back
//! to the bottom, like a terminal.

use alloc::vec::Vec;
use spin::Mutex;

static FONT: &[u8] = include_bytes!("../font/Lat15-Terminus16.psf");
const GLYPH_W: usize = 8;
const GLYPH_H: usize = 16;
const FG: u32 = 0x00C8_C8C8;
const BG: u32 = 0x0000_0000;
/// The THOS look of the console once the splash is over: soft white on a deep slate.
const THEME_FG: u32 = 0x00E0_E4EF;
const THEME_BG: u32 = 0x000D_1017;
/// The wordmark (Lato Bold, OFL): `u16 width, u16 height`, then one alpha byte per pixel.
static LOGO: &[u8] = include_bytes!("../font/thos-logo.bin");
/// Nothing is drawn while the splash screen is up (the log still goes to the serial port and /proc/dmesg).
static QUIET: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);
/// Scrollback depth, screen rows included.
const MAX_LINES: usize = 2000;
/// Widest console supported (text columns). 256 columns = 2048 px; wider
/// framebuffers simply leave the right-hand margin unused.
const MAX_COLS: usize = 256;

/// The text model's backing store — a static, not a `Vec`: the console must be
/// usable *before* the kernel heap exists (it is the first thing brought up) and
/// `write` must never allocate (see the module docs).
static mut CELLS: [u8; MAX_LINES * MAX_COLS] = [b' '; MAX_LINES * MAX_COLS];

#[derive(Clone, Copy, PartialEq)]
enum Esc {
    None,
    Esc,
    Csi,
}

/// Selection endpoints in *absolute* row numbers (stable while rows scroll off
/// the front of the ring) and columns.
#[derive(Clone, Copy)]
struct Sel {
    anchor: (u64, usize),
    pos: (u64, usize),
}

struct Con {
    base: *mut u8,
    pitch: usize,
    cols: usize,
    rows: usize,
    /// Ring of `MAX_LINES * cols` cells (a prefix of [`CELLS`]); logical row `i`
    /// lives at physical row `(start + i) % MAX_LINES`.
    cells: &'static mut [u8],
    start: usize,
    /// Logical rows in use (`rows ..= MAX_LINES`). Screen = the last `rows`.
    len: usize,
    /// Rows ever dropped off the front — absolute row = `dropped + logical`.
    dropped: u64,
    cx: usize,
    cy: usize,
    /// How many rows the view is scrolled up (0 = following the output).
    voff: usize,
    sel: Option<Sel>,
    /// Keyboard mark mode: arrows move `sel.pos` instead of typing.
    mark: bool,
    active: bool,
    esc: Esc,
    param: [usize; 2],
    nparam: usize,
    /// UTF-8 decoder: code point so far and continuation bytes still expected.
    cp: u32,
    need: u8,
    /// Pixel origin of the text grid and the colours (the login screen centres a narrow grid under the logo).
    ox: usize,
    oy: usize,
    fg: u32,
    bg: u32,
    width: usize,
    height: usize,
}

// The framebuffer pointer is only touched under the `CON` lock.
unsafe impl Send for Con {}

static CON: Mutex<Option<Con>> = Mutex::new(None);

/// Take over the framebuffer: clear it to black and start at the top left.
/// Only 32 bpp is supported (what Limine/VBE hands out); otherwise stays off.
pub fn init(fb: &limine::framebuffer::Framebuffer) {
    if fb.bpp != 32 || FONT.len() < 4 + 256 * GLYPH_H || FONT[0] != 0x36 || FONT[1] != 0x04 {
        return;
    }
    let cols = (fb.width as usize / GLYPH_W).min(MAX_COLS);
    let rows = fb.height as usize / GLYPH_H;
    if cols == 0 || rows == 0 || rows > MAX_LINES {
        return;
    }
    // Single-shot: the console owns CELLS exclusively from here on.
    let cells: &'static mut [u8] =
        unsafe { core::slice::from_raw_parts_mut(core::ptr::addr_of_mut!(CELLS) as *mut u8, MAX_LINES * cols) };
    cells.fill(b' ');
    let mut c = Con {
        base: fb.address() as *mut u8,
        pitch: fb.pitch as usize,
        cols,
        rows,
        cells,
        start: 0,
        len: rows,
        dropped: 0,
        cx: 0,
        cy: 0,
        voff: 0,
        sel: None,
        mark: false,
        active: true,
        esc: Esc::None,
        param: [0; 2],
        nparam: 0,
        cp: 0,
        need: 0,
        ox: 0,
        oy: 0,
        fg: FG,
        bg: BG,
        width: fb.width as usize,
        height: fb.height as usize,
    };
    c.redraw_all();
    *CON.lock() = Some(c);
}

/// Stop drawing (the framebuffer mapping is about to change).
pub fn suspend() {
    if let Some(c) = CON.lock().as_mut() {
        c.active = false;
    }
}

/// Resume drawing once the framebuffer is mapped again.
pub fn resume() {
    if let Some(c) = CON.lock().as_mut() {
        c.active = true;
        if QUIET.load(core::sync::atomic::Ordering::Relaxed) {
            paint_splash(c); // the splash is not part of the text model: paint it again on the freshly mapped framebuffer
        } else {
            c.redraw_all(); // anything printed while suspended is in the model only
        }
    }
}

/// While the login screen is up, kernel log lines (network bring-up and the like) stay off the screen; the login code itself lets its own lines through.
static LOGIN_FILTER: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);
static LOGIN_ALLOW: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);
pub fn login_allow(on: bool) {
    LOGIN_ALLOW.store(on, core::sync::atomic::Ordering::Relaxed);
}
/// Kernel log text (as opposed to program or prompt output): hidden while the login screen is up.
pub fn write_log(bytes: &[u8]) {
    use core::sync::atomic::Ordering::Relaxed;
    if LOGIN_FILTER.load(Relaxed) && !LOGIN_ALLOW.load(Relaxed) {
        return;
    }
    write(bytes);
}

/// Feed output bytes (UTF-8) to the console.
pub fn write(bytes: &[u8]) {
    if QUIET.load(core::sync::atomic::Ordering::Relaxed) {
        return;
    }
    let mut g = CON.lock();
    let Some(c) = g.as_mut() else { return };
    // New output ends any selection and returns to the live screen.
    let stale = c.voff != 0 || c.sel.is_some() || c.mark;
    c.voff = 0;
    c.sel = None;
    c.mark = false;
    if !c.active {
        // Still keep the text model current so `resume` can repaint it.
        for &b in bytes {
            c.utf8(b);
        }
        return;
    }
    if stale {
        c.redraw_all();
    }
    c.cursor_xor();
    for &b in bytes {
        c.utf8(b);
    }
    c.cursor_xor();
}

// ---- keyboard-driven view / selection API (called from `console`) ----

/// Scroll the view by `delta` rows (positive = back in time).
pub fn scroll_view(delta: isize) {
    if let Some(c) = CON.lock().as_mut() {
        let max = (c.len - c.rows) as isize;
        let v = (c.voff as isize + delta).clamp(0, max);
        c.voff = v as usize;
        c.redraw_all();
    }
}

/// Rows per "page" for `PgUp`/`PgDn` (one screen minus a line of context).
pub fn page_rows() -> isize {
    CON.lock().as_ref().map_or(20, |c| c.rows as isize - 1)
}

/// Jump to the oldest (`true`) or newest (`false`) row.
pub fn scroll_to(top: bool) {
    if let Some(c) = CON.lock().as_mut() {
        c.voff = if top { c.len - c.rows } else { 0 };
        c.redraw_all();
    }
}

/// Select the whole text model (scrollback + screen).
pub fn select_all() {
    if let Some(c) = CON.lock().as_mut() {
        let first = c.dropped;
        let last = c.dropped + c.len as u64 - 1;
        c.sel = Some(Sel { anchor: (first, 0), pos: (last, c.cols - 1) });
        c.mark = false;
        c.redraw_all();
    }
}

/// Enter keyboard mark mode at the text cursor.
pub fn mark_begin() {
    if let Some(c) = CON.lock().as_mut() {
        let row = c.dropped + (c.len - c.rows + c.cy) as u64;
        let col = c.cx.min(c.cols - 1);
        c.sel = Some(Sel { anchor: (row, col), pos: (row, col) });
        c.mark = true;
        c.redraw_all();
    }
}

/// Is keyboard mark mode active?
pub fn mark_active() -> bool {
    CON.lock().as_ref().is_some_and(|c| c.mark)
}

/// Move the mark-mode end by `(dx, dy)` cells (`dx` columns, `dy` rows).
pub fn mark_move(dx: isize, dy: isize) {
    if let Some(c) = CON.lock().as_mut() {
        let Some(mut s) = c.sel else { return };
        let (first, last) = (c.dropped, c.dropped + c.len as u64 - 1);
        let row = (s.pos.0 as i64 + dy as i64).clamp(first as i64, last as i64) as u64;
        let col = (s.pos.1 as isize + dx).clamp(0, c.cols as isize - 1) as usize;
        s.pos = (row, col);
        c.sel = Some(s);
        c.reveal(row);
        c.redraw_all();
    }
}

/// Mark mode: jump to the start (`true`) or end (`false`) of the current row.
pub fn mark_line_edge(start: bool) {
    if let Some(c) = CON.lock().as_mut() {
        let Some(mut s) = c.sel else { return };
        s.pos.1 = if start { 0 } else { c.cols - 1 };
        c.sel = Some(s);
        c.redraw_all();
    }
}

/// Leave mark mode / drop the selection.
pub fn clear_selection() {
    if let Some(c) = CON.lock().as_mut() {
        if c.sel.is_some() || c.mark {
            c.sel = None;
            c.mark = false;
            c.redraw_all();
        }
    }
}

/// `true` if there is a selection to copy.
pub fn has_selection() -> bool {
    CON.lock().as_ref().is_some_and(|c| c.sel.is_some())
}

/// The selected text (rows joined with `\n`, trailing blanks trimmed), or
/// `None` without a selection. Leaves the selection in place.
pub fn copy_selection() -> Option<Vec<u8>> {
    let g = CON.lock();
    let c = g.as_ref()?;
    let s = c.sel?;
    let (a, b) = if s.anchor <= s.pos { (s.anchor, s.pos) } else { (s.pos, s.anchor) };
    let mut out = Vec::new();
    for abs in a.0..=b.0 {
        let from = if abs == a.0 { a.1 } else { 0 };
        let to = if abs == b.0 { b.1 } else { c.cols - 1 };
        let idx = (abs - c.dropped) as usize;
        let row = &c.cells[c.phys(idx) * c.cols..][..c.cols];
        let mut seg = &row[from..=to.min(c.cols - 1)];
        while let [rest @ .., b' '] = seg {
            seg = rest; // trim trailing blanks
        }
        out.extend_from_slice(seg);
        if abs != b.0 {
            out.push(b'\n');
        }
    }
    Some(out)
}

/// Blank the visible screen (not the scrollback) and home the cursor.
pub fn clear_screen() {
    if let Some(c) = CON.lock().as_mut() {
        c.voff = 0;
        c.sel = None;
        c.mark = false;
        c.clear_visible();
        c.cx = 0;
        c.cy = 0;
        c.redraw_all();
    }
}

/// Number of text columns / rows (for `TIOCGWINSZ`); `(80, 25)` without a console.
pub fn size() -> (usize, usize) {
    CON.lock().as_ref().map_or((80, 25), |c| (c.cols, c.rows))
}

impl Con {
    /// Physical ring row of logical row `idx`.
    fn phys(&self, idx: usize) -> usize {
        (self.start + idx) % MAX_LINES
    }

    /// Logical index of the screen row `y` (0 = top of the live screen).
    fn screen_idx(&self, y: usize) -> usize {
        self.len - self.rows + y
    }

    fn cell(&mut self, idx: usize, x: usize) -> &mut u8 {
        let p = self.phys(idx);
        &mut self.cells[p * self.cols + x]
    }

    fn px(&mut self, x: usize, y: usize, v: u32) {
        unsafe { core::ptr::write_volatile(self.base.add(y * self.pitch + x * 4) as *mut u32, v) }
    }

    /// Draw glyph `ch` at text cell `(col, row)` of the *screen*.
    fn glyph(&mut self, col: usize, row: usize, ch: u8, inverted: bool) {
        let (fg, bg) = if inverted { (self.bg, self.fg) } else { (self.fg, self.bg) };
        let g = &FONT[4 + ch as usize * GLYPH_H..4 + (ch as usize + 1) * GLYPH_H];
        for (dy, bits) in g.iter().enumerate() {
            for dx in 0..GLYPH_W {
                let on = bits & (0x80 >> dx) != 0;
                self.px(self.ox + col * GLYPH_W + dx, self.oy + row * GLYPH_H + dy, if on { fg } else { bg });
            }
        }
    }

    fn selected(&self, abs: u64, col: usize) -> bool {
        let Some(s) = self.sel else { return false };
        let (a, b) = if s.anchor <= s.pos { (s.anchor, s.pos) } else { (s.pos, s.anchor) };
        (abs, col) >= a && (abs, col) <= b
    }

    /// Repaint the whole visible window from the text model.
    fn redraw_all(&mut self) {
        if !self.active {
            return;
        }
        for y in 0..self.rows {
            let idx = self.screen_idx(y) - self.voff;
            let abs = self.dropped + idx as u64;
            for x in 0..self.cols {
                let ch = *self.cell(idx, x);
                let inv = self.selected(abs, x);
                self.glyph(x, y, ch, inv);
            }
        }
        // Leftover pixels right of the last full cell column / below the last row.
        if self.voff == 0 && self.sel.is_none() {
            self.cursor_xor();
        }
    }

    /// Bring absolute row `abs` into view (mark mode cursor movement).
    fn reveal(&mut self, abs: u64) {
        let idx = (abs - self.dropped) as usize;
        let top_live = self.len - self.rows; // first live-screen row
        let view_top = top_live - self.voff;
        if idx < view_top {
            self.voff = top_live - idx;
        } else if idx >= view_top + self.rows {
            self.voff = top_live.saturating_sub(idx + 1 - self.rows);
        }
    }

    /// Draw/erase the block cursor by inverting its cell (live screen only).
    fn cursor_xor(&mut self) {
        if !self.active || self.voff != 0 {
            return;
        }
        let (x0, y0) = (self.ox + self.cx.min(self.cols - 1) * GLYPH_W, self.oy + self.cy * GLYPH_H);
        for dy in 0..GLYPH_H {
            for dx in 0..GLYPH_W {
                let p = unsafe { self.base.add((y0 + dy) * self.pitch + (x0 + dx) * 4) as *mut u32 };
                unsafe { core::ptr::write_volatile(p, core::ptr::read_volatile(p) ^ 0x00FF_FFFF) };
            }
        }
    }

    fn clear_cells(&mut self, row: usize, from: usize, to: usize) {
        let idx = self.screen_idx(row);
        for col in from..to {
            *self.cell(idx, col) = b' ';
            if self.active && self.voff == 0 {
                self.glyph(col, row, b' ', false);
            }
        }
    }

    fn clear_visible(&mut self) {
        for y in 0..self.rows {
            let idx = self.screen_idx(y);
            for x in 0..self.cols {
                *self.cell(idx, x) = b' ';
            }
        }
    }

    /// Append a blank row; the oldest row falls off the ring when full.
    fn scroll(&mut self) {
        if self.len < MAX_LINES {
            self.len += 1;
        } else {
            self.start = (self.start + 1) % MAX_LINES;
            self.dropped += 1;
        }
        let last = self.len - 1;
        for x in 0..self.cols {
            *self.cell(last, x) = b' ';
        }
        if self.active && self.voff == 0 {
            let row_bytes = self.pitch * GLYPH_H;
            unsafe {
                let top = self.base.add(self.oy * self.pitch);
                core::ptr::copy(top.add(row_bytes), top, row_bytes * (self.rows - 1));
            }
            let last_row = self.rows - 1;
            for x in 0..self.cols {
                self.glyph(x, last_row, b' ', false);
            }
        }
    }

    fn newline(&mut self) {
        self.cx = 0;
        if self.cy + 1 >= self.rows {
            self.scroll();
        } else {
            self.cy += 1;
        }
    }

    fn csi_final(&mut self, f: u8) {
        let n = |s: &Self, i: usize| if s.nparam > i { s.param[i] } else { 0 };
        match f {
            b'K' => {
                let (cy, cx, cols) = (self.cy, self.cx, self.cols);
                match n(self, 0) {
                    0 => self.clear_cells(cy, cx, cols),
                    1 => self.clear_cells(cy, 0, (cx + 1).min(cols)),
                    _ => self.clear_cells(cy, 0, cols),
                }
            }
            b'J' => {
                let (cy, cx, cols, rows) = (self.cy, self.cx, self.cols, self.rows);
                match n(self, 0) {
                    0 => {
                        self.clear_cells(cy, cx, cols);
                        for r in cy + 1..rows {
                            self.clear_cells(r, 0, cols);
                        }
                    }
                    _ => {
                        for r in 0..rows {
                            self.clear_cells(r, 0, cols);
                        }
                        self.cx = 0;
                        self.cy = 0;
                    }
                }
            }
            b'H' | b'f' => {
                self.cy = n(self, 0).max(1).saturating_sub(1).min(self.rows - 1);
                self.cx = n(self, 1).max(1).saturating_sub(1).min(self.cols - 1);
            }
            b'C' => self.cx = (self.cx + n(self, 0).max(1)).min(self.cols - 1),
            b'D' => self.cx = self.cx.saturating_sub(n(self, 0).max(1)),
            _ => {} // `m` (colours) and anything else: ignored
        }
    }

    /// Feed one byte of UTF-8; the font is Latin-15, so box drawing and
    /// punctuation fall back to ASCII look-alikes and unknown glyphs to `?`.
    fn utf8(&mut self, b: u8) {
        if b < 0x80 {
            self.need = 0;
            return self.put(b);
        }
        if self.need > 0 && b & 0xC0 == 0x80 {
            self.cp = (self.cp << 6) | (b & 0x3F) as u32;
            self.need -= 1;
            if self.need == 0 {
                let g = fallback(self.cp);
                self.put(g);
            }
            return;
        }
        match b {
            0xC0..=0xDF => (self.cp, self.need) = ((b & 0x1F) as u32, 1),
            0xE0..=0xEF => (self.cp, self.need) = ((b & 0x0F) as u32, 2),
            0xF0..=0xF7 => (self.cp, self.need) = ((b & 0x07) as u32, 3),
            _ => {
                self.need = 0;
                self.put(b'?');
            }
        }
    }

    fn put(&mut self, b: u8) {
        match self.esc {
            Esc::Esc => {
                if b == b'[' {
                    self.esc = Esc::Csi;
                    self.param = [0; 2];
                    self.nparam = 0;
                } else {
                    self.esc = Esc::None;
                }
                return;
            }
            Esc::Csi => {
                match b {
                    b'0'..=b'9' => {
                        if self.nparam == 0 {
                            self.nparam = 1;
                        }
                        let i = self.nparam - 1;
                        self.param[i] = self.param[i].saturating_mul(10) + (b - b'0') as usize;
                    }
                    b';' => self.nparam = (self.nparam.max(1) + 1).min(2),
                    0x40..=0x7E => {
                        self.esc = Esc::None;
                        self.csi_final(b);
                    }
                    _ => {}
                }
                return;
            }
            Esc::None => {}
        }
        match b {
            0x1B => self.esc = Esc::Esc,
            b'\n' => self.newline(),
            b'\r' => self.cx = 0,
            0x08 => self.cx = self.cx.saturating_sub(1),
            b'\t' => {
                self.cx = (self.cx + 8) & !7;
                if self.cx >= self.cols {
                    self.newline();
                }
            }
            0x07 => {}
            _ => {
                if self.cx >= self.cols {
                    self.newline();
                }
                let (cx, cy) = (self.cx, self.cy);
                let idx = self.screen_idx(cy);
                *self.cell(idx, cx) = b;
                if self.active {
                    self.glyph(cx, cy, b, false);
                }
                self.cx += 1;
            }
        }
    }
}

/// Glyph byte (Latin-15 slot) for a Unicode code point.
fn fallback(cp: u32) -> u8 {
    match cp {
        0xA0..=0xFF => cp as u8, // Latin-1 range: same slots as Latin-15 for the common letters
        0x2500 | 0x2501 | 0x2504..=0x2509 | 0x254C..=0x254F => b'-',
        0x2502 | 0x2503 | 0x250A..=0x250B | 0x2551 => b'|',
        0x2550 => b'=',
        0x2500..=0x257F => b'+',
        0x2010..=0x2015 => b'-',
        0x2018 | 0x2019 => b'\'',
        0x201C | 0x201D => b'"',
        0x2022 => b'*',
        0x2026 => b'.',
        0x2190 => b'<',
        0x2192 => b'>',
        0x20AC => 0xA4, // euro sign in Latin-15
        _ => b'?',
    }
}


// ---- boot splash and the themed console ----

impl Con {
    fn fill(&mut self, x0: usize, y0: usize, x1: usize, y1: usize, v: u32) {
        for y in y0..y1.min(self.height) {
            for x in x0..x1.min(self.width) {
                self.px(x, y, v);
            }
        }
    }
    fn blend(a: u32, b: u32, t: u32) -> u32 {
        let ch = |s: u32| ((a >> s & 255) * (255 - t) + (b >> s & 255) * t) / 255;
        ch(16) << 16 | ch(8) << 8 | ch(0)
    }
    /// The wordmark with its top-left corner at (x0, y0), tinted from `top` to `bottom`.
    fn draw_logo(&mut self, x0: usize, y0: usize, top: u32, bottom: u32, bg: u32) {
        let (w, h) = (u16::from_le_bytes([LOGO[0], LOGO[1]]) as usize, u16::from_le_bytes([LOGO[2], LOGO[3]]) as usize);
        for y in 0..h {
            let tint = Self::blend(top, bottom, (y * 255 / h.max(1)) as u32);
            for x in 0..w {
                let a = LOGO[4 + y * w + x] as u32;
                if a != 0 {
                    self.px(x0 + x, y0 + y, Self::blend(bg, tint, a));
                }
            }
        }
    }
    /// Re-lay the text grid: `max_cols` columns centred (0 = the whole width), starting `oy` pixels from the top, with the THEME colours.
    fn relayout(&mut self, max_cols: usize, oy: usize, logo: bool) {
        let cols = if max_cols == 0 { (self.width / GLYPH_W).min(MAX_COLS) } else { (self.width / GLYPH_W).min(max_cols).min(MAX_COLS) };
        let rows = (self.height - oy.min(self.height)) / GLYPH_H;
        if cols == 0 || rows == 0 {
            return;
        }
        self.fg = THEME_FG;
        self.bg = THEME_BG;
        self.cols = cols;
        self.rows = rows;
        self.ox = (self.width - cols * GLYPH_W) / 2;
        self.oy = oy;
        self.cells = unsafe { core::slice::from_raw_parts_mut(core::ptr::addr_of_mut!(CELLS) as *mut u8, MAX_LINES * cols) };
        self.cells.fill(b' ');
        self.start = 0;
        self.len = rows;
        self.dropped = 0;
        self.cx = 0;
        self.cy = 0;
        self.voff = 0;
        self.sel = None;
        self.mark = false;
        self.esc = Esc::None;
        self.active = true;
        let (w, h) = (self.width, self.height);
        self.fill(0, 0, w, h, THEME_BG);
        if logo {
            let (lw, lh) = (u16::from_le_bytes([LOGO[0], LOGO[1]]) as usize, u16::from_le_bytes([LOGO[2], LOGO[3]]) as usize);
            let _ = lh;
            self.draw_logo((w.saturating_sub(lw)) / 2, 36, 0x00F4_F7FF, 0x008F_B0F5, THEME_BG);
        }
        self.redraw_all();
    }
}

/// The boot splash: a deep gradient with a soft glow, the wordmark and an empty progress track. Text output stays off the screen until
/// [`theme_login`] (or [`reveal`] after a failure).
pub fn splash_start() {
    let mut g = CON.lock();
    let Some(c) = g.as_mut() else { return };
    paint_splash(c);
    QUIET.store(true, core::sync::atomic::Ordering::Relaxed);
}

/// How far the splash progress bar is (so a redraw after a framebuffer remap restores it).
static SPLASH_PCT: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);

fn paint_splash(c: &mut Con) {
    let (w, h) = (c.width, c.height);
    let (gx, gy) = (w / 2, h * 2 / 5);
    for y in 0..h {
        let base = Con::blend(0x000B_0E15, 0x0019_2036, (y * 255 / h) as u32);
        for x in 0..w {
            let dx = x as i64 - gx as i64;
            let dy = (y as i64 - gy as i64) * 3 / 2;
            let k = (w * h / 6) as i64;
            let glow = (60 * k / (k + dx * dx + dy * dy)) as u32;
            c.px(x, y, Con::blend(base, 0x0040_5FB0, glow));
        }
    }
    let (lw, lh) = (u16::from_le_bytes([LOGO[0], LOGO[1]]) as usize, u16::from_le_bytes([LOGO[2], LOGO[3]]) as usize);
    let (lx, ly) = ((w.saturating_sub(lw)) / 2, (h * 2 / 5).saturating_sub(lh / 2));
    // the logo is blended over the gradient it sits on
    for y in 0..lh {
        let tint = Con::blend(0x00F4_F7FF, 0x008F_B0F5, (y * 255 / lh.max(1)) as u32);
        for x in 0..lw {
            let a = LOGO[4 + y * lw + x] as u32;
            if a != 0 {
                let p = unsafe { core::ptr::read_volatile(c.base.add((ly + y) * c.pitch + (lx + x) * 4) as *const u32) };
                c.px(lx + x, ly + y, Con::blend(p, tint, a));
            }
        }
    }
    let (bw, bx, by) = (280usize, w / 2 - 140, ly + lh + 48);
    for x in 0..bw {
        c.px(bx + x, by, 0x0030_3A55);
        c.px(bx + x, by + 1, 0x0030_3A55);
        c.px(bx + x, by + 2, 0x0030_3A55);
    }
    paint_progress(c, SPLASH_PCT.load(core::sync::atomic::Ordering::Relaxed));
}

fn paint_progress(c: &mut Con, pct: u32) {
    let (w, h) = (c.width, c.height);
    let (lh, bw) = (u16::from_le_bytes([LOGO[2], LOGO[3]]) as usize, 280usize);
    let (bx, by) = (w / 2 - 140, (h * 2 / 5).saturating_sub(lh / 2) + lh + 48);
    let fill = bw * pct.min(100) as usize / 100;
    for x in 0..fill {
        let t = (x * 255 / bw) as u32;
        let col = Con::blend(0x007B_A3FF, 0x00B9_CDFF, t);
        for dy in 0..3 {
            c.px(bx + x, by + dy, col);
        }
    }
}

/// Advance the splash progress bar to `pct` percent.
pub fn splash_progress(pct: u32) {
    if !QUIET.load(core::sync::atomic::Ordering::Relaxed) {
        return;
    }
    SPLASH_PCT.store(pct, core::sync::atomic::Ordering::Relaxed);
    let mut g = CON.lock();
    let Some(c) = g.as_mut() else { return };
    paint_progress(c, pct);
}

/// The login screen: the wordmark on top, a narrow centred text column below it, in the THOS colours.
pub fn theme_login() {
    QUIET.store(false, core::sync::atomic::Ordering::Relaxed);
    LOGIN_FILTER.store(true, core::sync::atomic::Ordering::Relaxed);
    if let Some(c) = CON.lock().as_mut() {
        c.relayout(72, 36 + u16::from_le_bytes([LOGO[2], LOGO[3]]) as usize + 40, true);
    }
}

/// The shell console after login: the whole screen, THOS colours, no logo.
pub fn theme_shell() {
    LOGIN_FILTER.store(false, core::sync::atomic::Ordering::Relaxed);
    QUIET.store(false, core::sync::atomic::Ordering::Relaxed);
    if let Some(c) = CON.lock().as_mut() {
        c.relayout(0, 0, false);
    }
}

/// Called once a second from the timer interrupt: if the splash is still up 45 s after power-on, the boot is stuck (or a driver is slow),
/// so show the kernel log instead of a progress bar that does not move. Never blocks (interrupt context).
pub fn splash_watchdog(ms: u64) {
    use core::sync::atomic::Ordering::Relaxed;
    static DONE: core::sync::atomic::AtomicBool = core::sync::atomic::AtomicBool::new(false);
    if ms < 45_000 || !QUIET.load(Relaxed) || DONE.load(Relaxed) {
        return;
    }
    let Some(mut g) = CON.try_lock() else { return };
    let Some(c) = g.as_mut() else { return };
    DONE.store(true, Relaxed);
    QUIET.store(false, Relaxed);
    c.relayout(0, 0, false);
    crate::serial::with_log_try(|a, b| {
        for &byte in a.iter().chain(b.iter()) {
            c.utf8(byte);
        }
    });
    c.redraw_all();
}

/// A failure while the splash is up: make the text visible again.
pub fn reveal() {
    LOGIN_FILTER.store(false, core::sync::atomic::Ordering::Relaxed);
    if QUIET.load(core::sync::atomic::Ordering::Relaxed) {
        theme_shell();
    }
}
