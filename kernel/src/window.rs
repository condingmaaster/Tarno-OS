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
static CLASSES: Mutex<crate::winclass::Registry> = Mutex::new(crate::winclass::Registry::new());

pub struct Window {
    pub wndproc: u64,
    pub unicode: bool,
    pub owner_tid: u64,
    /// (x, y, width, height), screen coordinates — `rect_of` is how
    /// `gdi.rs`'s `GetDC(hwnd)` turns client-relative drawing into real
    /// screen pixels.
    pub rect: (i32, i32, i32, i32),
    /// Backing store while a display server (`/dev/winsys`) composes windows: the window then
    /// draws into this instead of the screen, and the server maps the frames.
    pub store: Option<alloc::sync::Arc<crate::process::Section>>,
    pub generation:u32,
    brush:crate::gdi_memory::BrushState,
    text:crate::gdi_memory::TextState,
    pen:crate::gdi_memory::PenState,
    pub owner_pid:u64,
    pub title:String,
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
pub fn winsys_map(request: u64) -> i64 {
    let hwnd=request as u32;
    let generation=(request>>32) as u32;
    let sec = match WINDOWS.lock().get(&hwnd).filter(|w|generation==0||generation==w.generation).and_then(|w| w.store.clone()) {
        Some(s) => s,
        None => return -22,
    };
    match crate::sched::current_proc() {
        Some(p) => p.map_section_view(&sec, 0, sec.size) as i64,
        None => -22,
    }
}

/// Replace the client backing store before notifying either compositor or application.
/// The compositor maps only the generation named by the event, never a differently sized store.
pub fn resize_from_server(hwnd:u32,width:u32,height:u32,state:u32)->bool {
    let Some(bytes)=(width as usize).checked_mul(height as usize).and_then(|n|n.checked_mul(4)) else{return false;};
    if width==0||height==0||width>4096||height>4096||bytes>16<<20 {return false;}
    if !WINDOWS.lock().contains_key(&hwnd){return false;}
    let Some(store)=crate::process::Section::try_zeroed(bytes) else{return false;};
    let (tid,generation)={
        let mut windows=WINDOWS.lock();let Some(w)=windows.get_mut(&hwnd) else{return false;};
        w.store=Some(alloc::sync::Arc::new(store));w.rect.2=width as i32;w.rect.3=height as i32;
        w.generation=w.generation.wrapping_add(1).max(1);
        (w.owner_tid,w.generation)
    };
    emit([4,hwnd,width,height,state,0,generation,0]);
    post(hwnd,tid,5,if state==0 {0}else{2},width as u64|((height as u64)<<16));
    post(hwnd,tid,WM_PAINT,0,0);
    true
}

// Brush selection belongs to the window lifetime, not a never-pruned DC cache.
pub fn window_brush(hwnd:u32)->Option<crate::gdi_memory::BrushState>{WINDOWS.lock().get(&hwnd).map(|w|w.brush)}
pub fn replace_window_brush(hwnd:u32,handle:u64)->Option<u64>{WINDOWS.lock().get_mut(&hwnd)?.brush.select(handle)}
pub fn set_dc_brush_color(hwnd:u32,color:u32)->Option<u32>{WINDOWS.lock().get_mut(&hwnd).map(|w|w.brush.set_color(color))}

pub fn window_pen(hwnd:u32)->Option<crate::gdi_memory::PenState>{WINDOWS.lock().get(&hwnd).map(|w|w.pen)}
pub fn replace_window_pen(hwnd:u32,pen:u64)->Option<u64>{WINDOWS.lock().get_mut(&hwnd)?.pen.select(pen)}
pub fn set_dc_pen_color(hwnd:u32,color:u32)->Option<u32>{WINDOWS.lock().get_mut(&hwnd).map(|w|w.pen.set_color(color))}
pub fn replace_window_owned_pen(hwnd:u32,pen:u64,color:u32)->Option<u64>{WINDOWS.lock().get_mut(&hwnd).map(|w|w.pen.select_owned(pen,color))}
pub fn pen_selected(pen:u64)->bool{WINDOWS.lock().values().any(|w|w.pen.selected==pen)}
pub fn reset_pens(mut belongs:impl FnMut(u64)->bool){for w in WINDOWS.lock().values_mut(){if belongs(w.pen.selected){w.pen=crate::gdi_memory::PenState::new();}}}
pub fn window_text(hwnd:u32)->Option<crate::gdi_memory::TextState>{WINDOWS.lock().get(&hwnd).map(|w|w.text)}
pub fn replace_window_font(hwnd:u32,font:u64)->Option<u64>{WINDOWS.lock().get_mut(&hwnd).map(|w|core::mem::replace(&mut w.text.font,font))}
pub fn set_text_property(hwnd:u32,kind:u32,value:u32)->Option<u32>{WINDOWS.lock().get_mut(&hwnd)?.text.set(kind,value)}
pub fn font_selected(font:u64)->bool{WINDOWS.lock().values().any(|w|w.text.font==font)}
pub fn reset_fonts(mut belongs:impl FnMut(u64)->bool){for w in WINDOWS.lock().values_mut(){if belongs(w.text.font){w.text.font=crate::gdi_memory::DEFAULT_FONT;}}}

pub fn store_of(hwnd: u32) -> Option<(alloc::sync::Arc<crate::process::Section>, i32, i32)> {
    let g = WINDOWS.lock();
    let w = g.get(&hwnd)?;
    Some((w.store.clone()?, w.rect.2, w.rect.3))
}
pub fn owner_process(hwnd:u32)->Option<u64>{WINDOWS.lock().get(&hwnd).map(|w|w.owner_pid)}
pub fn title_of(hwnd:u32)->Option<String>{WINDOWS.lock().get(&hwnd).map(|w|w.title.clone())}
pub fn set_title(hwnd:u32,title:String)->bool{
    {let mut windows=WINDOWS.lock();let Some(w)=windows.get_mut(&hwnd) else{return false;};w.title=title;}
    emit([5,hwnd,0,0,0,0,0,0]);true
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

/// Called only after this thread is no longer running, without the scheduler lock.
/// Window stores stay alive while a compositor or an in-flight GDI call holds a view.
pub fn cleanup_thread(tid:u64) {
    crate::gdi::cleanup_thread(tid);
    let removed={
        let mut windows=WINDOWS.lock();
        let handles:alloc::vec::Vec<_>=windows.iter().filter(|(_,w)|w.owner_tid==tid).map(|(&h,_)|h).collect();
        handles.into_iter().filter_map(|h|windows.remove(&h).map(|w|(h,w))).collect::<alloc::vec::Vec<_>>()
    };
    QUEUES.lock().remove(&tid);
    for (hwnd,window) in removed {emit([2,hwnd,0,0,0,0,0,0]);drop(window);}
}
pub fn cleanup_process(pid:u64) {CLASSES.lock().cleanup(pid);crate::gdi::cleanup_process(pid);}

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
    let _=register_class_encoded(name,wndproc,0,false);
}
pub fn register_class_encoded(name:String,wndproc:u64,instance:u64,wide:bool)->Result<u16,u32>{CLASSES.lock().register(crate::process::current_pid(),instance,name,wndproc,wide)}
pub fn class_by_name(instance:u64,name:&str)->Option<crate::winclass::Class>{CLASSES.lock().by_name(crate::process::current_pid(),instance,name)}
pub fn class_by_atom(atom:u16)->Option<crate::winclass::Class>{CLASSES.lock().by_atom(crate::process::current_pid(),atom)}
pub fn is_unicode(hwnd:u32)->Option<bool>{WINDOWS.lock().get(&hwnd).map(|w|w.unicode)}

/// `CreateWindowExA`: `0` if `class` was never registered.
pub fn create_window(class: &str, x: i32, y: i32, w: i32, h: i32, owner_tid: u64) -> u32 {
    create_window_styled(class,x,y,w,h,owner_tid,0)
}
pub fn create_window_styled(class:&str,x:i32,y:i32,w:i32,h:i32,owner_tid:u64,style:u32)->u32 {
    let Some(class)=class_by_name(0,class) else{return 0;};
    create_window_class(class,x,y,w,h,owner_tid,style)
}
pub fn create_window_class(class:crate::winclass::Class,x:i32,y:i32,w:i32,h:i32,owner_tid:u64,style:u32)->u32 {
    if w<=0||h<=0||w>4096||h>4096||(w as u64)*(h as u64)*4>16<<20{return 0;}
    let store = if composing() {
        let Some(section)=crate::process::Section::try_zeroed(w as usize*h as usize*4) else{return 0;};
        Some(alloc::sync::Arc::new(section))
    }else{None};
    let hwnd = NEXT_HWND.fetch_add(1, Ordering::Relaxed);
    let comp = store.is_some();
    WINDOWS.lock().insert(hwnd, Window { wndproc:class.wndproc, unicode:class.wide, owner_tid, rect: (x, y, w, h), store, generation:1,brush:crate::gdi_memory::BrushState::new(),text:crate::gdi_memory::TextState::new(),pen:crate::gdi_memory::PenState::new(),owner_pid:crate::process::current_pid(),title:String::new() });
    if comp {
        emit([1, hwnd, x as u32, y as u32, w as u32, h as u32, 1, style]);
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
    {
        let mut queues=QUEUES.lock();
        let q=queues.entry(tid).or_default();
        let msg=Msg {hwnd,message,wparam,lparam};
        // Retain the latest position without moving it across button transitions.
        if message==0x200 && q.back().is_some_and(|m|m.hwnd==hwnd && m.message==message && m.wparam==wparam) {
            *q.back_mut().unwrap()=msg;
        } else {q.push_back(msg);}
    }
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
