// SPDX-License-Identifier: GPL-2.0-or-later
//! Microsoft CRT argument quoting, independent of a shell or kernel state.
extern crate alloc;
use alloc::{string::String,vec::Vec};
fn space(c:u8)->bool{c==b' '||c==b'\t'}
pub fn parse(input:&[u8],limit:usize)->Vec<Vec<u8>>{
    let end=input.iter().position(|&c|c==0).unwrap_or(input.len());let mut p=0;let mut args=Vec::new();
    while p<end&&space(input[p]){p+=1;}
    if p==end||limit==0{return args;}
    // argv[0] is a pathname: only quote grouping applies, not backslash escapes.
    let mut arg=Vec::new();let mut quoted=false;
    while p<end&&(quoted||!space(input[p])){if input[p]==b'"'{quoted=!quoted;}else{arg.push(input[p]);}p+=1;}
    args.push(arg);
    while args.len()<limit{
        while p<end&&space(input[p]){p+=1;}if p==end{break;}
        let mut arg=Vec::new();let mut quoted=false;
        while p<end&&(quoted||!space(input[p])){
            let mut slashes=0;while p<end&&input[p]==b'\\'{slashes+=1;p+=1;}
            if p<end&&input[p]==b'"'{
                arg.extend(core::iter::repeat(b'\\').take(slashes/2));
                if slashes%2!=0{arg.push(b'"');p+=1;}
                else if quoted&&p+1<end&&input[p+1]==b'"'{arg.push(b'"');p+=2;}
                else{quoted=!quoted;p+=1;}
            }else{
                arg.extend(core::iter::repeat(b'\\').take(slashes));
                if p==end||(!quoted&&space(input[p])){break;}
                arg.push(input[p]);p+=1;
            }
        }
        args.push(arg);
    }args
}
pub fn encode(args:&[String])->String{
    let mut out=String::new();
    for(i,arg)in args.iter().enumerate(){
        if i!=0{out.push(' ');}out.push('"');
        if i==0{out.push_str(arg);}else{
            let mut slashes=0;
            for c in arg.chars(){if c=='\\'{slashes+=1;continue;}
                let count=if c=='"'{slashes*2+1}else{slashes};
                out.extend(core::iter::repeat('\\').take(count));out.push(c);slashes=0;
            }
            out.extend(core::iter::repeat('\\').take(slashes*2));
        }out.push('"');
    }out
}
#[cfg(test)]mod tests{
    use super::*;
    #[test]fn program_path(){assert_eq!(parse(br#""C:\Program Files\Tool.exe" x"#,32),[br"C:\Program Files\Tool.exe".to_vec(),b"x".to_vec()]);}
    #[test]fn empty_and_literal_shell_chars(){assert_eq!(parse(br#"app "" "Bob's $(touch nope)""#,32),[b"app".to_vec(),Vec::new(),b"Bob's $(touch nope)".to_vec()]);}
    #[test]fn quotes_and_backslashes(){assert_eq!(parse(br#"app "a\"b" "C:\dir\\""#,32),[b"app".to_vec(),b"a\"b".to_vec(),br"C:\dir\".to_vec()]);}
    #[test]fn adjacent_quotes(){assert_eq!(parse(br#"app "left""right""#,32),[b"app".to_vec(),b"left\"right".to_vec()]);}
    #[test]fn bounded(){assert_eq!(parse(b"app one two",2).len(),2);assert!(parse(b"app",0).is_empty());}
    #[test]fn roundtrip(){
        let values=["","one two","tab\tvalue","a\"b",r"C:\folder\",r#"\\\""#,"Bob's $(literal)","Größe"];
        for a in values{for b in values{let args=alloc::vec![String::from("/Downloads/Bob's tool.exe"),String::from(a),String::from(b)];assert_eq!(parse(encode(&args).as_bytes(),32),args.iter().map(|s|s.as_bytes().to_vec()).collect::<Vec<_>>());}}
    }
}
