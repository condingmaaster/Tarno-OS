// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded UTF-8/UTF-16 Windows conversions, independent of user pointers.
extern crate alloc;
use alloc::vec::Vec;
pub const INVALID_UNICODE:u32=1113;
pub const NO_MEMORY:u32=8;
pub fn to_wide(input:&[u8],strict:bool)->Result<Vec<u16>,u32>{
    let mut output=Vec::new();output.try_reserve_exact(input.len()).map_err(|_|NO_MEMORY)?;
    if strict {
        let text=core::str::from_utf8(input).map_err(|_|INVALID_UNICODE)?;
        output.extend(text.encode_utf16());
    } else {
        // Process valid spans without allocating a lossy intermediate string.
        let mut rest=input;
        while !rest.is_empty(){
            match core::str::from_utf8(rest){
                Ok(text)=>{output.extend(text.encode_utf16());break;}
                Err(error)=>{
                    let valid=error.valid_up_to();
                    output.extend(core::str::from_utf8(&rest[..valid]).unwrap().encode_utf16());
                    output.push(0xfffd);
                    let skipped=error.error_len().unwrap_or(rest.len()-valid);
                    rest=&rest[valid+skipped..];
                }
            }
        }
    }
    Ok(output)
}
pub fn to_utf8(input:&[u16],strict:bool)->Result<Vec<u8>,u32>{
    let mut output=Vec::new();let cap=input.len().checked_mul(3).ok_or(NO_MEMORY)?;
    output.try_reserve_exact(cap).map_err(|_|NO_MEMORY)?;
    for decoded in core::char::decode_utf16(input.iter().copied()){
        let ch=match decoded{Ok(ch)=>ch,Err(_) if strict=>return Err(INVALID_UNICODE),Err(_)=>'\u{fffd}'};
        let mut bytes=[0;4];output.extend_from_slice(ch.encode_utf8(&mut bytes).as_bytes());
    }
    Ok(output)
}
#[cfg(test)]mod tests{
 use super::*;
 #[test]fn known_values(){assert_eq!(to_wide("Aä€😀\0".as_bytes(),true).unwrap(),[65,0xe4,0x20ac,0xd83d,0xde00,0]);assert_eq!(to_utf8(&[65,0xe4,0x20ac,0xd83d,0xde00,0],true).unwrap(),"Aä€😀\0".as_bytes());}
 #[test]fn invalid_utf8(){for bytes in [&b"\xc0\xaf"[..],&b"\xed\xa0\x80"[..],&b"\xf4\x90\x80\x80"[..],&b"\xe2\x82"[..]] {assert_eq!(to_wide(bytes,true),Err(INVALID_UNICODE));assert_eq!(to_wide(bytes,false).unwrap(),alloc::string::String::from_utf8_lossy(bytes).encode_utf16().collect::<Vec<_>>());}}
 #[test]fn invalid_utf16(){assert_eq!(to_utf8(&[0xd800,65,0xdc00],true),Err(INVALID_UNICODE));assert_eq!(to_utf8(&[0xd800,65,0xdc00],false).unwrap(),"�A�".as_bytes());}
 #[test]fn every_scalar_roundtrip(){for value in 0..=0x10ffff{if let Some(ch)=char::from_u32(value){let mut bytes=[0;4];let text=ch.encode_utf8(&mut bytes);let wide=to_wide(text.as_bytes(),true).unwrap();assert_eq!(wide,ch.encode_utf16(&mut[0;2]));assert_eq!(to_utf8(&wide,true).unwrap(),text.as_bytes());}}}
}
