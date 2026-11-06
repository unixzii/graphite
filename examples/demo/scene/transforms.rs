//! Transforms: a clock whose hands rotate around their anchor points, and an
//! orbit of planets (one with its own moon) in nested rotating layers. The
//! orbit layers put their local origin at their center with a negative
//! `bounds.origin`.

use std::f64::consts::TAU;

use graphite::*;

use super::util::*;
use super::{CARD, Tile};

const FACE: f64 = 90.0;
const ORBIT: f64 = 112.0;

pub struct Clock {
    hour: Layer,
    minute: Layer,
    second: Layer,
    orbit: Layer,
    planets: Vec<Layer>,
    moon_orbit: Layer,
}

/// A hand pivoting at `pivot` (fraction of its length from the tip).
fn hand(scene: &Scene, card: &Layer, width: f64, length: f64, pivot: f64, color: Color) -> Layer {
    let hand = Layer::new(scene);
    hand.set_bounds(Rect::new(0.0, 0.0, width, length));
    hand.set_anchor_point(Point::new(0.5, pivot));
    hand.set_position(Point::new(CARD.width / 2.0, CARD.height / 2.0));
    hand.set_background(Some(color));
    hand.set_corner_radius(width / 2.0);
    card.add_sublayer(&hand);
    hand
}

impl Clock {
    pub fn build(scene: &Scene, card: &Layer) -> Clock {
        let center = Point::new(CARD.width / 2.0, CARD.height / 2.0);

        // The orbit: a layer whose local origin is its center.
        let orbit = Layer::new(scene);
        orbit.set_bounds(Rect::new(-ORBIT - 30.0, -ORBIT - 30.0, 2.0 * ORBIT + 60.0, 2.0 * ORBIT + 60.0));
        orbit.set_position(center);
        card.add_sublayer(&orbit);
        let colors = [BLUE, GREEN, ORANGE, PURPLE, TEAL, PINK];
        let mut planets = Vec::new();
        let mut moon_orbit = None;
        for (i, color) in colors.iter().enumerate() {
            let a = i as f64 * TAU / colors.len() as f64;
            let size = if i == 0 { 20.0 } else { 11.0 };
            let at = Point::new(ORBIT * a.cos(), ORBIT * a.sin());
            let planet = pill(
                scene,
                &orbit,
                Rect::new(at.x - size / 2.0, at.y - size / 2.0, size, size),
                hex(*color),
                size / 2.0,
            );
            if i == 0 {
                // A moon on its own orbit, nested inside the planet.
                let mo = Layer::new(scene);
                mo.set_bounds(Rect::new(-16.0, -16.0, 32.0, 32.0));
                mo.set_position(Point::new(size / 2.0, size / 2.0));
                planet.add_sublayer(&mo);
                pill(scene, &mo, Rect::new(13.0 - 3.5, -3.5, 7.0, 7.0), hex(INK), 3.5);
                moon_orbit = Some(mo);
            }
            planets.push(planet);
        }

        // The clock face, drawn once.
        let face = ContentLayer::new(scene);
        face.set_frame(Rect::new(center.x - FACE, center.y - FACE, 2.0 * FACE, 2.0 * FACE));
        face.set_drawer(|cx, info| {
            let c = Point::new(info.bounds.width() / 2.0, info.bounds.height() / 2.0);
            cx.fill_path(
                &circle(c, FACE),
                FillRule::NonZero,
                Paint::Radial { center: c, radius: FACE, stops: stops(&[(0.0, Color::WHITE), (1.0, hex(0xE9EBF2))]) },
            );
            cx.stroke_path(&circle(c, FACE - 1.0), &Stroke::new(Color::BLACK.with_alpha(0.12), 2.0));
            for i in 0..60 {
                cx.with_saved(|cx| {
                    cx.concat_transform(Affine::translate(c.to_vec2()));
                    cx.concat_transform(rotation(i as f64 * TAU / 60.0));
                    let major = i % 5 == 0;
                    let mut b = Path::builder();
                    b.move_to(Point::new(0.0, -FACE + 8.0))
                        .line_to(Point::new(0.0, -FACE + if major { 22.0 } else { 14.0 }));
                    let mut stroke =
                        Stroke::new(hex(INK).with_alpha(if major { 0.9 } else { 0.35 }), if major { 3.0 } else { 1.2 });
                    stroke.style.cap = LineCap::Round;
                    cx.stroke_path(&b.build(), &stroke);
                });
            }
        });
        card.add_sublayer(&face);

        let hour = hand(scene, card, 8.0, 52.0, 0.85, hex(INK));
        let minute = hand(scene, card, 6.0, 76.0, 0.88, hex(INK));
        let second = hand(scene, card, 2.0, 92.0, 0.82, hex(PINK));
        pill(scene, card, Rect::new(center.x - 6.0, center.y - 6.0, 12.0, 12.0), hex(PINK), 6.0);

        Clock { hour, minute, second, orbit, planets, moon_orbit: moon_orbit.unwrap() }
    }
}

impl Tile for Clock {
    fn update(&mut self, t: f64) {
        self.second.set_transform(rotation(t * TAU / 8.0));
        self.minute.set_transform(rotation(1.1 + t * TAU / 60.0));
        self.hour.set_transform(rotation(3.6 + t * TAU / 480.0));
        self.orbit.set_transform(rotation(t * 0.35));
        self.moon_orbit.set_transform(rotation(t * 2.5));
        for (i, planet) in self.planets.iter().enumerate().skip(1) {
            planet.set_transform(scaling(1.0 + 0.35 * (3.0 * t + i as f64).sin()));
        }
    }
}
