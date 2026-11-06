//! Conversions from graphite values to GDK / GSK / graphene values.

use graphite::{
    Affine, Color, Corners, GradientStop, Image, LineCap, LineJoin, Path, PathEl, Point, Rect, StrokeStyle,
    TransformKind, Vec2,
};
use gtk::{gdk, glib, graphene, gsk};

pub(crate) fn rgba(c: Color) -> gdk::RGBA {
    gdk::RGBA::new(c.r, c.g, c.b, c.a)
}

pub(crate) fn rect(r: Rect) -> graphene::Rect {
    graphene::Rect::new(r.min_x() as f32, r.min_y() as f32, r.width() as f32, r.height() as f32)
}

pub(crate) fn point(p: Point) -> graphene::Point {
    graphene::Point::new(p.x as f32, p.y as f32)
}

pub(crate) fn offset(v: Vec2) -> graphene::Point {
    graphene::Point::new(v.x as f32, v.y as f32)
}

/// The cheapest `GskTransform` for `m`, always categorized as 2D.
///
/// A raw `gsk_transform_matrix` is categorized as possibly 3D, which the
/// Cairo renderer cannot draw (it paints the node hot pink) and the GPU
/// renderers handle less efficiently. General affine matrices are therefore
/// decomposed into translate · rotate · skew · scale.
pub(crate) fn transform(m: Affine) -> Option<gsk::Transform> {
    let origin = offset(Vec2::new(m.tx, m.ty));
    let t = match m.kind() {
        TransformKind::Identity => return None,
        TransformKind::Translate => gsk::Transform::new().translate(&origin),
        TransformKind::ScaleTranslate => gsk::Transform::new().translate(&origin).scale(m.a as f32, m.d as f32),
        TransformKind::Affine => {
            // Rotating the first column onto the x axis leaves an upper
            // triangular matrix [sx shear; 0 sy] = skew(shear / sy) · scale.
            let sx = m.a.hypot(m.b);
            let sy = m.determinant() / sx;
            if sx == 0.0 || sy == 0.0 || !sy.is_finite() {
                // Degenerate (draws nothing); keep it exact.
                return Some(gsk::Transform::new().matrix(&graphene::Matrix::from_2d(m.a, m.b, m.c, m.d, m.tx, m.ty)));
            }
            let angle = m.b.atan2(m.a);
            let shear = (m.a * m.c + m.b * m.d) / sx;
            let mut t = gsk::Transform::new().translate(&origin).rotate(angle.to_degrees() as f32);
            if shear != 0.0 {
                t = t.skew((shear / sy).atan().to_degrees() as f32, 0.0);
            }
            t.scale(sx as f32, sy as f32)
        }
    };
    debug_assert!(
        {
            let (xx, yx, xy, yy, dx, dy) = t.to_2d();
            let tol = 1e-3 * (1.0 + m.a.abs() + m.b.abs() + m.c.abs() + m.d.abs());
            [(xx, m.a), (yx, m.b), (xy, m.c), (yy, m.d)].iter().all(|(g, e)| (*g as f64 - e).abs() <= tol)
                && (dx as f64 - m.tx).abs() <= 1e-2
                && (dy as f64 - m.ty).abs() <= 1e-2
        },
        "GskTransform decomposition does not match {m:?}"
    );
    Some(t)
}

/// A rectangle with rounded `corners`, radii clamped to half the shorter
/// side (the same clamping as `graphite::Path::rounded_rect`).
pub(crate) fn rounded_rect(r: Rect, radius: f64, corners: Corners) -> gsk::RoundedRect {
    let radius = radius.max(0.0).min(r.width().abs() / 2.0).min(r.height().abs() / 2.0) as f32;
    let corner = |c: Corners| {
        let v = if corners.contains(c) { radius } else { 0.0 };
        graphene::Size::new(v, v)
    };
    gsk::RoundedRect::new(
        rect(r),
        corner(Corners::TOP_LEFT),
        corner(Corners::TOP_RIGHT),
        corner(Corners::BOTTOM_RIGHT),
        corner(Corners::BOTTOM_LEFT),
    )
}

/// Builds a `GskPath`, offset by `offset`.
pub(crate) fn path(path: &Path, offset: Vec2) -> gsk::Path {
    let b = gsk::PathBuilder::new();
    let p = |pt: Point| ((pt.x + offset.x) as f32, (pt.y + offset.y) as f32);
    for el in path.elements() {
        match *el {
            PathEl::MoveTo(a) => {
                let (x, y) = p(a);
                b.move_to(x, y);
            }
            PathEl::LineTo(a) => {
                let (x, y) = p(a);
                b.line_to(x, y);
            }
            PathEl::QuadTo(c, a) => {
                let ((cx, cy), (x, y)) = (p(c), p(a));
                b.quad_to(cx, cy, x, y);
            }
            PathEl::CurveTo(c1, c2, a) => {
                let ((x1, y1), (x2, y2), (x, y)) = (p(c1), p(c2), p(a));
                b.cubic_to(x1, y1, x2, y2, x, y);
            }
            PathEl::Close => b.close(),
        }
    }
    b.to_path()
}

/// The part of `path` between fractions `start` and `end` of its length.
pub(crate) fn trim(path: &gsk::Path, start: f64, end: f64) -> Option<gsk::Path> {
    if start <= 0.0 && end >= 1.0 {
        return Some(path.clone());
    }
    if end <= start {
        return None;
    }
    let measure = gsk::PathMeasure::new(path);
    let len = measure.length();
    let from = measure.point(start as f32 * len)?;
    let to = measure.point(end as f32 * len)?;
    let b = gsk::PathBuilder::new();
    b.add_segment(path, &from, &to);
    Some(b.to_path())
}

pub(crate) fn fill_rule(rule: graphite::FillRule) -> gsk::FillRule {
    match rule {
        graphite::FillRule::NonZero => gsk::FillRule::Winding,
        graphite::FillRule::EvenOdd => gsk::FillRule::EvenOdd,
    }
}

pub(crate) fn stroke(style: &StrokeStyle) -> gsk::Stroke {
    let s = gsk::Stroke::new(style.width as f32);
    s.set_line_cap(match style.cap {
        LineCap::Butt => gsk::LineCap::Butt,
        LineCap::Round => gsk::LineCap::Round,
        LineCap::Square => gsk::LineCap::Square,
    });
    s.set_line_join(match style.join {
        LineJoin::Miter => gsk::LineJoin::Miter,
        LineJoin::Round => gsk::LineJoin::Round,
        LineJoin::Bevel => gsk::LineJoin::Bevel,
    });
    s.set_miter_limit(style.miter_limit as f32);
    let dash: Vec<f32> = style.dash.iter().map(|d| *d as f32).collect();
    s.set_dash(&dash);
    s.set_dash_offset(style.dash_phase as f32);
    s
}

pub(crate) fn color_stops(stops: &[GradientStop]) -> Vec<gsk::ColorStop> {
    stops.iter().map(|s| gsk::ColorStop::new(s.offset, rgba(s.color))).collect()
}

/// Uploads premultiplied RGBA8 pixels as a texture.
pub(crate) fn texture(image: &Image) -> gdk::Texture {
    let bytes = glib::Bytes::from(image.pixels());
    gdk::MemoryTexture::new(
        image.pixel_width() as i32,
        image.pixel_height() as i32,
        gdk::MemoryFormat::R8g8b8a8Premultiplied,
        &bytes,
        image.pixel_width() as usize * 4,
    )
    .into()
}
