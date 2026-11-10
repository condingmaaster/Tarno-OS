// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded counted UTF-16 registry names. No lossy '?' aliases or truncation.
//! Native registry policy still folds ASCII only; this is not full NT Unicode
//! case mapping. Ill-formed UTF-16 and embedded NUL are explicitly refused.
extern crate alloc;
use alloc::string::String;
#[derive(Clone,Copy,Debug,PartialEq,Eq)]
pub enum Error { Invalid, NoMemory }
pub fn decode(bytes:&[u8],length:u16,maximum:u16)->Result<String,Error>{
    let length=length as usize;
    if length%2!=0||length>maximum as usize||bytes.len()!=length{return Err(Error::Invalid);}
    let mut out=String::new();
    out.try_reserve_exact(length/2*3).map_err(|_|Error::NoMemory)?;
    for c in core::char::decode_utf16(bytes.chunks_exact(2).map(|b|u16::from_le_bytes([b[0],b[1]]))){
        let c=c.map_err(|_|Error::Invalid)?;
        if c=='\0'{return Err(Error::Invalid);}
        out.push(c);
    }
    Ok(out)
}
#[cfg(test)]mod tests{
 use super::*;
 fn bytes(s:&str)->alloc::vec::Vec<u8>{s.encode_utf16().flat_map(u16::to_le_bytes).collect()}
 #[test]fn unicode_and_surrogate_pairs_roundtrip(){for s in ["","Größe😀","目录/ключ","x"].iter(){let b=bytes(s);assert_eq!(decode(&b,b.len() as u16,b.len() as u16).unwrap(),*s);}}
 #[test]fn names_past_old_260_unit_limit_are_distinct(){let a="x".repeat(299)+"A";let b="x".repeat(299)+"B";for s in [a,b]{let bytes=bytes(&s);assert_eq!(decode(&bytes,600,600).unwrap(),s);}}
 #[test]fn maximum_counted_name_is_preserved(){let name="x".repeat(32767);let b=bytes(&name);assert_eq!(decode(&b,65534,65534).unwrap(),name);}
 #[test]fn malformed_headers_and_sequences_refused(){assert_eq!(decode(&[1],1,1),Err(Error::Invalid));assert_eq!(decode(&[1,0],2,0),Err(Error::Invalid));assert_eq!(decode(&[],2,2),Err(Error::Invalid));for b in [[0,0],[0,0xd8],[0,0xdc]]{assert_eq!(decode(&b,2,2),Err(Error::Invalid));}}
 #[test]fn all_scalar_values_roundtrip(){for c in (0..=0x10ffff).filter_map(char::from_u32).filter(|c|*c!='\0'){let mut buf=[0u8;4];let s:&str=c.encode_utf8(&mut buf);let b=bytes(s);assert_eq!(decode(&b,b.len() as u16,b.len() as u16).unwrap(),s);}}
}
