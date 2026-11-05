//! Immutable, shareable resources: [`Path`], [`Image`] and the paint types.
//!
//! Resources are reference counted (`Arc`), `Send + Sync`, and never change
//! after construction. That gives backends two guarantees:
//! - "did it change?" is an identity check ([`ResourceId`]);
//! - the native counterpart (a `CGPath`, an `ID2D1PathGeometry`, a `GskPath`,
//!   an SVG `d` string, …) can be built once and cached by id.

use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};

use crate::color::Color;
use crate::geometry::{Point, Rect, Size};

/// Process-unique identity of a resource. Never reused.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct ResourceId(u64);

impl ResourceId {
    pub(crate) fn next() -> Self {
        static NEXT: AtomicU64 = AtomicU64::new(1);
        ResourceId(NEXT.fetch_add(1, Ordering::Relaxed))
    }

    pub fn get(self) -> u64 {
        self.0
    }
}

// ───────────────────────────── Path ─────────────────────────────

/// One segment of a [`Path`].
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PathEl {
    MoveTo(Point),
    LineTo(Point),
    QuadTo(Point, Point),
    CurveTo(Point, Point, Point),
    Close,
}

/// How the interior of a path is determined.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum FillRule {
    #[default]
    NonZero,
    EvenOdd,
}

/// An immutable vector path, in the coordinate space of whatever draws it.
#[derive(Clone)]
pub struct Path(Arc<PathInner>);

struct PathInner {
    id: ResourceId,
    elements: Box<[PathEl]>,
}

impl Path {
    pub fn builder() -> PathBuilder {
        PathBuilder::default()
    }

    pub fn from_elements(elements: impl Into<Box<[PathEl]>>) -> Path {
        Path(Arc::new(PathInner { id: ResourceId::next(), elements: elements.into() }))
    }

    pub fn rect(r: Rect) -> Path {
        let mut b = Path::builder();
        b.add_rect(r);
        b.build()
    }

    pub fn ellipse(r: Rect) -> Path {
        let mut b = Path::builder();
        b.add_ellipse(r);
        b.build()
    }

    pub fn id(&self) -> ResourceId {
        self.0.id
    }

    pub fn elements(&self) -> &[PathEl] {
        &self.0.elements
    }
}

impl PartialEq for Path {
    fn eq(&self, other: &Path) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}

impl std::fmt::Debug for Path {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Path").field("id", &self.0.id).field("len", &self.0.elements.len()).finish()
    }
}

/// Builds a [`Path`].
#[derive(Default)]
pub struct PathBuilder {
    elements: Vec<PathEl>,
}

impl PathBuilder {
    pub fn move_to(&mut self, p: Point) -> &mut Self {
        self.elements.push(PathEl::MoveTo(p));
        self
    }

    pub fn line_to(&mut self, p: Point) -> &mut Self {
        self.elements.push(PathEl::LineTo(p));
        self
    }

    pub fn quad_to(&mut self, c: Point, p: Point) -> &mut Self {
        self.elements.push(PathEl::QuadTo(c, p));
        self
    }

    pub fn curve_to(&mut self, c1: Point, c2: Point, p: Point) -> &mut Self {
        self.elements.push(PathEl::CurveTo(c1, c2, p));
        self
    }

    pub fn close(&mut self) -> &mut Self {
        self.elements.push(PathEl::Close);
        self
    }

    pub fn add_rect(&mut self, r: Rect) -> &mut Self {
        self.move_to(r.origin)
            .line_to(Point::new(r.max_x(), r.min_y()))
            .line_to(Point::new(r.max_x(), r.max_y()))
            .line_to(Point::new(r.min_x(), r.max_y()))
            .close()
    }

    /// Adds an ellipse inscribed in `r`, approximated with four cubic Béziers.
    pub fn add_ellipse(&mut self, r: Rect) -> &mut Self {
        const K: f64 = 0.552_284_749_830_793_6; // 4/3 * (sqrt(2) - 1)
        let (rx, ry) = (r.width() / 2.0, r.height() / 2.0);
        let (cx, cy) = (r.min_x() + rx, r.min_y() + ry);
        let (ox, oy) = (rx * K, ry * K);
        self.move_to(Point::new(cx + rx, cy))
            .curve_to(Point::new(cx + rx, cy + oy), Point::new(cx + ox, cy + ry), Point::new(cx, cy + ry))
            .curve_to(Point::new(cx - ox, cy + ry), Point::new(cx - rx, cy + oy), Point::new(cx - rx, cy))
            .curve_to(Point::new(cx - rx, cy - oy), Point::new(cx - ox, cy - ry), Point::new(cx, cy - ry))
            .curve_to(Point::new(cx + ox, cy - ry), Point::new(cx + rx, cy - oy), Point::new(cx + rx, cy))
            .close()
    }

