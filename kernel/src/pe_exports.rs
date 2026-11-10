// SPDX-License-Identifier: GPL-2.0-or-later
//! Validate a materialised PE export table before allocating owned metadata.
extern crate alloc;
use alloc::{collections::BTreeMap,string::String,vec::Vec};
#[derive(Clone,Debug)]
pub enum Export { Empty, Addr(u64), Forward(String) }
const MAX_COUNT:usize=65536;
const MAX_TEXT:usize=2*1024*1024;
fn span(img:&[u8],at:usize,n:usize)->Result<&[u8],&'static str>{img.get(at..at.checked_add(n).ok_or("PE: export range overflow")?).ok_or("PE: export range outside image")}
fn u32at(img:&[u8],at:usize)->Result<u32,&'static str>{Ok(u32::from_le_bytes(span(img,at,4)?.try_into().unwrap()))}
fn text(img:&[u8],at:usize,end:usize)->Result<&str,&'static str>{
 if at==0||at>=end||end>img.len(){return Err("PE: export string range");}
 let s=span(img,at,(end-at).min(256))?;let n=s.iter().position(|b|*b==0).ok_or("PE: unterminated export string")?;
 if n==0{return Err("PE: empty export string");}
 core::str::from_utf8(&s[..n]).map_err(|_|"PE: non-UTF8 export string")
}
pub fn parse(img:&[u8],base:u64,rva:u32,size:u32)->Result<(Vec<Export>,u32,BTreeMap<String,usize>),&'static str>{
 if rva==0{return Ok((Vec::new(),1,BTreeMap::new()));}
 if base==0||base.checked_add(img.len()as u64).filter(|n|*n<=0x0000_8000_0000_0000).is_none(){return Err("PE: export image address");}
 let ed=rva as usize;let end=ed.checked_add(size as usize).ok_or("PE: export directory overflow")?;
 if size<40{return Err("PE: export directory too small");}span(img,ed,size as usize)?;
 let ord_base=u32at(img,ed+16)?;let nf=u32at(img,ed+20)?as usize;let nn=u32at(img,ed+24)?as usize;
 if nf>MAX_COUNT||nn>MAX_COUNT||ord_base.checked_add(nf.saturating_sub(1)as u32).is_none(){return Err("PE: absurd export count");}
 let eat=u32at(img,ed+28)?as usize;let names=u32at(img,ed+32)?as usize;let ords=u32at(img,ed+36)?as usize;
 for(at,n,stride)in[(eat,nf,4),(names,nn,4),(ords,nn,2)]{if n!=0&&at==0{return Err("PE: null export table");}span(img,at,n*stride)?;}
 let mut bytes=0usize;
 // No owned allocations until every RVA, name, ordinal and forwarder is valid.
 for i in 0..nf{let r=u32at(img,eat+i*4)?as usize;if r==0{continue;}if r>=ed&&r<end{let s=text(img,r,end)?;bytes=bytes.checked_add(s.len()).filter(|n|*n<=MAX_TEXT).ok_or("PE: export text budget")?;}else if r>=img.len(){return Err("PE: export target outside image");}}
 for i in 0..nn{let idx=u16::from_le_bytes(span(img,ords+i*2,2)?.try_into().unwrap())as usize;if idx>=nf{return Err("PE: export ordinal outside table");}let s=text(img,u32at(img,names+i*4)?as usize,img.len())?;bytes=bytes.checked_add(s.len()).filter(|n|*n<=MAX_TEXT).ok_or("PE: export text budget")?;}
 let mut exports=Vec::new();exports.try_reserve_exact(nf).map_err(|_|"PE: export metadata allocation")?;
 for i in 0..nf{let r=u32at(img,eat+i*4)?as usize;exports.push(if r==0{Export::Empty}else if r>=ed&&r<end{Export::Forward(String::from(text(img,r,end)?))}else{Export::Addr(base+r as u64)});}
 let mut by_name=BTreeMap::new();for i in 0..nn{let s=text(img,u32at(img,names+i*4)?as usize,img.len())?;let idx=u16::from_le_bytes(span(img,ords+i*2,2)?.try_into().unwrap())as usize;by_name.insert(String::from(s),idx);}
 Ok((exports,ord_base,by_name))
}
#[cfg(test)]mod tests{
 use super::*;use alloc::vec;
 fn set(b:&mut[u8],o:usize,v:u32){b[o..o+4].copy_from_slice(&v.to_le_bytes());}
 fn image()->Vec<u8>{let mut b=vec![0;4096];for(o,v)in[(16,1),(20,2),(24,1),(28,512),(32,528),(36,540)]{set(&mut b,256+o,v);}set(&mut b,512,1024);set(&mut b,516,350);set(&mut b,528,600);b[350..358].copy_from_slice(b"foo.Bar\0");b[600..605].copy_from_slice(b"name\0");b}
 #[test]fn valid_code_forwarder_and_empty(){let b=image();let(e,o,n)=parse(&b,0x10000,256,128).unwrap();assert_eq!(o,1);assert!(matches!(e[0],Export::Addr(0x10400)));assert!(matches!(&e[1],Export::Forward(s)if s=="foo.Bar"));assert_eq!(n["name"],0);assert!(parse(&[],1,0,0).unwrap().0.is_empty());}
 #[test]fn rejects_counts_tables_ordinals_targets_and_string_escape(){for(o,v)in[(276,65537),(280,65537),(284,4094),(288,4095),(292,4095),(512,4096),(528,4096)]{let mut b=image();set(&mut b,o,v);assert!(parse(&b,0x10000,256,128).is_err(),"offset {o}");}let mut b=image();b[540]=2;assert!(parse(&b,0x10000,256,128).is_err());let mut b=image();b[350..384].fill(b'a');b[384]=0;assert!(parse(&b,0x10000,256,128).is_err());assert!(parse(&image(),u64::MAX-1,256,128).is_err());assert!(parse(&image(),0x10000,256,39).is_err());}
 #[test]fn malformed_mutations_never_panic(){for at in 0..640{for v in [0u8,127,255]{let mut b=image();b[at]=v;let _=parse(&b,0x10000,256,128);}}for len in 0..4096{let b=image();let _=parse(&b[..len],0x10000,256,128);}}
}
