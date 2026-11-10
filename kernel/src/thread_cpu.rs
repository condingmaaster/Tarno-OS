// SPDX-License-Identifier: GPL-2.0-or-later
//! Instrumented CPU/mode accounting. The small shared record outlives a
//! scheduler thread when a Windows handle is retained; no kernel stack or
//! Task is retained by that handle. No timer sampling or background worker.
use alloc::sync::Arc;
use spin::Mutex;
use crate::thread_cpu_accounting::{Mode,Usage};
pub type Shared=Arc<Mutex<Usage>>;
pub fn new()->Shared{Arc::new(Mutex::new(Usage::new(crate::timer::realtime_ns()).expect("u64 Unix nanoseconds fit FILETIME")))}
use core::sync::atomic::{AtomicPtr,Ordering};
static CURRENT:[AtomicPtr<Mutex<Usage>>;crate::gdt::MAX_CPUS]=[const{AtomicPtr::new(core::ptr::null_mut())};crate::gdt::MAX_CPUS];
/// Called with IRQs disabled while installing this CPU's current thread. The
/// scheduler-owned Arc keeps the pointee alive until replacement. Install the
/// next pointer before releasing the previous thread's running claim/reaping.
pub fn set_current(cpu:usize,usage:Option<&Shared>){let pointer=usage.map_or(core::ptr::null_mut(),|u|Arc::as_ptr(u) as *mut Mutex<Usage>);if CURRENT[cpu].load(Ordering::Relaxed)!=pointer{CURRENT[cpu].store(pointer,Ordering::Release);}}
/// Entry/exit shims run with IRQs disabled, so this CPU cannot switch between
/// borrowing its pointer and charging it. No global scheduler lock or Arc
/// refcount traffic on every syscall/IRQ boundary. Remote queries use their
/// owned Arc and this same short per-record mutex.
fn mode(mode:Mode){if crate::sched::is_started(){let cpu=crate::smp::this_cpu() as usize;let p=CURRENT[cpu].load(Ordering::Acquire);if !p.is_null(){unsafe{&*p}.lock().mode(mode,crate::timer::monotonic_ns());}}}
#[no_mangle]pub extern "C" fn thos_cpu_enter_kernel(){mode(Mode::Kernel);}
#[no_mangle]pub extern "C" fn thos_cpu_enter_user(){mode(Mode::User);}
/// IRQ shims save15GPRs followed by RIP,CS. Only an interrupted ring3 context
/// changes modes; nested kernel IRQs keep the current kernel interval.
#[no_mangle]pub extern "C" fn thos_cpu_irq_enter(frame:*const u64){if unsafe{*frame.add(16)}&3==3{thos_cpu_enter_kernel();}}
#[no_mangle]pub extern "C" fn thos_cpu_irq_leave(frame:*const u64){if unsafe{*frame.add(16)}&3==3{thos_cpu_enter_user();}}
