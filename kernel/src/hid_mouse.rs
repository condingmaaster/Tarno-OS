// SPDX-License-Identifier: GPL-2.0-or-later
//! Bounded HID input-report parser for relative mouse axes and conventional buttons.
//! Unsupported layouts fall back to the negotiated three-byte boot protocol.
extern crate alloc;
use alloc::vec::Vec;
#[derive(Clone,Copy,Debug,PartialEq)]
pub struct Sample {pub buttons:u8,pub dx:i32,pub dy:i32,pub wheel:i32}
#[derive(Clone,Copy,Debug)]
struct Field {bit:u16,size:u8,signed:bool}
#[derive(Clone,Copy,Debug)]
pub struct Format {id:u8,bits:u16,buttons:[Option<Field>;3],x:Option<Field>,y:Option<Field>,wheel:Option<Field>}
impl Format {
    fn empty(id:u8)->Self {Self{id,bits:0,buttons:[None;3],x:None,y:None,wheel:None}}
    pub fn boot()->Self {Self{id:0,bits:24,buttons:[Some(Field{bit:0,size:1,signed:false}),Some(Field{bit:1,size:1,signed:false}),Some(Field{bit:2,size:1,signed:false})],x:Some(Field{bit:8,size:8,signed:true}),y:Some(Field{bit:16,size:8,signed:true}),wheel:None}}
    pub fn bytes(&self)->usize {(self.bits as usize).div_ceil(8)+usize::from(self.id!=0)}
    pub fn has_wheel(&self)->bool {self.wheel.is_some()}
    pub fn decode(&self,report:&[u8])->Option<Sample> {
        if report.len()<self.bytes(){return None;}
        let data=if self.id!=0 {if report[0]!=self.id{return None;} &report[1..]}else{report};
        let mut buttons=0;for(i,f)in self.buttons.iter().enumerate(){if let Some(f)=f {if extract(data,*f)?!=0{buttons|=1<<i;}}}
        Some(Sample{buttons,dx:extract(data,self.x?)?,dy:extract(data,self.y?)?,wheel:match self.wheel{Some(f)=>extract(data,f)?,None=>0}})
    }
}
fn extract(data:&[u8],f:Field)->Option<i32> {
    if f.size==0||f.size>16||f.bit as usize+f.size as usize>data.len()*8{return None;}
    let byte=f.bit as usize/8;let shift=f.bit as usize%8;
    let mut word=0u32;for i in 0..3 {if let Some(b)=data.get(byte+i){word|=(*b as u32)<<(8*i);}}
    let value=(word>>shift)&((1u32<<f.size)-1);
    Some(if f.signed {((value<<(32-f.size)) as i32)>>(32-f.size)}else{value as i32})
}
#[derive(Clone,Copy,Default)]
struct Global {page:u32,size:u32,count:u32,id:u8,minimum:i32}
fn signed(value:u32,bytes:usize)->i32 {if bytes==4{value as i32}else if bytes==0{0}else{((value<<(32-bytes*8)) as i32)>>(32-bytes*8)}}
fn usage(value:u32,page:u32)->u32 {if value>0xffff{value}else{(page<<16)|value}}
pub fn parse(descriptor:&[u8])->Option<Format> {
    let mut global=Global::default();let mut stack=Vec::new();let mut usages=Vec::new();let mut range=None;
    let mut reports:Vec<Format>=Vec::new();let(mut off,mut collections)=(0usize,0usize);
    while off<descriptor.len() {
        let prefix=descriptor[off];off+=1;if prefix==0xfe{return None;}
        let size=match prefix&3{3=>4,n=>n as usize};let end=off.checked_add(size)?;
        if end>descriptor.len(){return None;}
        let mut value=0u32;for(i,b)in descriptor[off..end].iter().enumerate(){value|=(*b as u32)<<(8*i);}off=end;
        let kind=(prefix>>2)&3;let tag=prefix>>4;
        match kind {
            1=>match tag {
                0=>{if value>65535{return None;}global.page=value;},1=>global.minimum=signed(value,size),7=>global.size=value,8=>{if value==0||value>255{return None;}global.id=value as u8;},9=>global.count=value,
                10=>{if stack.len()>=8{return None;}stack.push(global);},11=>global=stack.pop()?,_=>{}
            },
            2=>match tag {
                0=>{if usages.len()>=512{return None;}usages.push(usage(value,global.page));},
                1=>range=Some((usage(value,global.page),None)),2=>{if let Some(r)=range.as_mut(){r.1=Some(usage(value,global.page));}},_=>{}
            },
            0=>{
                if tag==10 {collections+=1;if collections>32{return None;}}
                if tag==12 {collections=collections.checked_sub(1)?;}
                if tag==8 {
                    let index=if let Some(n)=reports.iter().position(|r|r.id==global.id){n}else{
                        if reports.len()>=16{return None;}reports.push(Format::empty(global.id));reports.len()-1
                    };
                    let report=&mut reports[index];
                    if global.size==0||global.count==0||global.count>512{return None;}
                    let span=global.size.checked_mul(global.count)?;
                    let bits=(report.bits as u32).checked_add(span)?;if bits>512{return None;}
                    if value&1==0 && value&2!=0 {
                        for i in 0..global.count {
                            let u=usages.get(i as usize).copied().or_else(||range.and_then(|(lo,hi)|lo.checked_add(i).filter(|v|hi.is_some_and(|h|*v<=h)))).unwrap_or(0);
                            let field=Field{bit:(report.bits as u32+i*global.size) as u16,size:global.size.min(255) as u8,signed:global.minimum<0};
                            if (0x90001..=0x90003).contains(&u)&&global.size==1 {report.buttons[(u-0x90001) as usize]=Some(field);}
                            if value&4!=0 && (1..=16).contains(&global.size) {
                                match u {0x10030=>report.x=Some(field),0x10031=>report.y=Some(field),0x10038=>report.wheel=Some(field),_=>{}}
                            }
                        }
                    }
                    report.bits=bits as u16;
                }
                usages.clear();range=None;
            },_=>{}
        }
    }
    if collections!=0||!stack.is_empty(){return None;}
    reports.into_iter().find(|r|r.x.is_some()&&r.y.is_some()&&r.buttons[0].is_some()&&r.bytes()<=64)
}
#[cfg(test)]
mod tests {
    use super::*;
    const MOUSE:&[u8]=&[0x05,1,0x09,2,0xa1,1,0x09,1,0xa1,0,0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x75,1,0x95,3,0x81,2,0x75,5,0x95,1,0x81,1,0x05,1,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,0xc0,0xc0];
    #[test]fn standard_wheel_and_signed_motion(){let f=parse(MOUSE).unwrap();assert!(f.has_wheel());assert_eq!(f.bytes(),4);assert_eq!(f.decode(&[5,0x80,127,255]),Some(Sample{buttons:5,dx:-128,dy:127,wheel:-1}));assert!(f.decode(&[0,0,0]).is_none());}
    #[test]fn report_id_is_checked(){let mut d=alloc::vec![0x85,7];d.extend_from_slice(MOUSE);let f=parse(&d).unwrap();assert_eq!(f.bytes(),5);assert_eq!(f.decode(&[7,1,4,252,1]),Some(Sample{buttons:1,dx:4,dy:-4,wheel:1}));assert!(f.decode(&[6,1,4,252,1]).is_none());}
    #[test]fn absolute_and_malformed_are_not_guessed(){let mut d=MOUSE.to_vec();let n=d.len();d[n-3]=2;assert!(parse(&d).is_none());assert!(parse(&MOUSE[..MOUSE.len()-1]).is_none());assert!(parse(&[0x75]).is_none());assert!(parse(&[0x85,0]).is_none());assert!(parse(&[0x75,0,0x97,255,255,255,255,0x81,2]).is_none());}
    #[test]fn bounded_random_descriptors_never_panic(){let mut seed=7u32;for _ in 0..3000 {let mut data=[0u8;128];for b in &mut data {seed=seed.wrapping_mul(1664525).wrapping_add(1013904223);*b=(seed>>16)as u8;}let _=parse(&data);}}
    #[test]fn wide_descriptor_keeps_report_id_and_axis_boundaries(){
        let mut d=alloc::vec![0x85,7];d.extend_from_slice(&MOUSE[..32]);
        d.extend_from_slice(&[0x05,1,0x09,0x30,0x09,0x31,0x16,1,0x80,0x26,255,127,0x75,16,0x95,2,0x81,6,
            0x09,0x38,0x15,0x81,0x25,127,0x75,8,0x95,1,0x81,6,0xc0,0xc0]);
        let f=parse(&d).unwrap();assert_eq!(f.bytes(),7);
        assert_eq!(f.decode(&[7,3,0,4,0,248,255]),Some(Sample{buttons:3,dx:1024,dy:-2048,wheel:-1}));
    }
    #[test]fn wide_axes_and_unaligned_signed_fields(){assert_eq!(extract(&[0,0,0x20],Field{bit:7,size:16,signed:true}),Some(0x4000));assert_eq!(extract(&[0x80,0xff,0x7f],Field{bit:7,size:16,signed:true}),Some(-1));}
}
