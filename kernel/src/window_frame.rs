// SPDX-License-Identifier: GPL-2.0-or-later
//! Geometry of THOS's existing compositor frame, in device pixels.
//! All currently supported top-level windows use that frame (including style 0).
//! This does not implement child/popup windows, menus or extended decorations.
pub const BORDER: i32 = 1;
pub const TOP: i32 = 38;
pub const CAPTION: i32 = TOP - BORDER;
pub const DEFAULT_CLIENT: (i32, i32) = (400, 240);
const SUPPORTED_STYLE: u32 = 0x00cf_0000 | 0x1000_0000;

pub fn supported(style: u32, extended: u32, menu: bool) -> Result<(), u32> {
    if style & !SUPPORTED_STYLE != 0 || extended != 0 || menu { Err(50) } else { Ok(()) }
}

pub fn adjust(rect: [i32; 4], style: u32, extended: u32, menu: bool) -> Result<[i32; 4], u32> {
    supported(style, extended, menu)?;
    if rect[2] < rect[0] || rect[3] < rect[1] { return Err(87); }
    let r = [rect[0].checked_sub(BORDER), rect[1].checked_sub(TOP),
        rect[2].checked_add(BORDER), rect[3].checked_add(BORDER)];
    match r { [Some(a), Some(b), Some(c), Some(d)] => Ok([a,b,c,d]), _ => Err(87) }
}

pub fn client_size(width: i32, height: i32) -> Result<(i32, i32), u32> {
    let (width, height) = if width == i32::MIN {
        (DEFAULT_CLIENT.0 + 2 * BORDER, DEFAULT_CLIENT.1 + TOP + BORDER)
    } else { (width, height) };
    let (w,h) = (width.checked_sub(2 * BORDER).ok_or(87u32)?, height.checked_sub(TOP + BORDER).ok_or(87u32)?);
    if w <= 0 || h <= 0 || w > 4096 || h > 4096 || (w as u64)*(h as u64)*4 > 16<<20 { return Err(87); }
    Ok((w,h))
}

pub fn client_origin(x: i32, y: i32) -> Result<(i32,i32),u32> {
    let (x,y) = if x == i32::MIN { (0,0) } else { (x,y) };
    Ok((x.checked_add(BORDER).ok_or(87u32)?, y.checked_add(TOP).ok_or(87u32)?))
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn frame_matches_unmodified_desktop_theme() {
        let desktop = include_str!("../../xtask/testdata/thosdesk.c");
        assert!(desktop.lines().any(|l| l == format!("#define BORDER {BORDER}")));
        assert!(desktop.lines().any(|l| l == format!("#define TITLE {TOP}")));
    }
    #[test] fn adjusted_extents_roundtrip_client_sizes() {
        for w in [1,20,400,1280,2048] { for h in [1,40,240,800,2048] {
            let r=adjust([-100,-20,w-100,h-20],0xcf0000,0,false).unwrap();
            assert_eq!(client_size(r[2]-r[0],r[3]-r[1]),Ok((w,h)));
        }}
        assert_eq!(client_size(i32::MIN,123),Ok(DEFAULT_CLIENT));
    }
    #[test] fn unsupported_and_overflow_do_not_return_success() {
        for style in [0x40000000,0x80000000,0x00200000,0x08000000] { assert_eq!(adjust([0,0,400,240],style,0,false),Err(50)); }
        assert_eq!(adjust([0,0,400,240],0xcf0000,0,true),Err(50));
        assert_eq!(adjust([0,0,400,240],0xcf0000,0x200,false),Err(50));
        for r in [[i32::MIN,0,0,0],[0,i32::MIN,0,0],[0,0,i32::MAX,0],[0,0,0,i32::MAX],[4,0,3,0]] { assert_eq!(adjust(r,0,0,false),Err(87)); }
        assert_eq!(client_size(2,39),Err(87)); assert_eq!(client_size(-1,100),Err(87));
        assert_eq!(client_size(i32::MAX,i32::MAX),Err(87));
        assert_eq!(client_origin(i32::MAX,0),Err(87));
    }
}
