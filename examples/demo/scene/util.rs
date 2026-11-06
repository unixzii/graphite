//! Small helpers shared by the tiles.

use std::f64::consts::{FRAC_PI_2, TAU};
use std::sync::Arc;

use graphite::*;

/// A color from `0xRRGGBB`.
pub fn hex(rgb: u32) -> Color {
    Color::rgba8((rgb >> 16) as u8, (rgb >> 8) as u8, rgb as u8, 255)
}

pub const BLUE: u32 = 0x0A84FF;
pub const INDIGO: u32 = 0x5E5CE6;
pub const PURPLE: u32 = 0xBF5AF2;
pub const PINK: u32 = 0xFF375F;
pub const ORANGE: u32 = 0xFF9F0A;
pub const YELLOW: u32 = 0xFFD60A;
pub const GREEN: u32 = 0x30D158;
pub const TEAL: u32 = 0x64D2FF;
pub const INK: u32 = 0x1C1C1E;

pub fn stops(colors: &[(f32, Color)]) -> Arc<[GradientStop]> {
    colors.iter().map(|&(o, c)| GradientStop::new(o, c)).collect()
}

/// A plain layer with `frame`, added to `parent`.
pub fn layer(scene: &Scene, parent: &Layer, frame: Rect) -> Layer {
    let layer = Layer::new(scene);
    layer.set_frame(frame);
    parent.add_sublayer(&layer);
    layer
}

/// A rounded, colored layer added to `parent`.
pub fn pill(scene: &Scene, parent: &Layer, frame: Rect, color: Color, radius: f64) -> Layer {
    let layer = self::layer(scene, parent, frame);
    layer.set_background(Some(color));
    layer.set_corner_radius(radius);
    layer
}

pub fn circle(center: Point, radius: f64) -> Path {
    Path::ellipse(Rect::new(center.x - radius, center.y - radius, 2.0 * radius, 2.0 * radius))
}

/// A star alternating between `outer` and `inner` radii; its outline never
/// crosses itself.
pub fn star(center: Point, outer: f64, inner: f64, points: usize, rotation: f64) -> Path {
    let mut b = Path::builder();
    for i in 0..points * 2 {
        let r = if i % 2 == 0 { outer } else { inner };
        let a = rotation - FRAC_PI_2 + i as f64 * TAU / (points * 2) as f64;
        let p = Point::new(center.x + r * a.cos(), center.y + r * a.sin());
        if i == 0 {
            b.move_to(p)
        } else {
            b.line_to(p)
        };
    }
    b.close();
    b.build()
}

/// A self-intersecting star: `points` vertices joined every `skip` steps.
pub fn star_polygon(center: Point, radius: f64, points: usize, skip: usize, rotation: f64) -> Path {
    let mut b = Path::builder();
    for i in 0..points {
        let k = (i * skip) % points;
        let a = rotation - FRAC_PI_2 + k as f64 * TAU / points as f64;
        let p = Point::new(center.x + radius * a.cos(), center.y + radius * a.sin());
        if i == 0 {
            b.move_to(p)
        } else {
            b.line_to(p)
        };
    }
    b.close();
    b.build()
}

/// `0..1` easing in and out.
pub fn smooth(x: f64) -> f64 {
    let x = x.clamp(0.0, 1.0);
    x * x * (3.0 - 2.0 * x)
}

/// A `0..1` wave with period `period` seconds.
pub fn wave(t: f64, period: f64, offset: f64) -> f64 {
    0.5 - 0.5 * (TAU * (t / period + offset)).cos()
}

/// Rotation by `angle` around the origin.
pub fn rotation(angle: f64) -> Affine {
    Affine::rotate(angle)
}

/// Uniform scale.
pub fn scaling(s: f64) -> Affine {
    Affine::scale(s, s)
}
