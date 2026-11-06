//! Public property types shared by the layer API and the commit batch.

use std::sync::Arc;

use bitflags::bitflags;

use crate::canvas::DisplayList;
use crate::color::Color;
use crate::geometry::{Affine, Point, Rect, Size, Vec2};
use crate::resource::{FillRule, GradientStop, Image, Paint, Path, Stroke};

/// The concrete type of a layer, fixed at creation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum LayerKind {
    /// A plain layer: background, border, shadow and sublayers only.
    Container,
    /// Adds bitmap or drawn contents ([`crate::ContentLayer`]).
    Content,
    /// Adds a filled / stroked vector path ([`crate::ShapeLayer`]).
    Shape,
    /// Adds a gradient fill ([`crate::GradientLayer`]).
    Gradient,
}

bitflags! {
    /// Which corners `corner_radius` applies to.
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub struct Corners: u8 {
        const TOP_LEFT = 1 << 0;
        const TOP_RIGHT = 1 << 1;
        const BOTTOM_LEFT = 1 << 2;
        const BOTTOM_RIGHT = 1 << 3;
        const ALL = Self::TOP_LEFT.bits() | Self::TOP_RIGHT.bits() | Self::BOTTOM_LEFT.bits() | Self::BOTTOM_RIGHT.bits();
    }
}

impl Default for Corners {
    fn default() -> Self {
        Corners::ALL
    }
}

/// A border drawn inside the layer's bounds, above its contents and sublayers.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Border {
    pub width: f64,
    pub color: Color,
}

/// An outer drop shadow.
///
/// Unlike `CALayer`, the shadow is **not** clipped by the layer's own
/// `masks_to_bounds`, and it is never derived from the alpha of the layer's
/// contents: its shape is `path` if set, otherwise the layer's rounded bounds
/// grown by `spread`.
#[derive(Clone, Debug, PartialEq)]
pub struct Shadow {
    pub color: Color,
    pub offset: Vec2,
    /// Gaussian standard deviation, in points. Backends convert to their own
    /// notion of "blur radius".
    pub blur_sigma: f64,
    /// Grows (or, when negative, shrinks) the default rounded-bounds shape
    /// on every side, like CSS `box-shadow`'s spread: rounded corners grow
    /// with it, square corners stay square. Ignored when `path` is set.
    pub spread: f64,
    /// Shadow shape in the layer's local (bounds) coordinate space.
    pub path: Option<Path>,
}

impl Shadow {
    pub fn new(color: Color, offset: Vec2, blur_sigma: f64) -> Self {
        Shadow { color, offset, blur_sigma, spread: 0.0, path: None }
    }

    pub fn with_spread(self, spread: f64) -> Self {
        Shadow { spread, ..self }
    }
}

/// Properties common to every layer kind.
#[derive(Clone, Debug, PartialEq)]
pub struct LayerProps {
    /// The layer's local coordinate space. Sublayers, display lists and shape
    /// paths are expressed in it; `bounds.origin` acts as a scroll offset.
    pub bounds: Rect,
    /// Where the anchor point lies in the superlayer's local space.
    pub position: Point,
    /// Anchor in unit coordinates of `bounds.size`; `(0.5, 0.5)` is the center.
    pub anchor_point: Point,
    /// Applied around the anchor point.
    pub transform: Affine,
    /// Group opacity: the layer and its subtree are composited as a whole.
    pub opacity: f32,
    pub hidden: bool,
    /// Clips contents and sublayers to the (rounded) bounds.
    pub masks_to_bounds: bool,
    pub background: Option<Color>,
    pub corner_radius: f64,
    pub masked_corners: Corners,
    pub border: Option<Border>,
    pub shadow: Option<Shadow>,
}

impl Default for LayerProps {
    fn default() -> Self {
        LayerProps {
            bounds: Rect::ZERO,
            position: Point::ZERO,
            anchor_point: Point::new(0.5, 0.5),
            transform: Affine::IDENTITY,
            opacity: 1.0,
            hidden: false,
            masks_to_bounds: false,
            background: None,
            corner_radius: 0.0,
            masked_corners: Corners::ALL,
            border: None,
            shadow: None,
        }
    }
}

impl LayerProps {
    /// Maps the layer's box space (`(0, 0)` = its top-left corner) to its
    /// superlayer's local space: `T(position) · transform · T(-anchor · size)`.
    pub fn box_to_parent(&self) -> Affine {
        self.box_to_parent_with_size(self.bounds.size)
    }

    /// [`box_to_parent`](Self::box_to_parent) for a box of `size` (the
    /// snapped size, when pixel snapping changed it).
    pub fn box_to_parent_with_size(&self, size: Size) -> Affine {
        Affine::translate(Vec2::new(-self.anchor_point.x * size.width, -self.anchor_point.y * size.height))
            .then(self.transform)
            .then(Affine::translate(self.position.to_vec2()))
    }

