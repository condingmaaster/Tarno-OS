// SPDX-License-Identifier: GPL-2.0-or-later
//! Phase 3 — real windows on the ring-3 callback mechanism ([`crate::nt`]'s
//! `invoke_ring3_callback`, built for `CallWindowProcA`) + the GDI32/User32
//! skeleton ([`crate::gdi`]).
//!
//! A window is bookkeeping (class → `WndProc`, an owning thread, a rect) plus
//! a real per-thread message queue. `GetMessageA` blocks on it for real;
//! `DispatchMessageA` and `UpdateWindow` call the target `WndProc` in ring 3
//! through `invoke_ring3_callback` and get its `LRESULT` back, exactly like
//! `CallWindowProcA`. Still no compositor: a window's rect is recorded but
//! nothing clips or offsets GDI drawing into it yet (`gdi.rs`'s DC is still
//! the whole screen) — that's the next increment.

use alloc::collections::{BTreeMap, VecDeque};
use alloc::string::String;
use core::sync::atomic::{AtomicU32, Ordering};

use spin::Mutex;

use crate::wait::WaitQueue;

pub const WM_CREATE: u32 = 0x0001;
pub const WM_PAINT: u32 = 0x000F;
pub const WM_QUIT: u32 = 0x0012;

/// One registered window class: just its `WndProc`, the only thing
/// `CreateWindowExA` needs from `RegisterClassA`'s `WNDCLASSA`.
static CLASSES: Mutex<BTreeMap<String, u64>> = Mutex::new(BTreeMap::new());

pub struct Window {
    pub wndproc: u64,
    pub owner_tid: u64,
    /// (x, y, width, height), screen coordinates — `rect_of` is how
    /// `gdi.rs`'s `GetDC(hwnd)` turns client-relative drawing into real
    /// screen pixels.
    pub rect: (i32, i32, i32, i32),
    /// Backing store while a display server (`/dev/winsys`) composes windows: the window then
    /// draws into this instead of the screen, and the server maps the frames.
    pub store: Option<alloc::sync::Arc<crate::process::Section>>,
}

// --- window-server interface (`/dev/winsys`) ---

static WINSYS_OPENS: core::sync::atomic::AtomicUsize = core::sync::atomic::AtomicUsize::new(0);
/// Events for the display server: `[kind, hwnd, a, b, c, d, 0, 0]`
/// (1 = created x,y,w,h; 2 = destroyed; 3 = damage x,y,w,h).
static EVQ: Mutex<VecDeque<[u32; 8]>> = Mutex::new(VecDeque::new());
static EV_WQ: WaitQueue = WaitQueue::new();

pub fn winsys_open(on: bool) {
    if on {
        WINSYS_OPENS.fetch_add(1, Ordering::AcqRel);
    } else if WINSYS_OPENS.fetch_sub(1, Ordering::AcqRel) == 1 {
        EVQ.lock().clear();
    }
}

fn composing() -> bool {
    WINSYS_OPENS.load(Ordering::Acquire) > 0
}

fn emit(ev: [u32; 8]) {
    let mut q = EVQ.lock();
    // coalesce consecutive damage of one window into its union
    if ev[0] == 3 {
        if let Some(last) = q.back_mut() {
            if last[0] == 3 && last[1] == ev[1] {
                let (x0, y0) = (last[2].min(ev[2]), last[3].min(ev[3]));
                let (x1, y1) = ((last[2] + last[4]).max(ev[2] + ev[4]), (last[3] + last[5]).max(ev[3] + ev[5]));
                *last = [3, ev[1], x0, y0, x1 - x0, y1 - y0, 0, 0];
                return;
            }
        }
    }
    if q.len() < 1024 {
        q.push_back(ev);
    }
    drop(q);
    EV_WQ.wake_all();
}

pub fn winsys_read() -> Option<[u32; 8]> {
    EVQ.lock().pop_front()
}
pub fn winsys_ready() -> bool {
    !EVQ.lock().is_empty()
}
pub fn winsys_wait() {
    EV_WQ.wait_if_intr(|| EVQ.lock().is_empty());
}

/// The server maps window `hwnd`'s backing store into the calling process; returns the address.
pub fn winsys_map(hwnd: u32) -> i64 {
    let sec = match WINDOWS.lock().get(&hwnd).and_then(|w| w.store.clone()) {
        Some(s) => s,
        None => return -22,
    };
    match crate::sched::current_proc() {
        Some(p) => p.map_section_view(&sec, 0, sec.size) as i64,
        None => -22,
    }
}

pub fn store_of(hwnd: u32) -> Option<(alloc::sync::Arc<crate::process::Section>, i32, i32)> {
    let g = WINDOWS.lock();
    let w = g.get(&hwnd)?;
    Some((w.store.clone()?, w.rect.2, w.rect.3))
}

