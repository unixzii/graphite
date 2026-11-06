//! The demo scene. Platform independent: only uses the graphite API.

use std::f64::consts::TAU;
use std::sync::Arc;

use graphite::*;

pub const SIZE: Size = Size::new(480.0, 320.0);

/// Handles to the layers the animation drives.
pub struct Demo {
    ring: ShapeLayer,
    spinner: Layer,
}

impl Demo {
    /// Populates `scene` with layers showing the main features.
    pub fn build(scene: &Scene) -> Demo {
        let root = scene.root();
        root.set_background(Some(Color::rgba8(242, 242, 247, 255)));

        // A card: rounded, clipped, with a shadow that escapes the clip.
        let card = Layer::new(scene);
        card.set_frame(Rect::new(32.0, 32.0, 240.0, 168.0));
        card.set_background(Some(Color::WHITE));
        card.set_corner_radius(16.0);
        card.set_masks_to_bounds(true);
        card.set_shadow(Some(Shadow::new(Color::BLACK.with_alpha(0.25), Vec2::new(0.0, 8.0), 12.0)));
        root.add_sublayer(&card);

        let header = GradientLayer::new(scene);
        header.set_frame(Rect::new(0.0, 0.0, 240.0, 64.0));
        header.set_gradient(Some(Gradient::Linear {
            start: Point::new(0.0, 0.0),
            end: Point::new(1.0, 1.0),
            stops: Arc::from([
                GradientStop::new(0.0, Color::rgba8(10, 132, 255, 255)),
                GradientStop::new(1.0, Color::rgba8(191, 90, 242, 255)),
            ]),
        }));
        card.add_sublayer(&header);

        // Drawn contents: a small bar chart.
        let chart = ContentLayer::new(scene);
        chart.set_frame(Rect::new(16.0, 80.0, 208.0, 72.0));
        chart.set_drawer(draw_chart);
        card.add_sublayer(&chart);

        // A progress ring driven by `stroke_end`, over a faint track.
        let track = ShapeLayer::new(scene);
        let ring = ShapeLayer::new(scene);
        for (layer, color) in [(&track, Color::BLACK.with_alpha(0.08)), (&ring, Color::rgba8(255, 55, 95, 255))] {
            layer.set_frame(Rect::new(312.0, 40.0, 128.0, 128.0));
            layer.set_path(Some(Path::ellipse(Rect::new(8.0, 8.0, 112.0, 112.0))));
            let mut stroke = Stroke::new(color, 14.0);
            stroke.style.cap = LineCap::Round;
            layer.set_stroke(Some(stroke));
            root.add_sublayer(layer);
        }
        ring.set_transform(Affine::rotate(-TAU / 4.0)); // start at 12 o'clock

        // Image contents with a rounded clip and a border.
        let checker: Vec<u8> = (0..8 * 8)
            .flat_map(|i| if (i % 8 + i / 8) % 2 == 0 { [28, 28, 30, 255] } else { [255, 214, 10, 255] })
            .collect();
        let image = ContentLayer::new(scene);
        image.set_frame(Rect::new(312.0, 208.0, 64.0, 64.0));
        image.set_contents(Contents::Image(Image::from_rgba8_premultiplied(8, 8, 1.0, checker)));
        image.set_corner_radius(12.0);
        image.set_masks_to_bounds(true);
        image.set_border(Some(Border { width: 2.0, color: Color::BLACK.with_alpha(0.2) }));
        root.add_sublayer(&image);

        // A square spinning around its center.
        let spinner = Layer::new(scene);
        spinner.set_frame(Rect::new(400.0, 220.0, 40.0, 40.0));
        spinner.set_background(Some(Color::rgba8(255, 159, 10, 255)));
        spinner.set_corner_radius(6.0);
        root.add_sublayer(&spinner);

        // A gradient shown through a ring-shaped mask.
        let masked = GradientLayer::new(scene);
        masked.set_frame(Rect::new(32.0, 232.0, 56.0, 56.0));
        masked.set_gradient(Some(Gradient::Radial {
            center: Point::new(0.5, 0.5),
            radius: 0.5,
            stops: Arc::from([
                GradientStop::new(0.0, Color::rgb(1.0, 0.2, 0.2)),
                GradientStop::new(1.0, Color::rgb(0.2, 0.2, 1.0)),
            ]),
        }));
        let mask = ShapeLayer::new(scene);
        mask.set_frame(Rect::new(0.0, 0.0, 56.0, 56.0));
        mask.set_path(Some(Path::ellipse(Rect::new(4.0, 4.0, 48.0, 48.0))));
        mask.set_stroke(Some(Stroke::new(Color::WHITE, 8.0)));
        masked.set_mask(Some(&mask));
        root.add_sublayer(&masked);

        Demo { ring, spinner }
    }

    /// Sets the animated properties for `phase` in `0.0..1.0`.
    pub fn pose(&self, phase: f64) {
        self.ring.set_stroke_end(phase);
        self.spinner.set_transform(Affine::rotate(phase * TAU));
    }
}

fn draw_chart(cx: &mut Canvas, info: &DrawInfo) {
    let b = info.bounds;
    let values = [0.3, 0.8, 0.5, 0.95, 0.6, 0.4, 0.75];
    let w = b.width() / values.len() as f64;
    let paint = Paint::Linear {
        start: Point::new(0.0, b.min_y()),
        end: Point::new(0.0, b.max_y()),
        stops: Arc::from([
            GradientStop::new(0.0, Color::rgba8(48, 209, 88, 255)),
            GradientStop::new(1.0, Color::rgba8(0, 122, 255, 255)),
        ]),
    };
    for (i, v) in values.iter().enumerate() {
        let h = b.height() * v;
        let bar = Rect::new(b.min_x() + i as f64 * w + 3.0, b.max_y() - h, w - 6.0, h);
        let path = Path::rounded_rect(bar, 3.0, Corners::TOP_LEFT | Corners::TOP_RIGHT);
        cx.fill_path(&path, FillRule::NonZero, paint.clone());
    }
    cx.stroke_path(
        &Path::rect(Rect::new(b.min_x(), b.max_y() - 0.5, b.width(), 0.0)),
        &Stroke::new(Color::BLACK.with_alpha(0.3), 1.0),
    );
}
