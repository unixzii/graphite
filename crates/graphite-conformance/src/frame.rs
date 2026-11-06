//! Rendered frames and the probes / measurements taken on them.

use graphite::Point;

/// Premultiplied RGBA8 pixels, row 0 at the top.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Pixels {
    pub width: u32,
    pub height: u32,
    pub data: Vec<u8>,
}

/// A premultiplied RGBA8 color.
pub type Rgba8 = [u8; 4];

/// A captured frame, addressed in points.
pub struct Frame {
    pub pixels: Pixels,
    pub scale: f64,
}

impl Frame {
    /// The device pixel containing point `p`, or `None` outside the frame.
    pub fn at(&self, p: Point) -> Option<Rgba8> {
        let (x, y) = ((p.x * self.scale).floor(), (p.y * self.scale).floor());
        if x < 0.0 || y < 0.0 || x >= self.pixels.width as f64 || y >= self.pixels.height as f64 {
            return None;
        }
        let i = (y as usize * self.pixels.width as usize + x as usize) * 4;
        let d = &self.pixels.data;
        Some([d[i], d[i + 1], d[i + 2], d[i + 3]])
    }

    /// Samples every device pixel along the segment `from`–`to` (in points),
    /// returning `(distance from `from` in points, color in 0..=1)`.
    pub fn profile(&self, from: Point, to: Point) -> Vec<(f64, [f64; 4])> {
        let (dx, dy) = (to.x - from.x, to.y - from.y);
        let len = (dx * dx + dy * dy).sqrt();
        let steps = (len * self.scale).floor() as usize;
        (0..=steps)
            .filter_map(|i| {
                let d = (i as f64 + 0.5) / self.scale;
                if d > len {
                    return None;
                }
                let p = Point::new(from.x + dx * d / len, from.y + dy * d / len);
                self.at(p).map(|c| (d, c.map(|v| v as f64 / 255.0)))
            })
            .collect()
    }
}

/// The first distance at which `channel` of a profile crosses `threshold`
/// (in either direction), linearly interpolated between samples.
pub fn crossing(profile: &[(f64, [f64; 4])], channel: usize, threshold: f64) -> Option<f64> {
    profile.windows(2).find_map(|w| {
        let ((d0, c0), (d1, c1)) = (w[0], w[1]);
        let (v0, v1) = (c0[channel], c1[channel]);
        if (v0 - threshold) * (v1 - threshold) <= 0.0 && v0 != v1 {
            Some(d0 + (threshold - v0) / (v1 - v0) * (d1 - d0))
        } else {
            None
        }
    })
}

/// Total coverage of `channel` along a profile, in points: the width of a
/// line crossing it, independent of how its edges are anti-aliased.
pub fn coverage(profile: &[(f64, [f64; 4])], channel: usize, scale: f64) -> f64 {
    profile.iter().map(|(_, c)| c[channel]).sum::<f64>() / scale
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crossing_interpolates() {
        let p = vec![(0.0, [0.0, 0.0, 0.0, 1.0]), (1.0, [0.0, 0.0, 0.0, 0.5]), (2.0, [0.0, 0.0, 0.0, 0.0])];
        assert_eq!(crossing(&p, 3, 0.75), Some(0.5));
        assert_eq!(crossing(&p, 3, 0.25), Some(1.5));
        assert_eq!(coverage(&p, 3, 2.0), 0.75);
    }
}
