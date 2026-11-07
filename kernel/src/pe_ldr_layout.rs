// SPDX-License-Identifier: GPL-2.0-or-later
//! Complete bounded on-disk module metadata, before any user mapping.
extern crate alloc;
use alloc::vec::Vec;
pub const STRIDE:usize=0x80;
pub const MAX_MODULES:usize=64;
pub const MAX_BYTES:usize=128*1024;
pub struct Module<'a>{pub name:&'a str,pub full:&'a str,pub base:u64,pub entry:u64,pub size:u64}
pub fn build(base:u64,mods:&[Module<'_>])->Result<Vec<u8>,&'static str>{
 if mods.len()>MAX_MODULES{return Err("PE: too many loader entries");}
 let mut cursor=MAX_MODULES*STRIDE; // Stable entry area: appending never moves existing strings.
 let mut offsets=Vec::new();offsets.try_reserve_exact(mods.len()).map_err(|_|"PE: loader metadata allocation")?;
 for m in mods {
  if m.name.is_empty()||m.name.contains('\0')||m.full.contains('\0')||m.name.encode_utf16().count()>255{return Err("PE: invalid loader module name");}
  let mut fields=[(0usize,0usize);2];
  for (k,s) in [m.full,m.name].iter().enumerate(){
   let bytes=s.encode_utf16().count().checked_mul(2).filter(|n|*n<=65532).ok_or("PE: loader name too long")?;
   fields[k]=(cursor,bytes);cursor=cursor.checked_add(bytes+2).filter(|n|*n<=MAX_BYTES).ok_or("PE: loader strings too large")?;
  }
  offsets.push(fields);
 }
 let size=cursor.checked_add(4095).map(|n|n&!4095).filter(|n|*n<=MAX_BYTES).ok_or("PE: loader pages overflow")?;
 if base.checked_add(size as u64).filter(|n|*n<=0x0000_8000_0000_0000).is_none(){return Err("PE: loader address overflow");}
 let mut out=Vec::new();out.try_reserve_exact(size).map_err(|_|"PE: loader metadata allocation")?;out.resize(size,0);
 for (i,m) in mods.iter().enumerate(){
  let e=i*STRIDE;
  for (at,v) in [(0x30,m.base),(0x38,m.entry),(0x40,m.size)]{out[e+at..e+at+8].copy_from_slice(&v.to_le_bytes());}
  for(k,(field,s))in [(0x48,m.full),(0x58,m.name)].iter().enumerate(){
   let(at,n)=offsets[i][k];out[e+field..e+field+2].copy_from_slice(&(n as u16).to_le_bytes());out[e+field+2..e+field+4].copy_from_slice(&((n+2) as u16).to_le_bytes());out[e+field+8..e+field+16].copy_from_slice(&(base+at as u64).to_le_bytes());
   for(j,u)in s.encode_utf16().enumerate(){out[at+j*2..at+j*2+2].copy_from_slice(&u.to_le_bytes());}
  }
 }
 Ok(out)
}
#[cfg(test)]mod tests{
 use super::*;use alloc::{string::String,format};
 #[test]fn complete_long_names_cross_pages(){let names:Vec<_>=(0..20).map(|i|format!("{}-{i}.dll","a".repeat(200))).collect();let full:Vec<_>=names.iter().map(|s|format!("C:\\Windows\\System32\\{s}")).collect();let mods:Vec<_>=names.iter().zip(&full).enumerate().map(|(i,(n,f))|Module{name:n,full:f,base:i as u64+1,entry:0,size:4096}).collect();let b=build(0x10000,&mods).unwrap();assert!(b.len()>4096);for(i,m)in mods.iter().enumerate(){for(at,s)in[(0x48,m.full),(0x58,m.name)]{let e=i*STRIDE;let n=u16::from_le_bytes(b[e+at..e+at+2].try_into().unwrap())as usize;let p=u64::from_le_bytes(b[e+at+8..e+at+16].try_into().unwrap())as usize-0x10000;let units:Vec<_>=b[p..p+n].chunks_exact(2).map(|x|u16::from_le_bytes([x[0],x[1]])).collect();assert_eq!(String::from_utf16(&units).unwrap(),s);assert_eq!(&b[p+n..p+n+2],&[0,0]);}}}
 #[test]fn utf16_lengths_and_caps(){let m=Module{name:"Größe😀.dll",full:"C:\\Größe😀.dll",base:1,entry:2,size:3};let b=build(0x10000,&[m]).unwrap();assert_eq!(u16::from_le_bytes(b[0x58..0x5a].try_into().unwrap()),22);assert!(build(u64::MAX,&[]).is_err());let name="x".repeat(256);assert!(build(0x10000,&[Module{name:&name,full:&name,base:0,entry:0,size:0}]).is_err());let mods:Vec<_>=(0..65).map(|_|Module{name:"x",full:"x",base:0,entry:0,size:0}).collect();assert!(build(0x10000,&mods).is_err());}
}
