// SPDX-License-Identifier: GPL-2.0-or-later
//! Queue leases protect the interval between lookup and enqueue.
//! All access is serialized by the caller's registry lock.
extern crate alloc;
use alloc::{collections::BTreeMap,sync::Arc};
pub struct Registry<Q>{queues:BTreeMap<u64,Arc<Q>>}
impl<Q> Registry<Q>{
 pub const fn new()->Self{Self{queues:BTreeMap::new()}}
 pub fn lease(&mut self,key:u64,new:impl FnOnce()->Q)->Arc<Q>{self.queues.entry(key).or_insert_with(||Arc::new(new())).clone()}
 pub fn get(&self,key:u64)->Option<Arc<Q>>{self.queues.get(&key).cloned()}
 pub fn retire_if_idle(&mut self,key:u64,q:&Arc<Q>,idle:impl FnOnce(&Q)->bool)->bool{
  // Registry + this lease must be the only references. An empty queue can
  // still have a waiter between lookup and its atomic check/enqueue.
  if self.queues.get(&key).is_some_and(|current|Arc::ptr_eq(current,q))&&Arc::strong_count(q)==2&&idle(q){self.queues.remove(&key);true}else{false}
 }
}
#[cfg(test)]mod tests{
 use super::*;use core::sync::atomic::{AtomicBool,Ordering};
 #[test]fn staged_waiter_prevents_orphan_queue(){
  let mut r=Registry::new();let waiter=r.lease(42,||AtomicBool::new(true));
  let wake=r.get(42).unwrap();assert!(wake.load(Ordering::Relaxed));
  // The old empty-only retirement removed this queue here; the waiter would
  // subsequently enqueue on an unreachable Arc and miss every future wake.
  assert!(!r.retire_if_idle(42,&wake,|q|q.load(Ordering::Relaxed)));
  let future_wake=r.get(42).unwrap();assert!(Arc::ptr_eq(&waiter,&future_wake));drop(future_wake);
  waiter.store(false,Ordering::Relaxed);drop(waiter);assert!(!r.retire_if_idle(42,&wake,|q|q.load(Ordering::Relaxed)));
  wake.store(true,Ordering::Relaxed);assert!(r.retire_if_idle(42,&wake,|q|q.load(Ordering::Relaxed)));assert!(r.get(42).is_none());
 }
 #[test]fn idle_return_reclaims_registry_entry_without_reusing_live_queue(){
  let mut r=Registry::new();let old=r.lease(7,||());assert!(r.retire_if_idle(7,&old,|_|true));
  let next=r.lease(7,||());assert!(!Arc::ptr_eq(&old,&next));assert!(!r.retire_if_idle(7,&old,|_|true));assert!(r.retire_if_idle(7,&next,|_|true));
 }
}
