//! Conversions from graphite values to CoreGraphics / Core Animation values.

use std::ptr;

use graphite::{Affine, Color, Corners, GradientStop, Image, LineCap, LineJoin, Path, PathEl, Point, Rect, Size, Vec2};
use objc2::rc::Retained;
use objc2::runtime::AnyObject;
use objc2_core_foundation::{CFData, CFRetained, CGAffineTransform, CGFloat, CGPoint, CGRect, CGSize};
use objc2_core_graphics::{
    CGBitmapInfo, CGColor, CGColorRenderingIntent, CGColorSpace, CGDataProvider, CGGradient, CGImage, CGImageAlphaInfo,
    CGImageByteOrderInfo, CGLineCap, CGLineJoin, CGMutablePath, CGPath, kCGColorSpaceSRGB,
};
use objc2_foundation::{NSArray, NSNumber};
use objc2_quartz_core::CACornerMask;

pub(crate) fn cg_point(p: Point) -> CGPoint {
    CGPoint::new(p.x, p.y)
}

pub(crate) fn cg_size(s: Size) -> CGSize {
    CGSize::new(s.width, s.height)
}

pub(crate) fn cg_rect(r: Rect) -> CGRect {
    CGRect::new(cg_point(r.origin), cg_size(r.size))
}

pub(crate) fn graphite_rect(r: CGRect) -> Rect {
    Rect::new(r.origin.x, r.origin.y, r.size.width, r.size.height)
}

pub(crate) fn cg_affine(m: Affine) -> CGAffineTransform {
    // Same layout and conventions as `CGAffineTransform`.
    CGAffineTransform { a: m.a, b: m.b, c: m.c, d: m.d, tx: m.tx, ty: m.ty }
}

pub(crate) fn srgb() -> CFRetained<CGColorSpace> {
    // SAFETY: `kCGColorSpaceSRGB` is a valid, immutable CFString constant.
    CGColorSpace::with_name(Some(unsafe { kCGColorSpaceSRGB })).expect("sRGB color space is always available")
}

pub(crate) fn cg_color(c: Color) -> CFRetained<CGColor> {
    CGColor::new_srgb(c.r as CGFloat, c.g as CGFloat, c.b as CGFloat, c.a as CGFloat)
}

/// The corners of a layer in a flipped (y-down) geometry: `MinY` is the top.
pub(crate) fn ca_corners(c: Corners) -> CACornerMask {
    let mut mask = CACornerMask::empty();
    if c.contains(Corners::TOP_LEFT) {
        mask |= CACornerMask::LayerMinXMinYCorner;
    }
    if c.contains(Corners::TOP_RIGHT) {
        mask |= CACornerMask::LayerMaxXMinYCorner;
    }
    if c.contains(Corners::BOTTOM_LEFT) {
        mask |= CACornerMask::LayerMinXMaxYCorner;
    }
    if c.contains(Corners::BOTTOM_RIGHT) {
        mask |= CACornerMask::LayerMaxXMaxYCorner;
    }
    mask
}

pub(crate) fn cg_line_cap(c: LineCap) -> CGLineCap {
    match c {
        LineCap::Butt => CGLineCap::Butt,
        LineCap::Round => CGLineCap::Round,
        LineCap::Square => CGLineCap::Square,
    }
}

pub(crate) fn cg_line_join(j: LineJoin) -> CGLineJoin {
    match j {
        LineJoin::Miter => CGLineJoin::Miter,
        LineJoin::Round => CGLineJoin::Round,
        LineJoin::Bevel => CGLineJoin::Bevel,
    }
}

/// Builds a `CGPath`, optionally offset by `offset`.
pub(crate) fn cg_path(path: &Path, offset: Vec2) -> CFRetained<CGPath> {
    let m = CGMutablePath::new();
    let p = |pt: Point| (pt.x + offset.x, pt.y + offset.y);
    for el in path.elements() {
        // SAFETY: `m` is a valid mutable path and the transform pointer is null.
        unsafe {
            match *el {
                PathEl::MoveTo(a) => {
                    let (x, y) = p(a);
                    CGMutablePath::move_to_point(Some(&m), ptr::null(), x, y);
                }
                PathEl::LineTo(a) => {
                    let (x, y) = p(a);
                    CGMutablePath::add_line_to_point(Some(&m), ptr::null(), x, y);
                }
                PathEl::QuadTo(c, a) => {
                    let ((cx, cy), (x, y)) = (p(c), p(a));
                    CGMutablePath::add_quad_curve_to_point(Some(&m), ptr::null(), cx, cy, x, y);
                }
                PathEl::CurveTo(c1, c2, a) => {
                    let ((c1x, c1y), (c2x, c2y), (x, y)) = (p(c1), p(c2), p(a));
                    CGMutablePath::add_curve_to_point(Some(&m), ptr::null(), c1x, c1y, c2x, c2y, x, y);
                }
                PathEl::Close => CGMutablePath::close_subpath(Some(&m)),
            }
        }
    }
    // SAFETY: CGMutablePath is a CGPath.
    unsafe { CFRetained::cast_unchecked::<CGPath>(m) }
}

/// Wraps premultiplied RGBA8 pixels in a `CGImage` (the bytes are copied).
pub(crate) fn cg_image(image: &Image) -> Option<CFRetained<CGImage>> {
    let pixels = image.pixels();
    // SAFETY: the pointer/length pair describes `pixels`; CFData copies it.
    let data = unsafe { CFData::new(None, pixels.as_ptr(), pixels.len() as _) }?;
    let provider = CGDataProvider::with_cf_data(Some(&data))?;
    let (w, h) = (image.pixel_width() as usize, image.pixel_height() as usize);
    let info = CGBitmapInfo(CGImageAlphaInfo::PremultipliedLast.0 | CGImageByteOrderInfo::OrderDefault.0);
    // SAFETY: the provider holds exactly `w * h * 4` bytes and `decode` is null.
    unsafe {
        CGImage::new(
            w,
            h,
            8,
            32,
            w * 4,
            Some(&srgb()),
            info,
            Some(&provider),
            ptr::null(),
            true,
            CGColorRenderingIntent::RenderingIntentDefault,
        )
    }
}

pub(crate) fn cg_gradient(stops: &[GradientStop]) -> Option<CFRetained<CGGradient>> {
    let components: Vec<CGFloat> =
        stops.iter().flat_map(|s| [s.color.r, s.color.g, s.color.b, s.color.a].map(|v| v as CGFloat)).collect();
    let locations: Vec<CGFloat> = stops.iter().map(|s| s.offset as CGFloat).collect();
    // SAFETY: `components` has 4 entries per stop and `locations` one.
    unsafe { CGGradient::with_color_components(Some(&srgb()), components.as_ptr(), locations.as_ptr(), stops.len()) }
}

/// A CF object viewed as an Objective-C object (toll-free bridging).
pub(crate) fn cf_as_object<T: ?Sized>(cf: &T) -> &AnyObject {
    // SAFETY: every CoreFoundation object is a valid Objective-C object.
    unsafe { &*(cf as *const T as *const AnyObject) }
}

pub(crate) fn ns_numbers(values: impl IntoIterator<Item = f64>) -> Retained<NSArray<NSNumber>> {
    let numbers: Vec<Retained<NSNumber>> = values.into_iter().map(NSNumber::new_f64).collect();
    NSArray::from_retained_slice(&numbers)
}
