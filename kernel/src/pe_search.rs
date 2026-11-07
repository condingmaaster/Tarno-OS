// SPDX-License-Identifier: GPL-2.0-or-later
//! Process-local DLL directory selection. No filesystem or user pointers.
extern crate alloc;
use alloc::{string::String,vec::Vec,format};
pub const APPLICATION:u32=0x200;
pub const USER:u32=0x400;
pub const SYSTEM32:u32=0x800;
pub const DEFAULT:u32=0x1000;
pub const DLL_LOAD_DIR:u32=0x100;
const MASK:u32=APPLICATION|USER|SYSTEM32|DEFAULT;
pub fn default_flags(flags:u32)->Result<u32,u32>{if flags==0||flags&!MASK!=0{return Err(87);}Ok(expand(flags))}
fn expand(flags:u32)->u32{if flags&DEFAULT!=0{(flags&!DEFAULT)|APPLICATION|USER|SYSTEM32}else{flags}}
pub fn paths(default:u32,call:u32,explicit:bool,path:&str,app:&str)->Result<Vec<String>,u32>{
 if call&!(MASK|DLL_LOAD_DIR)!=0{return Err(50);}if call&DLL_LOAD_DIR!=0&&!explicit{return Err(87);}
 if explicit{return Ok(alloc::vec![String::from(path)]);}
 let flags=expand(if call==0{default}else{call});let name=path.rsplit('/').next().ok_or(87u32)?;let mut v=Vec::new();
 if flags&APPLICATION!=0{v.push(format!("{}/{name}",app.trim_end_matches('/')));}
 // USER has no entries until AddDllDirectory/SetDllDirectory is implemented.
 if flags&SYSTEM32!=0{let p=format!("/Windows/System32/{name}");if !v.iter().any(|q|q==&p){v.push(p);}}
 Ok(v)
}
#[cfg(test)]mod tests{use super::*;
 #[test]fn policies_and_per_call_override(){assert_eq!(default_flags(0),Err(87));assert_eq!(default_flags(DLL_LOAD_DIR),Err(87));assert_eq!(default_flags(DEFAULT).unwrap(),APPLICATION|USER|SYSTEM32);let p=paths(SYSTEM32,0,false,"/Windows/System32/x.dll","/app").unwrap();assert_eq!(p,["/Windows/System32/x.dll"]);assert_eq!(paths(SYSTEM32,APPLICATION,false,"x.dll","/app").unwrap(),["/app/x.dll"]);assert_eq!(paths(SYSTEM32,DEFAULT,false,"x.dll","/app").unwrap(),["/app/x.dll","/Windows/System32/x.dll"]);assert_eq!(paths(USER,0,false,"x.dll","/app").unwrap().len(),0);}
 #[test]fn full_path_and_unsupported_modes(){assert_eq!(paths(SYSTEM32,APPLICATION,true,"/other/x.dll","/app").unwrap(),["/other/x.dll"]);assert_eq!(paths(SYSTEM32,DLL_LOAD_DIR,false,"x.dll","/app"),Err(87));assert_eq!(paths(SYSTEM32,2,false,"x.dll","/app"),Err(50));assert_eq!(paths(SYSTEM32,0x80000000,false,"x.dll","/app"),Err(50));assert_eq!(paths(SYSTEM32,DEFAULT,false,"x.dll","/Windows/System32").unwrap(),["/Windows/System32/x.dll"]);}
}