    pub fn build(&mut self) -> Path {
        Path::from_elements(std::mem::take(&mut self.elements))
    }
}

// ───────────────────────────── Image ─────────────────────────────

/// An immutable bitmap: premultiplied RGBA8 in sRGB.
#[derive(Clone)]
pub struct Image(Arc<ImageInner>);

struct ImageInner {
    id: ResourceId,
    width: u32,
    height: u32,
    /// Pixels per point. A 2x asset has `scale == 2.0`.
    scale: f64,
    pixels: Box<[u8]>,
}

impl Image {
    /// Creates an image from premultiplied RGBA8 pixels, tightly packed.
    ///
    /// # Panics
    /// If `pixels.len() != width * height * 4` or `scale <= 0`.
    #[track_caller]
    pub fn from_rgba8_premultiplied(width: u32, height: u32, scale: f64, pixels: impl Into<Box<[u8]>>) -> Image {
        let pixels = pixels.into();
        assert_eq!(pixels.len(), width as usize * height as usize * 4, "pixel buffer size mismatch");
        assert!(scale > 0.0, "image scale must be positive");
        Image(Arc::new(ImageInner { id: ResourceId::next(), width, height, scale, pixels }))
    }

    pub fn id(&self) -> ResourceId {
        self.0.id
    }
    pub fn pixel_width(&self) -> u32 {
        self.0.width
    }
    pub fn pixel_height(&self) -> u32 {
        self.0.height
    }
    pub fn scale(&self) -> f64 {
        self.0.scale
    }
    pub fn pixels(&self) -> &[u8] {
        &self.0.pixels
    }

    /// Size in points (`pixels / scale`).
    pub fn size(&self) -> Size {
        Size::new(self.0.width as f64 / self.0.scale, self.0.height as f64 / self.0.scale)
    }
}

impl PartialEq for Image {
    fn eq(&self, other: &Image) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}

impl std::fmt::Debug for Image {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Image")
            .field("id", &self.0.id)
            .field("width", &self.0.width)
            .field("height", &self.0.height)
            .field("scale", &self.0.scale)
            .finish()
    }
}

// ───────────────────────────── Paint ─────────────────────────────

/// A color stop. `offset` is in `0.0..=1.0`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GradientStop {
    pub offset: f32,
    pub color: Color,
}

impl GradientStop {
    pub const fn new(offset: f32, color: Color) -> Self {
        GradientStop { offset, color }
    }
}

/// What a fill or stroke is painted with.
#[derive(Clone, Debug, PartialEq)]
pub enum Paint {
    Solid(Color),
    /// Linear gradient between two points, in the drawing's coordinate space.
    Linear {
        start: Point,
        end: Point,
        stops: Arc<[GradientStop]>,
    },
    /// Circular radial gradient, in the drawing's coordinate space.
    Radial {
        center: Point,
        radius: f64,
        stops: Arc<[GradientStop]>,
    },
}

impl From<Color> for Paint {
    fn from(c: Color) -> Paint {
        Paint::Solid(c)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum LineCap {
    #[default]
    Butt,
    Round,
    Square,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum LineJoin {
    #[default]
    Miter,
    Round,
    Bevel,
}

/// Stroke geometry parameters.
#[derive(Clone, Debug, PartialEq)]
pub struct StrokeStyle {
    pub width: f64,
    pub cap: LineCap,
    pub join: LineJoin,
    pub miter_limit: f64,
    pub dash: Arc<[f64]>,
    pub dash_phase: f64,
}

impl Default for StrokeStyle {
    fn default() -> Self {
        StrokeStyle {
            width: 1.0,
            cap: LineCap::Butt,
            join: LineJoin::Miter,
            miter_limit: 10.0,
            dash: Arc::new([]),
            dash_phase: 0.0,
        }
    }
}

impl StrokeStyle {
    pub fn with_width(width: f64) -> Self {
        StrokeStyle { width, ..Default::default() }
    }
}

/// A paint plus stroke geometry.
#[derive(Clone, Debug, PartialEq)]
pub struct Stroke {
    pub paint: Paint,
    pub style: StrokeStyle,
}

impl Stroke {
    pub fn new(paint: impl Into<Paint>, width: f64) -> Self {
        Stroke { paint: paint.into(), style: StrokeStyle::with_width(width) }
    }
}
