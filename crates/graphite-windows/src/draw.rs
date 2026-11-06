//! Replays a [`DisplayList`] into a Direct2D device context.

use std::collections::HashMap;
use std::mem::ManuallyDrop;

use graphite::{Affine, DisplayList, DrawOp, FillRule, Image, LineCap, LineJoin, Paint, Path, ResourceId, StrokeStyle};
use windows::Win32::Graphics::Direct2D::Common::{
    D2D_RECT_F, D2D_SIZE_U, D2D1_ALPHA_MODE_PREMULTIPLIED, D2D1_GRADIENT_STOP, D2D1_PIXEL_FORMAT,
};
use windows::Win32::Graphics::Direct2D::{
    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1_BITMAP_OPTIONS_NONE, D2D1_BITMAP_PROPERTIES1, D2D1_BRUSH_PROPERTIES,
    D2D1_CAP_STYLE, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_SQUARE, D2D1_DASH_STYLE_CUSTOM,
    D2D1_DASH_STYLE_SOLID, D2D1_DEFAULT_FLATTENING_TOLERANCE, D2D1_EXTEND_MODE_CLAMP, D2D1_GAMMA_2_2,
    D2D1_INTERPOLATION_MODE_LINEAR, D2D1_LAYER_OPTIONS1_NONE, D2D1_LAYER_PARAMETERS1, D2D1_LINE_JOIN,
    D2D1_LINE_JOIN_BEVEL, D2D1_LINE_JOIN_MITER_OR_BEVEL, D2D1_LINE_JOIN_ROUND, D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES,
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES, D2D1_STROKE_STYLE_PROPERTIES, ID2D1Bitmap1, ID2D1Brush, ID2D1DeviceContext,
    ID2D1Factory1, ID2D1Geometry, ID2D1PathGeometry, ID2D1StrokeStyle,
};
use windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_R8G8B8A8_UNORM;
use windows::core::{Interface, Result};

use crate::convert::{d2d_color, d2d_rect, matrix3x2, path_geometry, point, vec2};

/// The clips pushed at one save level, popped in reverse at `Restore`.
#[derive(Clone, Copy)]
enum Clip {
    AxisAligned,
    Layer,
}

#[derive(Clone)]
struct State {
    /// User space → local space.
    transform: Affine,
    alpha: f32,
    clips: Vec<Clip>,
}

/// Replays `list` (recorded in a layer's local space) into `dc`. `base`
/// maps local space to the context's pixels.
pub(crate) fn replay(dc: &ID2D1DeviceContext, factory: &ID2D1Factory1, list: &DisplayList, base: Affine) -> Result<()> {
    let mut r = Replayer { dc, base, bitmaps: HashMap::new() };
    let mut state = State { transform: Affine::IDENTITY, alpha: 1.0, clips: Vec::new() };
    let mut saved: Vec<State> = Vec::new();
    r.set_transform(state.transform);
    let result = (|| -> Result<()> {
        for op in list.ops() {
            match op {
                DrawOp::Save => {
                    saved.push(state.clone());
                    state.clips.clear();
                }
                DrawOp::Restore => {
                    let Some(previous) = saved.pop() else { continue };
                    r.pop_clips(&state.clips);
                    state = previous;
                    r.set_transform(state.transform);
                }
                DrawOp::ConcatTransform(m) => {
                    state.transform = m.then(state.transform);
                    r.set_transform(state.transform);
                }
                DrawOp::SetAlpha(a) => state.alpha *= a,
                DrawOp::ClipRect(rect) => {
                    let t = state.transform;
                    if t.b == 0.0 && t.c == 0.0 {
                        // SAFETY: popped at the matching Restore (or the end).
                        unsafe { dc.PushAxisAlignedClip(&d2d_rect(*rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE) };
                        state.clips.push(Clip::AxisAligned);
                    } else {
                        r.push_layer(&path_geometry(factory, &Path::rect(*rect), FillRule::NonZero)?.cast()?);
                        state.clips.push(Clip::Layer);
                    }
                }
                DrawOp::ClipPath { path, rule } => {
                    r.push_layer(&path_geometry(factory, path, *rule)?.cast()?);
                    state.clips.push(Clip::Layer);
                }
                DrawOp::FillPath { path, rule, paint } => {
                    let geometry = path_geometry(factory, path, *rule)?;
                    let brush = r.brush(paint, state.alpha)?;
                    // SAFETY: valid geometry and brush.
                    unsafe { dc.FillGeometry(&geometry, &brush, None) };
                }
                DrawOp::StrokePath { path, stroke } => {
                    let outline = stroke_outline(factory, path, &stroke.style, state.transform.then(base))?;
                    let brush = r.brush(&stroke.paint, state.alpha)?;
                    // SAFETY: valid geometry and brush.
                    unsafe { dc.FillGeometry(&outline, &brush, None) };
                }
                DrawOp::DrawImage { image, dst } => {
                    let bitmap = r.bitmap(image)?;
                    // SAFETY: a valid bitmap of this context.
                    unsafe {
                        dc.DrawBitmap(
                            &bitmap,
                            Some(&d2d_rect(*dst)),
                            state.alpha,
                            D2D1_INTERPOLATION_MODE_LINEAR,
                            None,
                            None,
                        )
                    };
                }
            }
        }
        Ok(())
    })();
    // Unwind whatever is still pushed, even after an error.
    r.pop_clips(&state.clips);
    for s in saved.iter().rev() {
        r.pop_clips(&s.clips);
    }
    result
}

struct Replayer<'a> {
    dc: &'a ID2D1DeviceContext,
    base: Affine,
    bitmaps: HashMap<ResourceId, ID2D1Bitmap1>,
}

