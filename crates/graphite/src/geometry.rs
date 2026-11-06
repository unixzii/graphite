//! Geometry primitives.
//!
//! Conventions shared by the whole crate:
//! - origin at the top-left, y axis pointing down;
//! - lengths are logical points (`f64`), never device pixels;
//! - angles are radians.

use std::ops::{Add, Mul, Neg, Sub};

/// A point in 2D space.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

impl Point {
    pub const ZERO: Point = Point { x: 0.0, y: 0.0 };

    pub const fn new(x: f64, y: f64) -> Self {
        Point { x, y }
    }

    pub fn to_vec2(self) -> Vec2 {
        Vec2::new(self.x, self.y)
    }
}

impl Add<Vec2> for Point {
    type Output = Point;
    fn add(self, v: Vec2) -> Point {
        Point::new(self.x + v.x, self.y + v.y)
    }
}

impl Sub<Vec2> for Point {
    type Output = Point;
    fn sub(self, v: Vec2) -> Point {
        Point::new(self.x - v.x, self.y - v.y)
    }
}

impl Sub for Point {
    type Output = Vec2;
    fn sub(self, p: Point) -> Vec2 {
        Vec2::new(self.x - p.x, self.y - p.y)
    }
}

/// A 2D displacement.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec2 {
    pub x: f64,
    pub y: f64,
}

impl Vec2 {
    pub const ZERO: Vec2 = Vec2 { x: 0.0, y: 0.0 };

    pub const fn new(x: f64, y: f64) -> Self {
        Vec2 { x, y }
    }

    pub fn to_point(self) -> Point {
        Point::new(self.x, self.y)
    }
}

impl Neg for Vec2 {
    type Output = Vec2;
    fn neg(self) -> Vec2 {
        Vec2::new(-self.x, -self.y)
    }
}

/// A 2D size.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Size {
    pub width: f64,
    pub height: f64,
}

impl Size {
    pub const ZERO: Size = Size { width: 0.0, height: 0.0 };

    pub const fn new(width: f64, height: f64) -> Self {
        Size { width, height }
    }
}

/// An axis-aligned rectangle described by its origin (top-left) and size.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rect {
    pub origin: Point,
    pub size: Size,
}

impl Rect {
    pub const ZERO: Rect = Rect { origin: Point::ZERO, size: Size::ZERO };

    pub const fn new(x: f64, y: f64, width: f64, height: f64) -> Self {
        Rect { origin: Point::new(x, y), size: Size::new(width, height) }
    }

    pub const fn from_size(size: Size) -> Self {
        Rect { origin: Point::ZERO, size }
    }

    pub fn min_x(&self) -> f64 {
        self.origin.x
    }
    pub fn min_y(&self) -> f64 {
        self.origin.y
    }
    pub fn max_x(&self) -> f64 {
        self.origin.x + self.size.width
    }
    pub fn max_y(&self) -> f64 {
        self.origin.y + self.size.height
    }
    pub fn width(&self) -> f64 {
        self.size.width
    }
    pub fn height(&self) -> f64 {
        self.size.height
    }

    pub fn contains(&self, p: Point) -> bool {
        p.x >= self.min_x() && p.x < self.max_x() && p.y >= self.min_y() && p.y < self.max_y()
    }
}

/// How an [`Affine`] transform can be classified. Backends use this to pick
/// the cheapest native representation (e.g. GSK has dedicated translate and
/// scale transforms that are much cheaper than a general matrix).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum TransformKind {
    Identity,
    Translate,
    ScaleTranslate,
    Affine,
}

/// A 2D affine transform, laid out like `CGAffineTransform`:
///
/// ```text
/// | a  c  tx |
/// | b  d  ty |
/// | 0  0  1  |
/// ```
///
/// Points are column vectors: `p' = M · p`. [`Affine::then`] composes in
/// application order.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Affine {
    pub a: f64,
    pub b: f64,
    pub c: f64,
    pub d: f64,
    pub tx: f64,
    pub ty: f64,
}

