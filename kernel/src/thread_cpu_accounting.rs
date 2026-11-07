// SPDX-License-Identifier: GPL-2.0-or-later
//! Pure timing state machine used by the instrumented scheduler.
//! Real monotonic timestamps must be supplied at CPU/mode transitions. Wall
//! time is separate; waiting/runnable-but-unscheduled time never becomes CPU
//! time. Private integration requires actual PE and scheduling proof.
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub enum Mode {User,Kernel}
#[derive(Debug)]
pub struct Usage {created:u64,exited:Option<u64>,user_ns:u64,kernel_ns:u64,mode:Mode,anchor:Option<u64>}
#[derive(Debug,PartialEq,Eq)]
pub struct Times {pub creation:u64,pub exit:u64,pub user:u64,pub kernel:u64}
const UNIX_FILETIME:u64=116_444_736_000_000_000;
pub fn filetime(utc_ns:u64)->Option<u64>{(utc_ns/100).checked_add(UNIX_FILETIME)}
impl Usage {
 pub fn new(utc_ns:u64)->Option<Self>{Some(Self{created:filetime(utc_ns)?,exited:None,user_ns:0,kernel_ns:0,mode:Mode::Kernel,anchor:None})}
 fn charge(&mut self,now:u64){if let Some(start)=self.anchor {let now=now.max(start);let delta=now-start;match self.mode{Mode::User=>self.user_ns=self.user_ns.saturating_add(delta),Mode::Kernel=>self.kernel_ns=self.kernel_ns.saturating_add(delta)}self.anchor=Some(now);}}
 /// Resume after a scheduling pause. A duplicate resume does not discard time.
 pub fn resume(&mut self,now:u64){if self.exited.is_none()&&self.anchor.is_none(){self.anchor=Some(now);}}
 pub fn mode(&mut self,mode:Mode,now:u64){if self.exited.is_none(){self.charge(now);self.mode=mode;}}
 pub fn pause(&mut self,now:u64){self.charge(now);self.anchor=None;}
 pub fn finish(&mut self,now:u64,utc_ns:u64)->Option<()>{if self.exited.is_none(){let exit=filetime(utc_ns)?;self.pause(now);self.exited=Some(exit);}Some(())}
 /// A query includes this currently running interval without mutating anchors.
 pub fn snapshot(&self,now:u64)->Times{let delta=self.anchor.map_or(0,|start|now.saturating_sub(start));Times{creation:self.created,exit:self.exited.unwrap_or(0),user:self.user_ns.saturating_add(if self.mode==Mode::User {delta}else{0})/100,kernel:self.kernel_ns.saturating_add(if self.mode==Mode::Kernel {delta}else{0})/100}}
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn blocked_is_not_cpu_time(){let mut s=Usage::new(1_000_000_000).unwrap();s.resume(10);s.mode(Mode::User,110);s.pause(610);let before=s.snapshot(610);assert_eq!(before.user,5);assert_eq!(before.kernel,1);assert_eq!(s.snapshot(50_000_000_000),before);s.resume(50_000_000_000);s.mode(Mode::Kernel,50_000_000_200);assert_eq!(s.snapshot(50_000_000_300).user,7);assert_eq!(s.snapshot(50_000_000_300).kernel,2);}
 #[test]fn finish_freezes_and_keeps_actual_wall_time(){let mut s=Usage::new(1_000_000_000).unwrap();s.resume(0);s.mode(Mode::User,200);s.finish(1000,3_000_000_000).unwrap();let before=s.snapshot(1000);assert_eq!(before.creation,UNIX_FILETIME+10_000_000);assert_eq!(before.exit,UNIX_FILETIME+30_000_000);assert_eq!((before.user,before.kernel),(8,2));s.resume(9000);s.mode(Mode::Kernel,9500);s.finish(9999,99_000_000_000).unwrap();assert_eq!(s.snapshot(99999),before);}
 #[test]fn subhundred_ns_intervals_accumulate(){let mut s=Usage::new(0).unwrap();for j in 0..1000 {let now=j*10;s.resume(now);s.mode(Mode::User,now);s.pause(now+1);}assert_eq!(s.snapshot(999999).user,10);assert_eq!(s.snapshot(999999).kernel,0);}
 #[test]fn queries_and_duplicate_resume_do_not_double_charge(){let mut s=Usage::new(0).unwrap();s.resume(100);s.mode(Mode::User,100);for j in 1..1000 {s.resume(j+100);let _=s.snapshot(j+100);}assert_eq!(s.snapshot(1100).user,10);s.pause(1100);assert_eq!(s.snapshot(999999).user,10);}
 #[test]fn backwards_sample_never_wraps(){let mut s=Usage::new(0).unwrap();s.resume(1000);s.mode(Mode::User,900);assert_eq!(s.snapshot(800).user,0);assert_eq!(s.snapshot(1200).user,2);}
 #[test]fn generated_work_and_wait_conserve_elapsed_time(){let mut s=Usage::new(0).unwrap();let(mut now,mut user,mut kernel)=(0u64,0u64,0u64);let mut random=1337u64;for _ in 0..100_000 {random=random.wrapping_mul(6364136223846793005).wrapping_add(1);now+=random%10000;s.resume(now);let(u,k)=((random>>16)%1000,(random>>32)%1000);s.mode(Mode::User,now);now+=u;user+=u;s.mode(Mode::Kernel,now);now+=k;kernel+=k;s.pause(now);let t=s.snapshot(now+10_000_000);assert_eq!(t.user,user/100);assert_eq!(t.kernel,kernel/100);}}
}