impl Replayer<'_> {
    fn set_transform(&self, user: Affine) {
        // SAFETY: plain state setter.
        unsafe { self.dc.SetTransform(&matrix3x2(user.then(self.base))) };
    }

    fn pop_clips(&self, clips: &[Clip]) {
        for clip in clips.iter().rev() {
            // SAFETY: each pop matches a push made by `replay`.
            unsafe {
                match clip {
                    Clip::AxisAligned => self.dc.PopAxisAlignedClip(),
                    Clip::Layer => self.dc.PopLayer(),
                }
            }
        }
    }

    fn push_layer(&self, mask: &ID2D1Geometry) {
        let infinite = D2D_RECT_F { left: -f32::MAX, top: -f32::MAX, right: f32::MAX, bottom: f32::MAX };
        let params = D2D1_LAYER_PARAMETERS1 {
            contentBounds: infinite,
            geometricMask: ManuallyDrop::new(Some(mask.clone())),
            maskAntialiasMode: D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            maskTransform: matrix3x2(Affine::IDENTITY),
            opacity: 1.0,
            opacityBrush: ManuallyDrop::new(None),
            layerOptions: D2D1_LAYER_OPTIONS1_NONE,
        };
        // SAFETY: popped at the matching Restore (or the end). The mask is
        // in user space, transformed by the current world transform.
        unsafe { self.dc.PushLayer(&params, None) };
        drop(ManuallyDrop::into_inner(params.geometricMask));
    }

    fn brush(&self, paint: &Paint, alpha: f32) -> Result<ID2D1Brush> {
        let props = D2D1_BRUSH_PROPERTIES { opacity: alpha, transform: matrix3x2(Affine::IDENTITY) };
        // SAFETY: brush creation with valid descriptions.
        unsafe {
            Ok(match paint {
                Paint::Solid(c) => self.dc.CreateSolidColorBrush(&d2d_color(*c), Some(&props))?.cast()?,
                Paint::Linear { start, end, stops } => {
                    let stops = self.stops(stops)?;
                    let lp = D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES { startPoint: point(*start), endPoint: point(*end) };
                    self.dc.CreateLinearGradientBrush(&lp, Some(&props), &stops)?.cast()?
                }
                Paint::Radial { center, radius, stops } => {
                    let stops = self.stops(stops)?;
                    let rp = D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES {
                        center: point(*center),
                        gradientOriginOffset: vec2(0.0, 0.0),
                        radiusX: *radius as f32,
                        radiusY: *radius as f32,
                    };
                    self.dc.CreateRadialGradientBrush(&rp, Some(&props), &stops)?.cast()?
                }
            })
        }
    }

    fn stops(
        &self,
        stops: &[graphite::GradientStop],
    ) -> Result<windows::Win32::Graphics::Direct2D::ID2D1GradientStopCollection> {
        let stops: Vec<D2D1_GRADIENT_STOP> =
            stops.iter().map(|s| D2D1_GRADIENT_STOP { position: s.offset, color: d2d_color(s.color) }).collect();
        // Gamma 2.2: interpolate in sRGB, like CoreGraphics.
        // SAFETY: a valid stop array.
        unsafe {
            windows::Win32::Graphics::Direct2D::ID2D1RenderTarget::CreateGradientStopCollection(
                self.dc,
                &stops,
                D2D1_GAMMA_2_2,
                D2D1_EXTEND_MODE_CLAMP,
            )
        }
    }

    fn bitmap(&mut self, image: &Image) -> Result<ID2D1Bitmap1> {
        if let Some(b) = self.bitmaps.get(&image.id()) {
            return Ok(b.clone());
        }
        let bitmap = create_bitmap(self.dc, image)?;
        self.bitmaps.insert(image.id(), bitmap.clone());
        Ok(bitmap)
    }
}