pub fn damage(hwnd: u32, x: i32, y: i32, w: i32, h: i32) {
    if composing() {
        emit([3, hwnd, x.max(0) as u32, y.max(0) as u32, w.max(0) as u32, h.max(0) as u32, 0, 0]);
    }
}

pub fn destroy_window(hwnd: u32) {
    if WINDOWS.lock().remove(&hwnd).is_some() {
        emit([2, hwnd, 0, 0, 0, 0, 0, 0]);
    }
}

static WINDOWS: Mutex<BTreeMap<u32, Window>> = Mutex::new(BTreeMap::new());
static NEXT_HWND: AtomicU32 = AtomicU32::new(1);

#[derive(Clone, Copy)]
pub struct Msg {
    pub hwnd: u32,
    pub message: u32,
    pub wparam: u64,
    pub lparam: u64,
}

/// Real per-thread message queues (Win32: every thread that creates a window
/// gets its own). Keyed by tid, same pattern as `process::THREAD_EXITS` /
/// `CALLBACK_FRAMES`.
static QUEUES: Mutex<BTreeMap<u64, VecDeque<Msg>>> = Mutex::new(BTreeMap::new());
/// Woken on every post; `GetMessageA` blocks on it, re-checking its own
/// thread's queue (one queue can be empty while another just got a message).
static QUEUE_WQ: WaitQueue = WaitQueue::new();

/// `RegisterClassA`: remember `name`'s `WndProc`. Real `RegisterClassA`
/// returns a 16-bit ATOM identifying the class; THOS doesn't need one
/// (`CreateWindowExA` looks classes up by name again), so callers just check
/// for non-zero success.
pub fn register_class(name: String, wndproc: u64) {
    CLASSES.lock().insert(name, wndproc);
}

/// `CreateWindowExA`: `0` if `class` was never registered.
pub fn create_window(class: &str, x: i32, y: i32, w: i32, h: i32, owner_tid: u64) -> u32 {
    let Some(&wndproc) = CLASSES.lock().get(class) else { return 0 };
    let hwnd = NEXT_HWND.fetch_add(1, Ordering::Relaxed);
    let store = (composing() && w > 0 && h > 0 && (w as usize) * (h as usize) * 4 <= 16 << 20)
        .then(|| alloc::sync::Arc::new(crate::process::Section::zeroed(w as usize * h as usize * 4)));
    let comp = store.is_some();
    WINDOWS.lock().insert(hwnd, Window { wndproc, owner_tid, rect: (x, y, w, h), store });
    if comp {
        emit([1, hwnd, x as u32, y as u32, w as u32, h as u32, 0, 0]);
    }
    post(hwnd, owner_tid, WM_CREATE, 0, 0);
    hwnd
}

pub fn wndproc_of(hwnd: u32) -> Option<u64> {
    WINDOWS.lock().get(&hwnd).map(|w| w.wndproc)
}

/// `(x, y, width, height)` in screen coordinates — `gdi.rs`'s `GetDC(hwnd)`
/// uses this to turn client-relative drawing into real screen pixels.
pub fn rect_of(hwnd: u32) -> Option<(i32, i32, i32, i32)> {
    WINDOWS.lock().get(&hwnd).map(|w| w.rect)
}

fn owner_of(hwnd: u32) -> Option<u64> {
    WINDOWS.lock().get(&hwnd).map(|w| w.owner_tid)
}

/// Queue `message` for `tid` directly — used for the window-creation
/// `WM_CREATE` (owner already known) and by `post_quit` (always the calling
/// thread's own queue).
pub fn post(hwnd: u32, tid: u64, message: u32, wparam: u64, lparam: u64) {
    QUEUES.lock().entry(tid).or_default().push_back(Msg { hwnd, message, wparam, lparam });
    QUEUE_WQ.wake_all();
}

/// `PostMessageA(hwnd, ...)`: find `hwnd`'s owning thread and queue there.
/// `false` if `hwnd` doesn't name a live window (real `PostMessageA` fails
/// the same way on a bad `HWND`).
pub fn post_message(hwnd: u32, message: u32, wparam: u64, lparam: u64) -> bool {
    match owner_of(hwnd) {
        Some(tid) => {
            post(hwnd, tid, message, wparam, lparam);
            true
        }
        None => false,
    }
}

/// `PostQuitMessage(nExitCode)`: always targets the calling thread's own
/// queue, `hwnd` `0` (real `WM_QUIT` isn't associated with any window).
pub fn post_quit(tid: u64, exit_code: u64) {
    post(0, tid, WM_QUIT, exit_code, 0);
}

/// `GetMessageA`: block until `tid`'s queue has a message, then pop it.
pub fn get_message(tid: u64) -> Msg {
    QUEUE_WQ.wait_if(|| QUEUES.lock().get(&tid).map_or(true, VecDeque::is_empty));
    QUEUES.lock().get_mut(&tid).and_then(VecDeque::pop_front).unwrap_or(Msg {
        hwnd: 0,
        message: WM_QUIT,
        wparam: 0,
        lparam: 0,
    })
}
