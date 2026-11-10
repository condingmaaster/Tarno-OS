// SPDX-License-Identifier: GPL-2.0-or-later
//! A genuine waitable thread handle, distinct from a writable Event handle.
use alloc::sync::Arc;
use core::sync::atomic::{AtomicBool,AtomicU32,AtomicU64,Ordering};
pub struct Thread {pub usage:crate::thread_cpu::Shared,pub event:Arc<crate::wait::Event>,tid:AtomicU64,finished:AtomicBool,code:AtomicU32}
impl Thread {
 pub fn new()->Arc<Self>{Arc::new(Self{usage:crate::thread_cpu::new(),event:Arc::new(crate::wait::Event::new()),tid:AtomicU64::new(0),finished:AtomicBool::new(false),code:AtomicU32::new(259)})}
 pub fn bind(&self,tid:u64){self.tid.store(tid,Ordering::Release);}
 pub fn tid(&self)->u64{self.tid.load(Ordering::Acquire)}
 pub fn code(&self)->u32{if self.finished.load(Ordering::Acquire){self.code.load(Ordering::Relaxed)}else{259}}
 pub fn finish(&self,code:u32){self.usage.lock().finish(crate::timer::monotonic_ns(),crate::timer::realtime_ns());self.code.store(code,Ordering::Relaxed);self.finished.store(true,Ordering::Release);self.event.signal();}
}
pub fn object(handle:u64)->Result<Arc<Thread>,u32>{
 if handle>i32::MAX as u64{return Err(6);}
 crate::sched::current().task().and_then(|t|t.handle_thread(handle as i32)).ok_or(6)
}
pub fn id(handle:u64)->Result<u32,u32>{
 let tid=if handle==(-2i64 as u64){crate::process::current_tid()}else{object(handle)?.tid()};
 u32::try_from(tid).map_err(|_|534)
}
pub fn exit_code(handle:u64,out:u64)->Result<(),u32>{
 let code=if handle==(-2i64 as u64){259}else{object(handle)?.code()};
 if out==0{return Err(87);}let bytes=crate::usercopy::slice_mut(out,4).map_err(|_|998u32)?;bytes.copy_from_slice(&code.to_le_bytes());Ok(())
}

/// Validate every output before writing any; a retained handle owns only the
/// usage record, not the exited thread's kernel stack/address space.
pub fn times(handle:u64,outputs:[u64;4])->Result<(),u32>{
 let usage=if handle==(-2i64 as u64){crate::sched::current().cpu_usage.clone().ok_or(50u32)?}else{object(handle)?.usage.clone()};
 for p in outputs {if p==0{return Err(87);}if !crate::usercopy::user_ok(p,8,true){return Err(998);}}
 let t=usage.lock().snapshot(crate::timer::monotonic_ns());
 for(p,value)in outputs.into_iter().zip([t.creation,t.exit,t.kernel,t.user]){crate::usercopy::slice_mut(p,8).map_err(|_|998u32)?.copy_from_slice(&value.to_le_bytes());}Ok(())
}