impl Default for Affine {
    fn default() -> Self {
        Affine::IDENTITY
    }
}

impl Affine {
    pub const IDENTITY: Affine = Affine { a: 1.0, b: 0.0, c: 0.0, d: 1.0, tx: 0.0, ty: 0.0 };

    pub const fn translate(v: Vec2) -> Affine {
        Affine { tx: v.x, ty: v.y, ..Affine::IDENTITY }
    }

    pub const fn scale(sx: f64, sy: f64) -> Affine {
        Affine { a: sx, d: sy, ..Affine::IDENTITY }
    }

    pub fn rotate(radians: f64) -> Affine {
        let (s, c) = radians.sin_cos();
        Affine { a: c, b: s, c: -s, d: c, tx: 0.0, ty: 0.0 }
    }

    /// `self` applied first, then `next`.
    pub fn then(self, next: Affine) -> Affine {
        next * self
    }

    pub fn transform_point(&self, p: Point) -> Point {
        Point::new(self.a * p.x + self.c * p.y + self.tx, self.b * p.x + self.d * p.y + self.ty)
    }

    pub fn determinant(&self) -> f64 {
        self.a * self.d - self.b * self.c
    }

    pub fn inverse(&self) -> Option<Affine> {
        let det = self.determinant();
        if det == 0.0 || !det.is_finite() {
            return None;
        }
        let inv = 1.0 / det;
        Some(Affine {
            a: self.d * inv,
            b: -self.b * inv,
            c: -self.c * inv,
            d: self.a * inv,
            tx: (self.c * self.ty - self.d * self.tx) * inv,
            ty: (self.b * self.tx - self.a * self.ty) * inv,
        })
    }

    pub fn kind(&self) -> TransformKind {
        if self.b != 0.0 || self.c != 0.0 {
            TransformKind::Affine
        } else if self.a != 1.0 || self.d != 1.0 {
            TransformKind::ScaleTranslate
        } else if self.tx != 0.0 || self.ty != 0.0 {
            TransformKind::Translate
        } else {
            TransformKind::Identity
        }
    }

    pub fn is_identity(&self) -> bool {
        self.kind() == TransformKind::Identity
    }
}

/// Matrix product: `(self * rhs)` applies `rhs` first, then `self`.
impl Mul for Affine {
    type Output = Affine;
    fn mul(self, r: Affine) -> Affine {
        Affine {
            a: self.a * r.a + self.c * r.b,
            b: self.b * r.a + self.d * r.b,
            c: self.a * r.c + self.c * r.d,
            d: self.b * r.c + self.d * r.d,
            tx: self.a * r.tx + self.c * r.ty + self.tx,
            ty: self.b * r.tx + self.d * r.ty + self.ty,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn approx(a: Point, b: Point) -> bool {
        (a.x - b.x).abs() < 1e-9 && (a.y - b.y).abs() < 1e-9
    }

    #[test]
    fn then_applies_in_order() {
        let m = Affine::scale(2.0, 2.0).then(Affine::translate(Vec2::new(10.0, 0.0)));
        assert!(approx(m.transform_point(Point::new(1.0, 1.0)), Point::new(12.0, 2.0)));
    }

    #[test]
    fn inverse_round_trips() {
        let m = Affine::rotate(0.3).then(Affine::scale(2.0, 3.0)).then(Affine::translate(Vec2::new(5.0, -7.0)));
        let p = Point::new(3.0, 4.0);
        let q = m.inverse().unwrap().transform_point(m.transform_point(p));
        assert!(approx(p, q));
    }

    #[test]
    fn classifies() {
        assert_eq!(Affine::IDENTITY.kind(), TransformKind::Identity);
        assert_eq!(Affine::translate(Vec2::new(1.0, 0.0)).kind(), TransformKind::Translate);
        assert_eq!(Affine::scale(2.0, 1.0).kind(), TransformKind::ScaleTranslate);
        assert_eq!(Affine::rotate(0.1).kind(), TransformKind::Affine);
    }
}