/// A Direct2D bitmap holding `image`'s premultiplied RGBA pixels.
pub(crate) fn create_bitmap(dc: &ID2D1DeviceContext, image: &Image) -> Result<ID2D1Bitmap1> {
    let props = D2D1_BITMAP_PROPERTIES1 {
        pixelFormat: D2D1_PIXEL_FORMAT { format: DXGI_FORMAT_R8G8B8A8_UNORM, alphaMode: D2D1_ALPHA_MODE_PREMULTIPLIED },
        dpiX: 96.0,
        dpiY: 96.0,
        bitmapOptions: D2D1_BITMAP_OPTIONS_NONE,
        colorContext: ManuallyDrop::new(None),
    };
    let size = D2D_SIZE_U { width: image.pixel_width(), height: image.pixel_height() };
    // SAFETY: the pixel buffer is `width × height × 4` bytes.
    unsafe { dc.CreateBitmap(size, Some(image.pixels().as_ptr().cast()), image.pixel_width() * 4, &props) }
}

fn cap(c: LineCap) -> D2D1_CAP_STYLE {
    match c {
        LineCap::Butt => D2D1_CAP_STYLE_FLAT,
        LineCap::Round => D2D1_CAP_STYLE_ROUND,
        LineCap::Square => D2D1_CAP_STYLE_SQUARE,
    }
}

fn join(j: LineJoin) -> D2D1_LINE_JOIN {
    match j {
        // CoreGraphics bevels joins past the miter limit.
        LineJoin::Miter => D2D1_LINE_JOIN_MITER_OR_BEVEL,
        LineJoin::Round => D2D1_LINE_JOIN_ROUND,
        LineJoin::Bevel => D2D1_LINE_JOIN_BEVEL,
    }
}

/// The area covered by stroking `path` (in user space), to be filled.
///
/// `DrawGeometry` is not used: on the VirtIO GPU of the development VM it
/// draws strokes without any anti-aliasing (not one partially covered
/// pixel), while fills are anti-aliased. Widening the stroke and filling the
/// result is what Direct2D does for wide strokes anyway, and looks the same
/// everywhere. `to_device` (user space → pixels) sets the flattening
/// tolerance.
fn stroke_outline(
    factory: &ID2D1Factory1,
    path: &Path,
    style: &StrokeStyle,
    to_device: Affine,
) -> Result<ID2D1PathGeometry> {
    let geometry = path_geometry(factory, path, FillRule::NonZero)?;
    let stroke_style = stroke_style(factory, style)?;
    let m = to_device;
    let device_per_unit = (m.a * m.a + m.b * m.b).max(m.c * m.c + m.d * m.d).sqrt().max(1e-6);
    let tolerance = (D2D1_DEFAULT_FLATTENING_TOLERANCE as f64 / device_per_unit) as f32;
    // SAFETY: the sink is used between Open and Close only.
    unsafe {
        let outline: ID2D1PathGeometry = factory.CreatePathGeometry()?.cast()?;
        let sink = outline.Open()?;
        geometry.Widen(style.width as f32, &stroke_style, None, tolerance, &sink)?;
        sink.Close()?;
        Ok(outline)
    }
}

/// Direct2D dash lengths are in multiples of the stroke width.
pub(crate) fn stroke_style(factory: &ID2D1Factory1, style: &StrokeStyle) -> Result<ID2D1StrokeStyle> {
    let w = style.width.max(1e-6);
    let dashes: Vec<f32> = style.dash.iter().map(|d| (d / w) as f32).collect();
    let props = D2D1_STROKE_STYLE_PROPERTIES {
        startCap: cap(style.cap),
        endCap: cap(style.cap),
        dashCap: cap(style.cap),
        lineJoin: join(style.join),
        miterLimit: style.miter_limit as f32,
        dashStyle: if dashes.is_empty() { D2D1_DASH_STYLE_SOLID } else { D2D1_DASH_STYLE_CUSTOM },
        dashOffset: (style.dash_phase / w) as f32,
    };
    // SAFETY: valid properties; dashes only with the custom style.
    unsafe {
        let factory: &windows::Win32::Graphics::Direct2D::ID2D1Factory = factory;
        factory.CreateStrokeStyle(&props, if dashes.is_empty() { None } else { Some(&dashes) })
    }
}
