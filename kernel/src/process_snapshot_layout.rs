// SPDX-License-Identifier: GPL-2.0-or-later
//! Win64 PROCESSENTRY32W byte layout. No host-sized struct casts or allocation.
pub const ENTRY_BYTES:usize=568;
#[derive(Clone)]
pub struct Record {pub pid:u32,pub parent:u32,pub threads:u32,pub name:[u16;260]}
pub fn basename(path:&str)->[u16;260]{
 let mut out=[0;260];let base=path.rsplit(|c|c=='/'||c=='\\').next().unwrap_or("");
 let mut used=0;
 for c in base.chars(){let mut units=[0;2];let encoded=c.encode_utf16(&mut units);if used+encoded.len()>259{break;}out[used..used+encoded.len()].copy_from_slice(encoded);used+=encoded.len();}
 out
}
impl Record {
 pub fn bytes(&self)->[u8;ENTRY_BYTES]{
  let mut out=[0;ENTRY_BYTES];
  for (at,value) in [(0,ENTRY_BYTES as u32),(8,self.pid),(28,self.threads),(32,self.parent),(36,8)] {out[at..at+4].copy_from_slice(&value.to_le_bytes());}
  // THOS's scheduler currently has one normal priority class; expose its
  // Windows NORMAL_PRIORITY_CLASS base value, not a claimed priority API.
  for(i,c)in self.name.iter().enumerate(){out[44+i*2..46+i*2].copy_from_slice(&c.to_le_bytes());}out
 }
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn layout(){let r=Record{pid:123,parent:7,threads:5,name:basename("C:\\tmp\\hello.exe")};let b=r.bytes();assert_eq!(u32::from_le_bytes(b[..4].try_into().unwrap()),568);assert_eq!(u32::from_le_bytes(b[8..12].try_into().unwrap()),123);assert_eq!(u32::from_le_bytes(b[28..32].try_into().unwrap()),5);assert_eq!(u32::from_le_bytes(b[32..36].try_into().unwrap()),7);assert_eq!(u32::from_le_bytes(b[36..40].try_into().unwrap()),8);assert_eq!(b[44],b'h');assert_eq!(b[62],0);for range in [4..8,12..28,40..44,564..568]{assert!(b[range].iter().all(|x|*x==0));}}
 #[test]fn unicode(){let n=basename("/tmp/😀-ä.exe");assert_eq!(&n[..4],&[0xd83d,0xde00,45,0xe4]);}
 #[test]fn truncation_keeps_pairs(){let mut s=std::string::String::new();for _ in 0..258{s.push('x');}s.push('😀');let n=basename(&s);assert_eq!(n[257],120);assert_eq!(n[258],0);assert_eq!(n[259],0);}
 #[test]fn terminates(){assert_eq!(basename("")[0],0);let n=basename(&"x".repeat(400));assert_eq!(n[258],120);assert_eq!(n[259],0);}
}
