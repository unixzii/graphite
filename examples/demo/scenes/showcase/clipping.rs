//! Clipping, scrolling and corners: a list scrolled by animating its
//! container's `bounds.origin` (background and border stay put, sublayers
//! move), a gradient morphing from square to circle as its corner radius
//! and border grow, cycling masked corners, and a rotating bar clipped by
//! a rounded container.

use graphite::*;

use super::util::*;
use super::{Tile, images};

const PHONE: Rect = Rect::new(20.0, 20.0, 124.0, 244.0);
const ROWS: usize = 12;
const ROW_PITCH: f64 = 60.0;

pub struct Clipping {
    phone: Layer,
    indicator: Layer,
    morph: GradientLayer,
    leaf: Layer,
    bar: Layer,
}

impl Clipping {
    pub fn build(scene: &Scene, card: &Layer) -> Clipping {
        let phone = layer(scene, card, PHONE);
        phone.set_background(Some(hex(0xF2F2F7)));
        phone.set_corner_radius(22.0);
        phone.set_border(Some(Border { width: 3.0, color: hex(INK) }));
        phone.set_masks_to_bounds(true);
        let colors = [BLUE, GREEN, ORANGE, PURPLE, TEAL, PINK, YELLOW, INDIGO];
        for i in 0..ROWS {
            let y = 12.0 + i as f64 * ROW_PITCH;
            let row = pill(scene, &phone, Rect::new(10.0, y, 104.0, 50.0), Color::WHITE, 12.0);
            row.set_shadow(Some(Shadow::new(Color::BLACK.with_alpha(0.08), Vec2::new(0.0, 1.0), 1.5)));
            pill(scene, &row, Rect::new(8.0, 10.0, 30.0, 30.0), hex(colors[i % colors.len()]), 15.0);
            pill(scene, &row, Rect::new(46.0, 14.0, 50.0, 8.0), hex(0xD1D1D6), 4.0);
            pill(scene, &row, Rect::new(46.0, 28.0, 32.0 + (i * 7 % 18) as f64, 8.0), hex(0xE5E5EA), 4.0);
        }
        let indicator = pill(scene, card, Rect::new(PHONE.max_x() + 6.0, PHONE.min_y(), 4.0, 48.0), hex(INK), 2.0);
        indicator.set_opacity(0.35);

        let morph = GradientLayer::new(scene);
        morph.set_frame(Rect::new(176.0, 22.0, 88.0, 88.0));
        morph.set_gradient(Some(Gradient::Linear {
            start: Point::new(0.0, 0.0),
            end: Point::new(1.0, 1.0),
            stops: stops(&[(0.0, hex(TEAL)), (1.0, hex(BLUE))]),
        }));
        morph.set_masks_to_bounds(true);
        card.add_sublayer(&morph);

        let leaf = pill(scene, card, Rect::new(176.0, 126.0, 88.0, 56.0), hex(GREEN), 28.0);

        let frame = layer(scene, card, Rect::new(166.0, 200.0, 108.0, 64.0));
        frame.set_background(Some(hex(0xF2F2F7)));
        frame.set_corner_radius(18.0);
        frame.set_masks_to_bounds(true);
        frame.set_border(Some(Border { width: 2.0, color: hex(INK).with_alpha(0.2) }));
        let tiles = ContentLayer::new(scene);
        tiles.set_frame(Rect::new(0.0, 0.0, 108.0, 64.0));
        tiles.set_contents(Contents::Image(images::checker(0xF2F2F7, 0xE1E3EC)));
        tiles.set_gravity(Gravity::ResizeAspectFill);
        frame.add_sublayer(&tiles);
        let bar = pill(scene, &frame, Rect::new(-26.0, 24.0, 160.0, 16.0), hex(ORANGE), 8.0);

        Clipping { phone, indicator, morph, leaf, bar }
    }
}

impl Tile for Clipping {
    fn update(&mut self, t: f64) {
        // Scroll by moving the bounds origin: the content moves up.
        let content = 24.0 + ROWS as f64 * ROW_PITCH - 10.0;
        let range = content - PHONE.height();
        let progress = smooth(wave(t, 8.0, 0.0));
        self.phone.set_bounds(Rect::new(0.0, progress * range, PHONE.width(), PHONE.height()));
        let track = PHONE.height() - 48.0;
        self.indicator.set_position(Point::new(PHONE.max_x() + 8.0, PHONE.min_y() + 24.0 + progress * track));

        let m = smooth(wave(t, 4.0, 0.0));
        self.morph.set_corner_radius(44.0 * m);
        self.morph.set_border(Some(Border { width: 6.0 * m, color: Color::WHITE.with_alpha(0.9) }));
        self.morph.set_transform(rotation(m * std::f64::consts::FRAC_PI_4));

        let steps = [
            Corners::TOP_LEFT | Corners::BOTTOM_RIGHT,
            Corners::TOP_RIGHT | Corners::BOTTOM_LEFT,
            Corners::TOP_LEFT | Corners::TOP_RIGHT,
            Corners::ALL,
        ];
        self.leaf.set_masked_corners(steps[(t / 1.5) as usize % steps.len()]);

        self.bar.set_transform(rotation(t * 1.2));
    }
}
