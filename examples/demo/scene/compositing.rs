//! Compositing: a fan of photo cards rotating around their bottom edge,
//! showing four image gravities and taking turns on top (z-order); group
//! opacity next to per-layer opacity; and a dot blinking with `hidden`.

use graphite::*;

use super::util::*;
use super::{CARD, Tile, images};

const FAN_BASE: Point = Point::new(144.0, 176.0);

pub struct Compositing {
    fan: Layer,
    photos: Vec<ContentLayer>,
    front: usize,
    group: Vec<Layer>,
    single: Vec<Layer>,
    dot: Layer,
}

impl Compositing {
    pub fn build(scene: &Scene, card: &Layer) -> Compositing {
        let fan = layer(scene, card, Rect::from_size(CARD));
        let picture = images::landscape(120, 80, 2.0);
        let gravities = [Gravity::ResizeAspectFill, Gravity::ResizeAspect, Gravity::Resize, Gravity::Center];
        let photos = gravities
            .iter()
            .map(|gravity| {
                let photo = ContentLayer::new(scene);
                photo.set_bounds(Rect::new(0.0, 0.0, 84.0, 112.0));
                photo.set_anchor_point(Point::new(0.5, 1.0));
                photo.set_position(FAN_BASE);
                photo.set_background(Some(hex(0xE5E5EA)));
                photo.set_contents(Contents::Image(picture.clone()));
                photo.set_gravity(*gravity);
                photo.set_corner_radius(12.0);
                photo.set_masks_to_bounds(true);
                photo.set_border(Some(Border { width: 3.0, color: Color::WHITE }));
                photo.set_shadow(Some(Shadow::new(Color::BLACK.with_alpha(0.3), Vec2::new(0.0, 4.0), 5.0)));
                fan.add_sublayer(&photo);
                photo
            })
            .collect();

        // Left: one group at 50 % (the overlap is not darker).
        // Right: each circle at 50 % (the overlap blends).
        let left = layer(scene, card, Rect::new(16.0, 196.0, 124.0, 76.0));
        left.set_opacity(0.5);
        let right = layer(scene, card, Rect::new(148.0, 196.0, 124.0, 76.0));
        let mut group = Vec::new();
        let mut single = Vec::new();
        for (i, color) in [PINK, BLUE].iter().enumerate() {
            let x = 14.0 + i as f64 * 40.0;
            group.push(pill(scene, &left, Rect::new(x, 6.0, 64.0, 64.0), hex(*color), 32.0));
            let c = pill(scene, &right, Rect::new(x, 6.0, 64.0, 64.0), hex(*color), 32.0);
            c.set_opacity(0.5);
            single.push(c);
        }

        let dot = pill(scene, card, Rect::new(CARD.width - 28.0, 14.0, 12.0, 12.0), hex(PINK), 6.0);

        Compositing { fan, photos, front: usize::MAX, group, single, dot }
    }
}

impl Tile for Compositing {
    fn update(&mut self, t: f64) {
        let spread = 0.1 + 0.24 * smooth(wave(t, 5.0, 0.0));
        let n = self.photos.len() as f64;
        for (i, photo) in self.photos.iter().enumerate() {
            photo.set_transform(rotation((i as f64 - (n - 1.0) / 2.0) * spread));
        }
        // A different photo comes to the front every 1.2 s.
        let front = (t / 1.2) as usize % self.photos.len();
        if front != self.front {
            self.front = front;
            self.fan.add_sublayer(&self.photos[front]);
        }

        let slide = 12.0 * (t * 1.6).sin();
        for layers in [&self.group, &self.single] {
            layers[0].set_position(Point::new(14.0 + 32.0 + slide, 38.0));
            layers[1].set_position(Point::new(54.0 + 32.0 - slide, 38.0));
        }

        self.dot.set_hidden((t * 2.0) as usize % 2 == 1);
    }
}
