//! Masks and gradients: a dimmed picture with a soft spotlight (the bright
//! copy is masked by a moving radial-gradient layer), and a linear-gradient
//! badge masked by a rotating star shape.

use std::f64::consts::TAU;

use graphite::*;

use super::util::*;
use super::{CARD, Tile, images};

const SPOT: f64 = 120.0;

pub struct Masks {
    spotlight: GradientLayer,
    badge: GradientLayer,
    badge_mask: ShapeLayer,
}

impl Masks {
    pub fn build(scene: &Scene, card: &Layer) -> Masks {
        let full = Rect::from_size(CARD);
        let picture = images::landscape(CARD.width as u32, CARD.height as u32, 2.0);

        let night = layer(scene, card, full);
        night.set_background(Some(hex(0x0B0D2A)));
        let dim = ContentLayer::new(scene);
        dim.set_frame(full);
        dim.set_contents(Contents::Image(picture.clone()));
        dim.set_opacity(0.3);
        card.add_sublayer(&dim);

        // The bright copy, revealed by a soft radial-gradient mask.
        let lit = ContentLayer::new(scene);
        lit.set_frame(full);
        lit.set_contents(Contents::Image(picture));
        let spotlight = GradientLayer::new(scene);
        spotlight.set_bounds(Rect::new(0.0, 0.0, SPOT, SPOT));
        spotlight.set_gradient(Some(Gradient::Radial {
            center: Point::new(0.5, 0.5),
            radius: 0.5,
            stops: stops(&[(0.0, Color::WHITE), (0.6, Color::WHITE), (1.0, Color::WHITE.with_alpha(0.0))]),
        }));
        lit.set_mask(Some(&spotlight));
        card.add_sublayer(&lit);

        // A gradient badge cut out by a rotating star.
        let badge = GradientLayer::new(scene);
        badge.set_frame(Rect::new(CARD.width - 92.0, 14.0, 78.0, 78.0));
        let badge_mask = ShapeLayer::new(scene);
        badge_mask.set_frame(Rect::new(0.0, 0.0, 78.0, 78.0));
        badge_mask.set_path(Some(star(Point::new(39.0, 39.0), 38.0, 18.0, 6, 0.0)));
        badge_mask.set_fill(Some(Color::WHITE.into()));
        badge.set_mask(Some(&badge_mask));
        card.add_sublayer(&badge);

        Masks { spotlight, badge, badge_mask }
    }
}

impl Tile for Masks {
    fn update(&mut self, t: f64) {
        // The spotlight wanders on a Lissajous curve, in the masked layer's
        // local space.
        let x = CARD.width / 2.0 + 90.0 * (t * 0.9).sin();
        let y = CARD.height / 2.0 + 70.0 * (t * 1.3 + 0.5).sin();
        self.spotlight.set_position(Point::new(x, y));
        self.spotlight.set_transform(scaling(0.85 + 0.25 * wave(t, 2.5, 0.0)));

        let a = t * 0.9;
        self.badge.set_gradient(Some(Gradient::Linear {
            start: Point::new(0.5 - 0.5 * a.cos(), 0.5 - 0.5 * a.sin()),
            end: Point::new(0.5 + 0.5 * a.cos(), 0.5 + 0.5 * a.sin()),
            stops: stops(&[(0.0, hex(YELLOW)), (0.5, hex(PINK)), (1.0, hex(PURPLE))]),
        }));
        self.badge_mask.set_transform(rotation(-t * TAU / 10.0));
    }
}
