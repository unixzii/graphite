//! Colors.

/// A non-premultiplied sRGB color with components in `0.0..=1.0`.
///
/// Wide-gamut color spaces are deliberately out of scope for the MVP; every
/// backend can represent sRGB exactly.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Color {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

impl Color {
    pub const TRANSPARENT: Color = Color::rgba(0.0, 0.0, 0.0, 0.0);
    pub const BLACK: Color = Color::rgb(0.0, 0.0, 0.0);
    pub const WHITE: Color = Color::rgb(1.0, 1.0, 1.0);

    pub const fn rgba(r: f32, g: f32, b: f32, a: f32) -> Self {
        Color { r, g, b, a }
    }

    pub const fn rgb(r: f32, g: f32, b: f32) -> Self {
        Color::rgba(r, g, b, 1.0)
    }

    /// Builds a color from 8-bit sRGB components.
    pub fn rgba8(r: u8, g: u8, b: u8, a: u8) -> Self {
        Color::rgba(r as f32 / 255.0, g as f32 / 255.0, b as f32 / 255.0, a as f32 / 255.0)
    }

    /// Returns the same color with its alpha replaced.
    pub const fn with_alpha(self, a: f32) -> Self {
        Color { a, ..self }
    }
}
