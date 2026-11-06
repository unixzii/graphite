//! Replays a [`DisplayList`] into a `CGContext`.

use std::ptr;

use graphite::{DisplayList, DrawOp, FillRule, Paint, Path, Rect, StrokeStyle, Vec2};
use objc2_core_foundation::{CFRetained, CGFloat, CGRect};
use objc2_core_graphics::{
    CGBitmapContextCreate, CGBitmapContextCreateImage, CGContext, CGGradientDrawingOptions, CGImage, CGImageAlphaInfo,
    CGImageByteOrderInfo,
};

use crate::convert::{
    cg_affine, cg_color, cg_gradient, cg_image, cg_line_cap, cg_line_join, cg_path, cg_point, cg_rect, srgb,
};

/// Rasterizes `list`, recorded in a layer's local space, covering `bounds`
/// at `scale` device pixels per point. Returns `None` for an empty area.
pub(crate) fn rasterize(list: &DisplayList, bounds: Rect, scale: f64) -> Option<CFRetained<CGImage>> {
    let w = (bounds.width() * scale).ceil();
    let h = (bounds.height() * scale).ceil();
    if !(w >= 1.0 && h >= 1.0) {
        return None;
    }
    let info = CGImageAlphaInfo::PremultipliedLast.0 | CGImageByteOrderInfo::OrderDefault.0;
    // SAFETY: a null data pointer lets CoreGraphics allocate the buffer.
    let cx = unsafe { CGBitmapContextCreate(ptr::null_mut(), w as usize, h as usize, 8, 0, Some(&srgb()), info) }?;
    // CoreGraphics is y-up; graphite is y-down with the origin at bounds.origin.
    CGContext::translate_ctm(Some(&cx), 0.0, h);
    CGContext::scale_ctm(Some(&cx), scale, -scale);
    CGContext::translate_ctm(Some(&cx), -bounds.min_x(), -bounds.min_y());
    replay(&cx, list);
    CGBitmapContextCreateImage(Some(&cx))
}

/// Replays `list` into `cx`, whose CTM must already map graphite's y-down
/// user space.
pub(crate) fn replay(cx: &CGContext, list: &DisplayList) {
    let c = Some(cx);
    // Graphite's SetAlpha multiplies; CoreGraphics' sets. Track it ourselves.
    let mut alpha: Vec<CGFloat> = vec![1.0];
    for op in list.ops() {
        match op {
            DrawOp::Save => {
                CGContext::save_g_state(c);
                alpha.push(*alpha.last().unwrap());
            }
            DrawOp::Restore => {
                CGContext::restore_g_state(c);
                if alpha.len() > 1 {
                    alpha.pop();
                }
            }
            DrawOp::ConcatTransform(m) => CGContext::concat_ctm(c, cg_affine(*m)),
            DrawOp::SetAlpha(a) => {
                let top = alpha.last_mut().unwrap();
                *top *= *a as CGFloat;
                CGContext::set_alpha(c, *top);
            }
            DrawOp::ClipRect(r) => CGContext::clip_to_rect(c, cg_rect(*r)),
            DrawOp::ClipPath { path, rule } => {
                CGContext::add_path(c, Some(&cg_path(path, Vec2::ZERO)));
                clip(cx, *rule);
            }
            DrawOp::FillPath { path, rule, paint } => fill(cx, path, *rule, paint),
            DrawOp::StrokePath { path, stroke } => {
                set_stroke_style(cx, &stroke.style);
                match &stroke.paint {
                    Paint::Solid(color) => {
                        CGContext::set_stroke_color_with_color(c, Some(&cg_color(*color)));
                        CGContext::add_path(c, Some(&cg_path(path, Vec2::ZERO)));
                        CGContext::stroke_path(c);
                    }
                    gradient => {
                        CGContext::save_g_state(c);
                        CGContext::add_path(c, Some(&cg_path(path, Vec2::ZERO)));
                        CGContext::replace_path_with_stroked_path(c);
                        CGContext::clip(c);
                        draw_gradient(cx, gradient);
                        CGContext::restore_g_state(c);
                    }
                }
            }
            DrawOp::DrawImage { image, dst } => {
                let Some(cg) = cg_image(image) else { continue };
                // CGContextDrawImage assumes y-up; flip locally around `dst`.
                CGContext::save_g_state(c);
                CGContext::translate_ctm(c, dst.min_x(), dst.max_y());
                CGContext::scale_ctm(c, 1.0, -1.0);
                CGContext::draw_image(c, CGRect::new(cg_point(Default::default()), cg_rect(*dst).size), Some(&cg));
                CGContext::restore_g_state(c);
            }
        }
    }
}

fn clip(cx: &CGContext, rule: FillRule) {
    match rule {
        FillRule::NonZero => CGContext::clip(Some(cx)),
        FillRule::EvenOdd => CGContext::eo_clip(Some(cx)),
    }
}

fn fill(cx: &CGContext, path: &Path, rule: FillRule, paint: &Paint) {
    let c = Some(cx);
    match paint {
        Paint::Solid(color) => {
            CGContext::set_fill_color_with_color(c, Some(&cg_color(*color)));
            CGContext::add_path(c, Some(&cg_path(path, Vec2::ZERO)));
            match rule {
                FillRule::NonZero => CGContext::fill_path(c),
                FillRule::EvenOdd => CGContext::eo_fill_path(c),
            }
        }
        gradient => {
            CGContext::save_g_state(c);
            CGContext::add_path(c, Some(&cg_path(path, Vec2::ZERO)));
            clip(cx, rule);
            draw_gradient(cx, gradient);
            CGContext::restore_g_state(c);
        }
    }
}

/// Fills the current clip with a gradient paint.
fn draw_gradient(cx: &CGContext, paint: &Paint) {
    let extend = CGGradientDrawingOptions::DrawsBeforeStartLocation | CGGradientDrawingOptions::DrawsAfterEndLocation;
    match paint {
        Paint::Solid(_) => unreachable!("solid paints are handled by the caller"),
        Paint::Linear { start, end, stops } => {
            if let Some(g) = cg_gradient(stops) {
                CGContext::draw_linear_gradient(Some(cx), Some(&g), cg_point(*start), cg_point(*end), extend);
            }
        }
        Paint::Radial { center, radius, stops } => {
            if let Some(g) = cg_gradient(stops) {
                let p = cg_point(*center);
                CGContext::draw_radial_gradient(Some(cx), Some(&g), p, 0.0, p, *radius, extend);
            }
        }
    }
}

fn set_stroke_style(cx: &CGContext, style: &StrokeStyle) {
    let c = Some(cx);
    CGContext::set_line_width(c, style.width);
    CGContext::set_line_cap(c, cg_line_cap(style.cap));
    CGContext::set_line_join(c, cg_line_join(style.join));
    CGContext::set_miter_limit(c, style.miter_limit);
    // SAFETY: the pointer/length pair describes `style.dash` (or is empty).
    unsafe {
        if style.dash.is_empty() {
            CGContext::set_line_dash(c, 0.0, ptr::null(), 0);
        } else {
            CGContext::set_line_dash(c, style.dash_phase, style.dash.as_ptr(), style.dash.len());
        }
    }
}
