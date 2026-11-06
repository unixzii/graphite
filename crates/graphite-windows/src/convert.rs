//! Conversions to composition, Direct2D and numerics values.

use graphite::{Affine, Corners, FillRule, Path, PathEl, Point, Rect, Size};
use windows::Graphics::IGeometrySource2D;
use windows::UI::Composition::CompositionPath;
use windows::Win32::Graphics::Direct2D::Common::{
    D2D_RECT_F, D2D1_BEZIER_SEGMENT, D2D1_COLOR_F, D2D1_FIGURE_BEGIN_FILLED, D2D1_FIGURE_END_CLOSED,
    D2D1_FIGURE_END_OPEN,
};
use windows::Win32::Graphics::Direct2D::{
    D2D1_QUADRATIC_BEZIER_SEGMENT, ID2D1Factory, ID2D1Factory1, ID2D1Geometry, ID2D1PathGeometry,
};
use windows::Win32::System::WinRT::Graphics::Direct2D::{IGeometrySource2DInterop, IGeometrySource2DInterop_Impl};
use windows::core::{Interface, Ref, Result, implement};
use windows_numerics::{Matrix3x2, Matrix4x4, Vector2};

use crate::context::Context;

/// Composition brush colors are 8-bit, non-premultiplied sRGB.
pub(crate) fn color(c: graphite::Color) -> windows::UI::Color {
    let q = |v: f32| (v.clamp(0.0, 1.0) * 255.0).round() as u8;
    windows::UI::Color { A: q(c.a), R: q(c.r), G: q(c.g), B: q(c.b) }
}

pub(crate) fn d2d_color(c: graphite::Color) -> D2D1_COLOR_F {
    D2D1_COLOR_F { r: c.r, g: c.g, b: c.b, a: c.a }
}

pub(crate) fn vec2(x: f64, y: f64) -> Vector2 {
    Vector2 { X: x as f32, Y: y as f32 }
}

pub(crate) fn point(p: Point) -> Vector2 {
    vec2(p.x, p.y)
}

pub(crate) fn d2d_rect(r: Rect) -> D2D_RECT_F {
    D2D_RECT_F { left: r.min_x() as f32, top: r.min_y() as f32, right: r.max_x() as f32, bottom: r.max_y() as f32 }
}

/// Numerics matrices are row-vector: `p' = p · M`.
pub(crate) fn matrix3x2(m: Affine) -> Matrix3x2 {
    Matrix3x2 { M11: m.a as f32, M12: m.b as f32, M21: m.c as f32, M22: m.d as f32, M31: m.tx as f32, M32: m.ty as f32 }
}

pub(crate) fn matrix4x4(m: Affine) -> Matrix4x4 {
    Matrix4x4 {
        M11: m.a as f32,
        M12: m.b as f32,
        M13: 0.0,
        M14: 0.0,
        M21: m.c as f32,
        M22: m.d as f32,
        M23: 0.0,
        M24: 0.0,
        M31: 0.0,
        M32: 0.0,
        M33: 1.0,
        M34: 0.0,
        M41: m.tx as f32,
        M42: m.ty as f32,
        M43: 0.0,
        M44: 1.0,
    }
}

/// A Direct2D path geometry for `path`.
pub(crate) fn path_geometry(factory: &ID2D1Factory1, path: &Path, rule: FillRule) -> Result<ID2D1PathGeometry> {
    // SAFETY: the sink is used between Open and Close only.
    unsafe {
        let geometry = factory.CreatePathGeometry()?;
        let sink = geometry.Open()?;
        sink.SetFillMode(Context::fill_mode(rule));
        let mut open = false;
        let mut start = Point::ZERO;
        let begin = |sink: &windows::Win32::Graphics::Direct2D::ID2D1GeometrySink, open: &mut bool, p: Point| {
            if !*open {
                sink.BeginFigure(point(p), D2D1_FIGURE_BEGIN_FILLED);
                *open = true;
            }
        };
        let mut current = Point::ZERO;
        for el in path.elements() {
            match *el {
                PathEl::MoveTo(p) => {
                    if open {
                        sink.EndFigure(D2D1_FIGURE_END_OPEN);
                        open = false;
                    }
                    start = p;
                    current = p;
                }
                PathEl::LineTo(p) => {
                    begin(&sink, &mut open, current);
                    sink.AddLine(point(p));
                    current = p;
                }
                PathEl::QuadTo(c, p) => {
                    begin(&sink, &mut open, current);
                    sink.AddQuadraticBezier(&D2D1_QUADRATIC_BEZIER_SEGMENT { point1: point(c), point2: point(p) });
                    current = p;
                }
                PathEl::CurveTo(c1, c2, p) => {
                    begin(&sink, &mut open, current);
                    sink.AddBezier(&D2D1_BEZIER_SEGMENT { point1: point(c1), point2: point(c2), point3: point(p) });
                    current = p;
                }
                PathEl::Close => {
                    if open {
                        sink.EndFigure(D2D1_FIGURE_END_CLOSED);
                        open = false;
                    }
                    current = start;
                }
            }
        }
        if open {
            sink.EndFigure(D2D1_FIGURE_END_OPEN);
        }
        sink.Close()?;
        geometry.cast()
    }
}

