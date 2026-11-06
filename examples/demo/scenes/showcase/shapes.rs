//! Shape layers: activity rings driven by `stroke_end`, a morphing blob
//! whose path is rebuilt every frame with marching dashes, a star casting a
//! shadow along a custom path, and an even-odd compound path.

use std::f64::consts::{FRAC_PI_2, TAU};

use graphite::*;

use super::util::*;
use super::{CARD, Tile};

pub struct Shapes {
    rings: Vec<ShapeLayer>,
    blob: ShapeLayer,
    star: ShapeLayer,
    compound: ShapeLayer,
}

fn shape(scene: &Scene, card: &Layer, frame: Rect) -> ShapeLayer {
    let shape = ShapeLayer::new(scene);
    shape.set_frame(frame);
    card.add_sublayer(&shape);
    shape
}

fn blob_path(t: f64) -> Path {
    let c = Point::new(40.0, 40.0);
    let mut b = Path::builder();
    for i in 0..72 {
        let a = i as f64 * TAU / 72.0;
        let r = 27.0 + 5.0 * (5.0 * a + 2.0 * t).sin() + 3.0 * (3.0 * a - 3.0 * t).sin();
        let p = Point::new(c.x + r * a.cos(), c.y + r * a.sin());
        if i == 0 {
            b.move_to(p)
        } else {
            b.line_to(p)
        };
    }
    b.close();
    b.build()
}

/// Two circles and a square in one path; with the even-odd rule the
/// overlaps become holes.
fn compound_path(t: f64) -> Path {
    let mut b = Path::builder();
    b.add_rect(Rect::new(14.0, 14.0, 52.0, 52.0));
    let dx = 14.0 * (t * 1.4).sin();
    b.add_ellipse(Rect::new(4.0 + dx, 4.0, 40.0, 40.0));
    b.add_ellipse(Rect::new(36.0 - dx, 36.0, 40.0, 40.0));
    b.build()
}

impl Shapes {
    pub fn build(scene: &Scene, card: &Layer) -> Shapes {
        let center = Point::new(CARD.width / 2.0, 100.0);
        let mut rings = Vec::new();
        for (radius, color) in [(78.0, PINK), (58.0, GREEN), (38.0, TEAL)] {
            let frame =
                Rect::new(center.x - radius - 8.0, center.y - radius - 8.0, 2.0 * radius + 16.0, 2.0 * radius + 16.0);
            let path = circle(Point::new(radius + 8.0, radius + 8.0), radius);
            for (alpha, is_ring) in [(0.15, false), (1.0, true)] {
                let layer = shape(scene, card, frame);
                layer.set_path(Some(path.clone()));
                let mut stroke = Stroke::new(hex(color).with_alpha(alpha), 16.0);
                stroke.style.cap = LineCap::Round;
                layer.set_stroke(Some(stroke));
                // Start at 12 o'clock.
                layer.set_transform(rotation(-FRAC_PI_2));
                if is_ring {
                    rings.push(layer);
                }
            }
        }

        let blob = shape(scene, card, Rect::new(16.0, 196.0, 80.0, 80.0));
        blob.set_fill(Some(hex(INDIGO).into()));

        let star = shape(scene, card, Rect::new(104.0, 196.0, 80.0, 80.0));
        let star_path = super::util::star(Point::new(40.0, 40.0), 36.0, 15.0, 5, 0.0);
        star.set_path(Some(star_path.clone()));
        star.set_fill(Some(hex(YELLOW).into()));
        let mut stroke = Stroke::new(hex(ORANGE), 2.0);
        stroke.style.join = LineJoin::Round;
        star.set_stroke(Some(stroke));
        let mut shadow = Shadow::new(Color::BLACK.with_alpha(0.35), Vec2::new(0.0, 5.0), 3.0);
        shadow.path = Some(star_path);
        star.set_shadow(Some(shadow));

        let compound = shape(scene, card, Rect::new(192.0, 196.0, 80.0, 80.0));
        compound.set_fill(Some(hex(PURPLE).into()));
        compound.set_fill_rule(FillRule::EvenOdd);

        Shapes { rings, blob, star, compound }
    }
}

impl Tile for Shapes {
    fn update(&mut self, t: f64) {
        for (i, ring) in self.rings.iter().enumerate() {
            ring.set_stroke_end(0.08 + 0.88 * smooth(wave(t, 3.0 + i as f64, i as f64 * 0.2)));
        }
        self.blob.set_path(Some(blob_path(t)));
        let mut ants = Stroke::new(hex(INK).with_alpha(0.8), 2.0);
        ants.style.dash = [6.0, 5.0].into();
        ants.style.dash_phase = t * 22.0;
        self.blob.set_stroke(Some(ants));
        self.star.set_transform(rotation(t * 0.8));
        self.compound.set_path(Some(compound_path(t)));
    }
}
