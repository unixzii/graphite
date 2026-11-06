//! The demo scene: six cards, each exercising one area of the API.
//! Platform independent: only the graphite API is used.
//!
//! | Card | Shows |
//! |---|---|
//! | [`transforms`] | anchor points, nested rotations and scales, `bounds.origin` as a coordinate offset |
//! | [`canvas`] | an animated drawer: curves, Béziers, dashes, even-odd fills, gradients, clip paths, alpha, images |
//! | [`shapes`] | shape layers: animated `stroke_end`, morphing paths, marching dashes, even-odd fill, custom shadow paths |
//! | [`masks`] | layer masks (a moving spotlight, a rotating star), linear and radial gradient layers |
//! | [`clipping`] | `masks_to_bounds`, scrolling via `bounds.origin`, animated corner radii, masked corners, borders |
//! | [`compositing`] | z-order changes, shadows, group vs per-layer opacity, `hidden`, image gravity |

mod canvas;
mod clipping;
mod compositing;
mod images;
mod masks;
mod shapes;
mod transforms;
mod util;

use graphite::*;

use util::hex;

pub const SIZE: Size = Size::new(960.0, 640.0);

const COLUMNS: usize = 3;
const ROWS: usize = 2;
const MARGIN: f64 = 24.0;
const GAP: f64 = 24.0;

/// The size of every card.
pub const CARD: Size = Size::new(
    (SIZE.width - 2.0 * MARGIN - (COLUMNS as f64 - 1.0) * GAP) / COLUMNS as f64,
    (SIZE.height - 2.0 * MARGIN - (ROWS as f64 - 1.0) * GAP) / ROWS as f64,
);

/// One card's animation.
pub trait Tile {
    /// Poses the card for time `t`, in seconds.
    fn update(&mut self, t: f64);
}

pub struct Demo {
    tiles: Vec<Box<dyn Tile>>,
}

/// A rounded, clipped card with a soft shadow, at grid cell (`col`, `row`).
fn card(scene: &Scene, col: usize, row: usize) -> Layer {
    let card = Layer::new(scene);
    card.set_frame(Rect::new(
        MARGIN + col as f64 * (CARD.width + GAP),
        MARGIN + row as f64 * (CARD.height + GAP),
        CARD.width,
        CARD.height,
    ));
    card.set_background(Some(Color::WHITE));
    card.set_corner_radius(20.0);
    card.set_masks_to_bounds(true);
    card.set_border(Some(Border { width: 1.0, color: Color::BLACK.with_alpha(0.06) }));
    card.set_shadow(Some(Shadow::new(Color::BLACK.with_alpha(0.16), Vec2::new(0.0, 6.0), 10.0)));
    scene.root().add_sublayer(&card);
    card
}

impl Demo {
    pub fn build(scene: &Scene) -> Demo {
        // A soft backdrop gradient behind the cards.
        let backdrop = GradientLayer::new(scene);
        backdrop.set_frame(Rect::from_size(SIZE));
        backdrop.set_gradient(Some(Gradient::Linear {
            start: Point::new(0.0, 0.0),
            end: Point::new(1.0, 1.0),
            stops: util::stops(&[(0.0, hex(0xEEF1F8)), (1.0, hex(0xF6EEF7))]),
        }));
        scene.root().add_sublayer(&backdrop);

        let tiles: Vec<Box<dyn Tile>> = vec![
            Box::new(transforms::Clock::build(scene, &card(scene, 0, 0))),
            Box::new(canvas::Drawing::build(scene, &card(scene, 1, 0))),
            Box::new(shapes::Shapes::build(scene, &card(scene, 2, 0))),
            Box::new(masks::Masks::build(scene, &card(scene, 0, 1))),
            Box::new(clipping::Clipping::build(scene, &card(scene, 1, 1))),
            Box::new(compositing::Compositing::build(scene, &card(scene, 2, 1))),
        ];
        let mut demo = Demo { tiles };
        demo.update(0.0);
        demo
    }

    /// Poses the whole scene for time `t`, in seconds.
    pub fn update(&mut self, t: f64) {
        for tile in &mut self.tiles {
            tile.update(t);
        }
    }
}
