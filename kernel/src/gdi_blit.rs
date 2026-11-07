// SPDX-License-Identifier: GPL-2.0-or-later
//! Allocation-free clipping and overlap-safe 32-bit SRCCOPY primitives.
//! GDI uses the page-chunk copy for physically scattered bitmap/window stores.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Shape {
    pub width: u32,
    pub height: u32,
    pub stride: usize,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Region {
    pub dx: usize,
    pub dy: usize,
    pub sx: usize,
    pub sy: usize,
    pub width: usize,
    pub height: usize,
}
pub fn clip(
    dst: Shape,
    src: Shape,
    dx: i32,
    dy: i32,
    sx: i32,
    sy: i32,
    width: i32,
    height: i32,
) -> Option<Region> {
    let (dx, dy, sx, sy, width, height) = (
        dx as i64,
        dy as i64,
        sx as i64,
        sy as i64,
        width as i64,
        height as i64,
    );
    let x0 = 0.max(-dx).max(-sx);
    let y0 = 0.max(-dy).max(-sy);
    let x1 = width.min(dst.width as i64 - dx).min(src.width as i64 - sx);
    let y1 = height
        .min(dst.height as i64 - dy)
        .min(src.height as i64 - sy);
    if x1 <= x0 || y1 <= y0 {
        return None;
    }
    Some(Region {
        dx: (dx + x0) as usize,
        dy: (dy + y0) as usize,
        sx: (sx + x0) as usize,
        sy: (sy + y0) as usize,
        width: (x1 - x0) as usize,
        height: (y1 - y0) as usize,
    })
}
/// Win32 clips the destination only. Reject an out-of-bounds source after
/// destination clipping instead of inventing or partially copying pixels.
pub fn clip_destination(
    dst: Shape,
    src: Shape,
    dx: i32,
    dy: i32,
    sx: i32,
    sy: i32,
    width: i32,
    height: i32,
) -> Result<Option<Region>, ()> {
    if width < 0 || height < 0 {
        return Err(());
    }
    let (dx, dy, sx, sy, width, height) = (
        dx as i64,
        dy as i64,
        sx as i64,
        sy as i64,
        width as i64,
        height as i64,
    );
    let x0 = 0.max(-dx);
    let y0 = 0.max(-dy);
    let x1 = width.min(dst.width as i64 - dx);
    let y1 = height.min(dst.height as i64 - dy);
    if x1 <= x0 || y1 <= y0 {
        return Ok(None);
    }
    if sx + x0 < 0 || sy + y0 < 0 || sx + x1 > src.width as i64 || sy + y1 > src.height as i64 {
        return Err(());
    }
    Ok(Some(Region {
        dx: (dx + x0) as usize,
        dy: (dy + y0) as usize,
        sx: (sx + x0) as usize,
        sy: (sy + y0) as usize,
        width: (x1 - x0) as usize,
        height: (y1 - y0) as usize,
    }))
}
fn valid_shape(shape: Shape, len: usize) -> bool {
    shape.stride >= shape.width as usize
        && (shape.height as usize)
            .checked_mul(shape.stride)
            .is_some_and(|n| n <= len)
}
fn fits(shape: Shape, x: usize, y: usize, r: Region) -> bool {
    x.checked_add(r.width)
        .is_some_and(|n| n <= shape.width as usize)
        && y.checked_add(r.height)
            .is_some_and(|n| n <= shape.height as usize)
}
pub fn copy_between(
    dst: &mut [u32],
    ds: Shape,
    src: &[u32],
    ss: Shape,
    r: Region,
) -> Result<(), ()> {
    if !valid_shape(ds, dst.len())
        || !valid_shape(ss, src.len())
        || !fits(ds, r.dx, r.dy, r)
        || !fits(ss, r.sx, r.sy, r)
    {
        return Err(());
    }
    for row in 0..r.height {
        let d = (r.dy + row) * ds.stride + r.dx;
        let s = (r.sy + row) * ss.stride + r.sx;
        dst[d..d + r.width].copy_from_slice(&src[s..s + r.width]);
    }
    Ok(())
}
pub fn copy_within(pixels: &mut [u32], shape: Shape, r: Region) -> Result<(), ()> {
    if !valid_shape(shape, pixels.len())
        || !fits(shape, r.dx, r.dy, r)
        || !fits(shape, r.sx, r.sy, r)
    {
        return Err(());
    }
    // Bottom-up for downward overlap; copy_within supplies horizontal memmove.
    for index in 0..r.height {
        let row = if r.dy > r.sy {
            r.height - 1 - index
        } else {
            index
        };
        let d = (r.dy + row) * shape.stride + r.dx;
        let s = (r.sy + row) * shape.stride + r.sx;
        pixels.copy_within(s..s + r.width, d);
    }
    Ok(())
}
/// Source-over for an opaque RGB source with a constant alpha. No per-pixel
/// alpha or color-space conversion is implied by this primitive.
pub fn blend_rgb(dst: u32, src: u32, alpha: u8) -> u32 {
    let a = alpha as u32;
    let mut out = 0;
    for shift in [0, 8, 16] {
        let d = (dst >> shift) & 255;
        let s = (src >> shift) & 255;
        out |= ((s * a + d * (255 - a) + 127) / 255) << shift;
    }
    out
}
/// Copy a clipped region in contiguous chunks, handling independently paged
/// source/destination rows without a temporary image or row allocation.
///
/// # Safety
/// Each callback must return a valid pixel pointer and a positive number of
/// accessible pixels from it (or ending at it when backwards). Regions must
/// be in bounds. Choose backwards for a same-surface move down/right; distinct
/// surfaces must not alias. Callers keep both stores alive during the copy.
pub unsafe fn copy_paged(
    r: Region,
    backwards: bool,
    mut dst: impl FnMut(usize, usize, bool) -> (*mut u32, usize),
    mut src: impl FnMut(usize, usize, bool) -> (*mut u32, usize),
) {
    for row in 0..r.height {
        let row = if backwards { r.height - 1 - row } else { row };
        let mut done = 0;
        while done < r.width {
            let x = if backwards { r.width - 1 - done } else { done };
            let (dp, dn) = dst(r.dx + x, r.dy + row, backwards);
            let (sp, sn) = src(r.sx + x, r.sy + row, backwards);
            let n = (r.width - done).min(dn).min(sn);
            assert!(n > 0);
            if backwards {
                core::ptr::copy(sp.sub(n - 1), dp.sub(n - 1), n);
            } else {
                core::ptr::copy(sp, dp, n);
            }
            done += n;
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn next(seed: &mut u32) -> u32 {
        *seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
        *seed
    }
    // Independent pixel-by-pixel snapshot semantics, with no clipping formula
    // or overlap-direction logic shared with the implementation.
    fn model(dst: &mut [u32], ds: Shape, src: &[u32], ss: Shape, p: [i32; 6]) {
        let [dx, dy, sx, sy, w, h] = p;
        for y in 0..h.max(0) {
            for x in 0..w.max(0) {
                let (a, b, c, d) = (
                    dx as i64 + x as i64,
                    dy as i64 + y as i64,
                    sx as i64 + x as i64,
                    sy as i64 + y as i64,
                );
                if a >= 0
                    && b >= 0
                    && c >= 0
                    && d >= 0
                    && a < ds.width as i64
                    && b < ds.height as i64
                    && c < ss.width as i64
                    && d < ss.height as i64
                {
                    dst[b as usize * ds.stride + a as usize] =
                        src[d as usize * ss.stride + c as usize];
                }
            }
        }
    }
    #[test]
    fn snapshot_model_10000() {
        let mut seed = 47;
        for _ in 0..10000 {
            let w = next(&mut seed) % 13 + 1;
            let h = next(&mut seed) % 11 + 1;
            let stride = w as usize + (next(&mut seed) % 4) as usize;
            let shape = Shape {
                width: w,
                height: h,
                stride,
            };
            let original: Vec<u32> = (0..stride * h as usize).map(|n| n as u32 + 17).collect();
            let mut p = [0; 6];
            for v in &mut p[..4] {
                *v = (next(&mut seed) % 41) as i32 - 20;
            }
            p[4] = (next(&mut seed) % 24) as i32 - 2;
            p[5] = (next(&mut seed) % 22) as i32 - 2;
            let mut expected = original.clone();
            model(&mut expected, shape, &original, shape, p);
            let mut actual = original.clone();
            if let Some(r) = clip(shape, shape, p[0], p[1], p[2], p[3], p[4], p[5]) {
                copy_within(&mut actual, shape, r).unwrap();
            }
            assert_eq!(actual, expected, "{p:?}");
            let mut paged = original.clone();
            if let Some(r) = clip(shape, shape, p[0], p[1], p[2], p[3], p[4], p[5]) {
                let ptr = paged.as_mut_ptr();
                let chunk = |x: usize, y: usize, back: bool| {
                    let off = y * shape.stride + x;
                    (
                        unsafe { ptr.add(off) },
                        if back { off % 7 + 1 } else { 7 - off % 7 },
                    )
                };
                unsafe {
                    copy_paged(
                        r,
                        r.dy > r.sy || (r.dy == r.sy && r.dx > r.sx),
                        chunk,
                        chunk,
                    );
                }
            }
            assert_eq!(paged, expected, "page overlap {p:?}");
            let ds = Shape {
                width: w,
                height: h,
                stride: stride + 3,
            };
            let mut expected = vec![0xabcdef; ds.stride * h as usize];
            let mut actual = expected.clone();
            model(&mut expected, ds, &original, shape, p);
            if let Some(r) = clip(ds, shape, p[0], p[1], p[2], p[3], p[4], p[5]) {
                copy_between(&mut actual, ds, &original, shape, r).unwrap();
            }
            assert_eq!(actual, expected, "padded {p:?}");
            let mut paged = vec![0xabcdef; ds.stride * h as usize];
            if let Some(r) = clip(ds, shape, p[0], p[1], p[2], p[3], p[4], p[5]) {
                let dp = paged.as_mut_ptr();
                let sp = original.as_ptr() as *mut u32;
                unsafe {
                    copy_paged(
                        r,
                        false,
                        |x, y, _| {
                            let off = y * ds.stride + x;
                            (dp.add(off), 11 - off % 11)
                        },
                        |x, y, _| {
                            let off = y * shape.stride + x;
                            (sp.add(off), 7 - off % 7)
                        },
                    );
                }
            }
            assert_eq!(paged, expected, "different pages {p:?}");
        }
    }
    #[test]
    fn overlapping_all_directions() {
        let s = Shape {
            width: 6,
            height: 5,
            stride: 8,
        };
        let original: Vec<_> = (0..40).collect();
        for dx in -3..4 {
            for dy in -3..4 {
                let p = [dx, dy, 1, 1, 5, 4];
                let mut expected = original.clone();
                model(&mut expected, s, &original, s, p);
                let mut actual = original.clone();
                if let Some(r) = clip(s, s, dx, dy, 1, 1, 5, 4) {
                    copy_within(&mut actual, s, r).unwrap();
                }
                assert_eq!(actual, expected);
            }
        }
    }
    #[test]
    fn invalid_buffers_unchanged() {
        let s = Shape {
            width: 3,
            height: 3,
            stride: 3,
        };
        let r = Region {
            dx: 0,
            dy: 0,
            sx: 0,
            sy: 0,
            width: 3,
            height: 3,
        };
        let mut p = [7; 8];
        assert_eq!(copy_within(&mut p, s, r), Err(()));
        assert_eq!(p, [7; 8]);
        let mut p = [7; 9];
        let bad = Region {
            dx: usize::MAX,
            ..r
        };
        assert_eq!(copy_within(&mut p, s, bad), Err(()));
        assert_eq!(p, [7; 9]);
        let overflow = Shape {
            stride: usize::MAX,
            ..s
        };
        assert_eq!(copy_between(&mut p, overflow, &[0; 9], s, r), Err(()));
        assert_eq!(p, [7; 9]);
    }
    #[test]
    fn destination_clip_does_not_hide_invalid_source() {
        let s = Shape {
            width: 10,
            height: 10,
            stride: 10,
        };
        assert_eq!(clip_destination(s, s, 0, 0, -1, 0, 4, 4), Err(()));
        assert_eq!(clip_destination(s, s, 0, 0, 8, 0, 4, 4), Err(()));
        assert_eq!(
            clip_destination(s, s, -1, 0, -1, 0, 4, 4)
                .unwrap()
                .unwrap()
                .sx,
            0
        );
        assert_eq!(clip_destination(s, s, 20, 20, -100, -100, 4, 4), Ok(None));
        assert_eq!(clip_destination(s, s, 0, 0, 0, 0, -1, 4), Err(()));
    }
    #[test]
    fn constant_alpha_endpoints_rounding_and_bounds() {
        assert_eq!(blend_rgb(0xff, 0xff0000, 128), 0x80007f);
        for d in 0..256u32 {
            for s in 0..256u32 {
                for a in [0u8, 1, 64, 127, 128, 192, 254, 255] {
                    let rgb = blend_rgb(d * 0x010101, s * 0x010101, a);
                    let actual = rgb & 255;
                    assert!(actual >= d.min(s) && actual <= d.max(s));
                    assert_eq!(
                        actual,
                        (d as i64 + (((s as i64 - d as i64) * a as i64 + 127 + 255 * 255) / 255)
                            - 255) as u32
                    );
                    if a == 0 {
                        assert_eq!(rgb, d * 0x010101);
                    }
                    if a == 255 {
                        assert_eq!(rgb, s * 0x010101);
                    }
                }
            }
        }
    }
    #[test]
    fn extreme_coordinates() {
        let s = Shape {
            width: 10,
            height: 10,
            stride: 10,
        };
        assert_eq!(clip(s, s, i32::MIN, 0, 0, 0, i32::MAX, 1), None);
        assert_eq!(clip(s, s, i32::MAX, 0, 0, 0, i32::MAX, 1), None);
        assert_eq!(clip(s, s, 0, 0, 0, 0, -1, 1), None);
        assert_eq!(
            clip(s, s, -1, -1, -1, -1, i32::MAX, i32::MAX).unwrap(),
            Region {
                dx: 0,
                dy: 0,
                sx: 0,
                sy: 0,
                width: 10,
                height: 10
            }
        );
    }
}