    /// The axis-aligned bounding box of the layer in its superlayer's local space.
    pub fn frame(&self) -> Rect {
        let m = self.box_to_parent();
        let Size { width: w, height: h } = self.bounds.size;
        let corners = [Point::new(0.0, 0.0), Point::new(w, 0.0), Point::new(0.0, h), Point::new(w, h)]
            .map(|p| m.transform_point(p));
        let (mut x0, mut y0, mut x1, mut y1) = (f64::INFINITY, f64::INFINITY, f64::NEG_INFINITY, f64::NEG_INFINITY);
        for p in corners {
            x0 = x0.min(p.x);
            y0 = y0.min(p.y);
            x1 = x1.max(p.x);
            y1 = y1.max(p.y);
        }
        Rect::new(x0, y0, x1 - x0, y1 - y0)
    }
}

/// How bitmap contents are placed inside a layer's bounds.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub enum Gravity {
    Center,
    Top,
    Bottom,
    Left,
    Right,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    /// Stretch to fill the bounds.
    #[default]
    Resize,
    /// Scale uniformly to fit, centered.
    ResizeAspect,
    /// Scale uniformly to fill, centered (may overflow; clip with `masks_to_bounds`).
    ResizeAspectFill,
}

impl Gravity {
    /// Places content of size `content` inside a box of size `container`
    /// whose top-left is `(0, 0)`.
    pub fn place(self, content: Size, container: Size) -> Rect {
        let (cw, ch) = (container.width, container.height);
        let (w, h) = (content.width, content.height);
        let aspect = |fill: bool| {
            if w <= 0.0 || h <= 0.0 {
                return Rect::ZERO;
            }
            let (sx, sy) = (cw / w, ch / h);
            let s = if fill { sx.max(sy) } else { sx.min(sy) };
            let (nw, nh) = (w * s, h * s);
            Rect::new((cw - nw) / 2.0, (ch - nh) / 2.0, nw, nh)
        };
        let (left, cx, right) = (0.0, (cw - w) / 2.0, cw - w);
        let (top, cy, bottom) = (0.0, (ch - h) / 2.0, ch - h);
        match self {
            Gravity::Resize => Rect::from_size(container),
            Gravity::ResizeAspect => aspect(false),
            Gravity::ResizeAspectFill => aspect(true),
            Gravity::Center => Rect::new(cx, cy, w, h),
            Gravity::Top => Rect::new(cx, top, w, h),
            Gravity::Bottom => Rect::new(cx, bottom, w, h),
            Gravity::Left => Rect::new(left, cy, w, h),
            Gravity::Right => Rect::new(right, cy, w, h),
            Gravity::TopLeft => Rect::new(left, top, w, h),
            Gravity::TopRight => Rect::new(right, top, w, h),
            Gravity::BottomLeft => Rect::new(left, bottom, w, h),
            Gravity::BottomRight => Rect::new(right, bottom, w, h),
        }
    }
}

/// The contents of a [`crate::ContentLayer`].
#[derive(Clone, Debug, Default, PartialEq)]
pub enum Contents {
    #[default]
    None,
    Image(Image),
    /// Drawing in the layer's local (bounds) coordinate space.
    Drawing(DisplayList),
}

/// Properties of a [`crate::ShapeLayer`]. The path is in the layer's local
/// (bounds) coordinate space.
#[derive(Clone, Debug, PartialEq)]
pub struct ShapeProps {
    pub path: Option<Path>,
    pub fill: Option<Paint>,
    pub fill_rule: FillRule,
    pub stroke: Option<Stroke>,
    /// Portion of the path that is stroked, `0.0..=1.0` of its length.
    pub stroke_start: f64,
    pub stroke_end: f64,
}

impl Default for ShapeProps {
    fn default() -> Self {
        ShapeProps {
            path: None,
            fill: None,
            fill_rule: FillRule::NonZero,
            stroke: None,
            stroke_start: 0.0,
            stroke_end: 1.0,
        }
    }
}

/// The gradient of a [`crate::GradientLayer`], in **unit coordinates** of the
/// layer's bounds (`(0, 0)` top-left, `(1, 1)` bottom-right), so it follows
/// the layer's size.
#[derive(Clone, Debug, PartialEq)]
pub enum Gradient {
    Linear {
        start: Point,
        end: Point,
        stops: Arc<[GradientStop]>,
    },
    /// An ellipse with radii `radius * bounds.size` (a circle on square layers).
    Radial {
        center: Point,
        radius: f64,
        stops: Arc<[GradientStop]>,
    },
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn gravity_places_content() {
        let content = Size::new(20.0, 10.0);
        let container = Size::new(100.0, 100.0);
        assert_eq!(Gravity::Resize.place(content, container), Rect::new(0.0, 0.0, 100.0, 100.0));
        assert_eq!(Gravity::Center.place(content, container), Rect::new(40.0, 45.0, 20.0, 10.0));
        assert_eq!(Gravity::BottomRight.place(content, container), Rect::new(80.0, 90.0, 20.0, 10.0));
        assert_eq!(Gravity::ResizeAspect.place(content, container), Rect::new(0.0, 25.0, 100.0, 50.0));
        assert_eq!(Gravity::ResizeAspectFill.place(content, container), Rect::new(-50.0, 0.0, 200.0, 100.0));
        assert_eq!(Gravity::ResizeAspect.place(Size::ZERO, container), Rect::ZERO);
    }
}