/// Hands a Direct2D geometry to composition (`CompositionPath` takes an
/// `IGeometrySource2D` that exposes it through `IGeometrySource2DInterop`).
#[implement(IGeometrySource2D, IGeometrySource2DInterop)]
struct GeometrySource(ID2D1Geometry);

impl windows::Graphics::IGeometrySource2D_Impl for GeometrySource_Impl {}

impl IGeometrySource2DInterop_Impl for GeometrySource_Impl {
    fn GetGeometry(&self) -> Result<ID2D1Geometry> {
        Ok(self.0.clone())
    }

    fn TryGetGeometryUsingFactory(&self, _factory: Ref<ID2D1Factory>) -> Result<ID2D1Geometry> {
        Err(windows::Win32::Foundation::E_NOTIMPL.into())
    }
}

pub(crate) fn composition_path(factory: &ID2D1Factory1, path: &Path, rule: FillRule) -> Result<CompositionPath> {
    let geometry: ID2D1Geometry = path_geometry(factory, path, rule)?.cast()?;
    let source: IGeometrySource2D = GeometrySource(geometry).into();
    CompositionPath::Create(&source)
}

/// The bounding box of a path's points, control points included (a
/// superset of its exact bounds). `None` for an empty path.
pub(crate) fn control_bounds(path: &Path) -> Option<Rect> {
    let (mut x0, mut y0, mut x1, mut y1) = (f64::INFINITY, f64::INFINITY, f64::NEG_INFINITY, f64::NEG_INFINITY);
    let mut add = |p: Point| {
        x0 = x0.min(p.x);
        y0 = y0.min(p.y);
        x1 = x1.max(p.x);
        y1 = y1.max(p.y);
    };
    for el in path.elements() {
        match *el {
            PathEl::MoveTo(p) | PathEl::LineTo(p) => add(p),
            PathEl::QuadTo(c, p) => {
                add(c);
                add(p);
            }
            PathEl::CurveTo(c1, c2, p) => {
                add(c1);
                add(c2);
                add(p);
            }
            PathEl::Close => {}
        }
    }
    (x0 <= x1 && y0 <= y1).then(|| Rect::new(x0, y0, x1 - x0, y1 - y0))
}

pub(crate) fn inset(r: Rect, d: f64) -> Rect {
    Rect::new(r.min_x() + d, r.min_y() + d, (r.width() - 2.0 * d).max(0.0), (r.height() - 2.0 * d).max(0.0))
}

/// The border ring of a `size` box (fill it even-odd): the rounded box
/// minus the box inset by `width`, whose corners have radius
/// `radius - width`, like Core Animation's borders.
pub(crate) fn border_ring(size: Size, radius: f64, corners: Corners, width: f64) -> Path {
    let outer = Rect::from_size(size);
    let mut b = Path::builder();
    b.add_rounded_rect(outer, radius, corners);
    b.add_rounded_rect(inset(outer, width), (radius - width).max(0.0), corners);
    b.build()
}

/// Corner radii of a rounded box, clamped like `Path::rounded_rect`:
/// `[top-left, top-right, bottom-right, bottom-left]`.
pub(crate) fn corner_radii(size: Size, radius: f64, corners: Corners) -> [f64; 4] {
    let r = radius.max(0.0).min(size.width.abs() / 2.0).min(size.height.abs() / 2.0);
    let pick = |c: Corners| if corners.contains(c) { r } else { 0.0 };
    [pick(Corners::TOP_LEFT), pick(Corners::TOP_RIGHT), pick(Corners::BOTTOM_RIGHT), pick(Corners::BOTTOM_LEFT)]
}
