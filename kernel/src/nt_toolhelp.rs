// SPDX-License-Identifier: GPL-2.0-or-later
//! Read-only process snapshots in the ordinary per-task object table. Closing
//! the handle (or process exit) releases its immutable data and iterator.
use alloc::{sync::Arc,vec::Vec};
use spin::Mutex;
use crate::{process,sched,usercopy};
use crate::process_snapshot_layout::{Record,ENTRY_BYTES};
use core::sync::atomic::{AtomicUsize,Ordering};
static SNAPSHOT_BYTES:AtomicUsize=AtomicUsize::new(0);
const SNAPSHOT_BUDGET:usize=16*1024*1024;
struct Budget(usize);
impl Budget {
 fn claim(bytes:usize)->Result<Self,u32>{
  let mut old=SNAPSHOT_BYTES.load(Ordering::Relaxed);
  loop {let new=old.checked_add(bytes).filter(|n|*n<=SNAPSHOT_BUDGET).ok_or(8u32)?;
   match SNAPSHOT_BYTES.compare_exchange_weak(old,new,Ordering::AcqRel,Ordering::Relaxed){Ok(_)=>return Ok(Self(bytes)),Err(v)=>old=v}
  }
 }
}
impl Drop for Budget {fn drop(&mut self){SNAPSHOT_BYTES.fetch_sub(self.0,Ordering::AcqRel);}}
pub struct Snapshot {rows:Vec<Record>,position:Mutex<Option<usize>>,_budget:Budget}
impl Snapshot {
 fn capture()->Result<Arc<Self>,u32>{
  let tasks=process::try_live_tasks(4096).map_err(|_|8u32)?;
  let budget=Budget::claim(tasks.len()*core::mem::size_of::<Record>()+core::mem::size_of::<Self>()+32)?;
  let mut rows=Vec::new();rows.try_reserve_exact(tasks.len()).map_err(|_|8u32)?;
  for t in tasks {rows.push(Record{pid:u32::try_from(t.pid).map_err(|_|534u32)?,parent:u32::try_from(t.ppid).map_err(|_|534u32)?,threads:t.snapshot_thread_count(),name:t.snapshot_exe_name()});}
  Ok(Arc::new(Self{rows,position:Mutex::new(None),_budget:budget}))
 }
}
pub fn create(flags:u32,_pid:u32)->Result<i64,u32>{
 if flags&!0x8000_001f!=0||flags==0{return Err(87);}
 // Threads/modules/heaps and Windows handle inheritance need their own real
 // contracts; never silently return a process-only snapshot for those flags.
 if flags!=2{return Err(50);}
 let task=sched::current().task().ok_or(6u32)?;
 let snapshot=Snapshot::capture()?;
 task.try_handle_alloc(process::HandleObject::Toolhelp(snapshot),true,4096).map(|h|h as i64).map_err(|_|8)
}
pub fn read(handle:u64,entry:u64,first:bool)->Result<(),u32>{
 if handle>i32::MAX as u64{return Err(6);}
 let task=sched::current().task().ok_or(6u32)?;
 let snapshot=task.handle_toolhelp(handle as i32).ok_or(6u32)?;
 if !usercopy::user_ok(entry,4,false){return Err(998);}
 if usercopy::get::<u32>(entry)<ENTRY_BYTES as u32{return Err(24);}
 let out=usercopy::slice_mut(entry,ENTRY_BYTES).map_err(|_|998u32)?;
 let mut position=snapshot.position.lock();
 let index=if first{0}else{position.map(|v|v+1).ok_or(18u32)?};
 let row=snapshot.rows.get(index).ok_or(18u32)?;
 out.copy_from_slice(&row.bytes());*position=Some(index);Ok(())
}
