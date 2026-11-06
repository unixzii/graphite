//! Immediate-mode drawing, re-recorded every frame through
//! `set_needs_display`: a gradient-stroked wave, a dashed cubic Bézier with
//! its handles, an even-odd pentagram, and a circular clip path with
//! sliding stripes and a drawn image at reduced alpha.

use std::cell::Cell;
use std::f64::consts::{FRAC_PI_4, TAU};
use std::rc::Rc;

use graphite::*;

use super::util::*;
use super::{CARD, Tile, images};

pub struct Drawing {
    layer: ContentLayer,
    time: Rc<Cell<f64>>,
}

fn round_stroke(paint: impl Into<Paint>, width: f64) -> Stroke {
    let mut s = Stroke::new(paint, width);
    s.style.cap = LineCap::Round;
    s.style.join = LineJoin::Round;
    s
}

fn draw(cx: &mut Canvas, t: f64, picture: &Image) {
    let (w, h) = (CARD.width, CARD.height);

    // Dashed grid.
    let mut grid = Stroke::new(Color::BLACK.with_alpha(0.08), 1.0);
    grid.style.dash = [2.0, 4.0].into();
    let mut b = Path::builder();
    for i in 1..(w / 24.0) as usize {
        b.move_to(Point::new(i as f64 * 24.0, 0.0)).line_to(Point::new(i as f64 * 24.0, h));
    }
    for i in 1..(h / 24.0) as usize {
        b.move_to(Point::new(0.0, i as f64 * 24.0)).line_to(Point::new(w, i as f64 * 24.0));
    }
    cx.stroke_path(&b.build(), &grid);

    // A wave stroked with a linear gradient, and a fainter echo.
    for (k, alpha) in [(1.0, 1.0), (0.6, 0.35)] {
        let mut b = Path::builder();
        for i in 0..=64 {
            let x = 16.0 + i as f64 * (w - 32.0) / 64.0;
            let y = 64.0 + 34.0 * k * (x * 0.04 + t * 2.4).sin() * (x * 0.011 - t * 0.7).cos();
            if i == 0 {
                b.move_to(Point::new(x, y))
            } else {
                b.line_to(Point::new(x, y))
            };
        }
        let paint = Paint::Linear {
            start: Point::new(16.0, 0.0),
            end: Point::new(w - 16.0, 0.0),
            stops: stops(&[(0.0, hex(BLUE)), (0.5, hex(PURPLE)), (1.0, hex(PINK))]),
        };
        cx.with_saved(|cx| {
            cx.set_alpha(alpha as f32);
            cx.stroke_path(&b.build(), &round_stroke(paint, 3.5));
        });
    }

    // A cubic Bézier with animated control points, and its handles.
    let (p0, p3) = (Point::new(24.0, 134.0), Point::new(w - 24.0, 134.0));
    let c1 = Point::new(96.0, 134.0 - 42.0 * (t * 1.3).sin());
    let c2 = Point::new(w - 96.0, 134.0 + 42.0 * (t * 1.1).cos());
    let mut b = Path::builder();
    b.move_to(p0).curve_to(c1, c2, p3);
    let mut dashed = round_stroke(hex(ORANGE), 2.5);
    dashed.style.dash = [8.0, 6.0].into();
    dashed.style.dash_phase = -t * 30.0;
    cx.stroke_path(&b.build(), &dashed);
    let mut handles = Path::builder();
    handles.move_to(p0).line_to(c1).move_to(p3).line_to(c2);
    cx.stroke_path(&handles.build(), &Stroke::new(Color::BLACK.with_alpha(0.25), 1.0));
    for p in [c1, c2] {
        cx.fill_path(&circle(p, 4.0), FillRule::NonZero, hex(ORANGE));
    }

    // An even-odd pentagram: the center pentagon is a hole.
    let star_center = Point::new(76.0, 218.0);
    cx.with_saved(|cx| {
        cx.concat_transform(Affine::translate(star_center.to_vec2()));
        cx.concat_transform(rotation(t * 0.6));
        let star = star_polygon(Point::ZERO, 50.0, 5, 2, 0.0);
        let fill = Paint::Radial {
            center: Point::ZERO,
            radius: 50.0,
            stops: stops(&[(0.0, hex(YELLOW)), (1.0, hex(ORANGE))]),
        };
        cx.fill_path(&star, FillRule::EvenOdd, fill);
        let mut outline = Stroke::new(hex(INK).with_alpha(0.7), 1.5);
        outline.style.join = LineJoin::Miter;
        cx.stroke_path(&star, &outline);
    });

    // A circular clip path: sliding stripes, then an image at 85 % alpha.
    let disc = Point::new(212.0, 218.0);
    cx.with_saved(|cx| {
        cx.clip_path(&circle(disc, 52.0), FillRule::NonZero);
        cx.concat_transform(Affine::translate(disc.to_vec2()));
        cx.with_saved(|cx| {
            cx.concat_transform(rotation(FRAC_PI_4));
            let slide = (t * 24.0) % 24.0;
            for i in -6..6 {
                let color = if i % 2 == 0 { hex(TEAL) } else { hex(INDIGO) };
                cx.fill_rect(Rect::new(i as f64 * 12.0 + slide - 6.0, -80.0, 12.0, 160.0), color);
            }
        });
        cx.set_alpha(0.85);
        cx.concat_transform(rotation(0.25 * (t * 1.5).sin()));
        cx.draw_image(picture, Rect::new(-34.0, -24.0, 68.0, 48.0));
    });
    cx.stroke_path(&circle(disc, 52.0), &Stroke::new(hex(INK).with_alpha(0.7), 2.0));

    // A ring of dots whose alpha follows a rotating highlight.
    for i in 0..12 {
        let a = i as f64 * TAU / 12.0;
        let fade = (0.5 + 0.5 * (a - t * 3.0).cos()) as f32;
        cx.with_saved(|cx| {
            cx.set_alpha(0.15 + 0.85 * fade);
            cx.fill_path(
                &circle(Point::new(144.0 + 16.0 * a.cos(), 218.0 + 16.0 * a.sin()), 3.0),
                FillRule::NonZero,
                hex(INK),
            );
        });
    }
}

impl Drawing {
    pub fn build(scene: &Scene, card: &Layer) -> Drawing {
        let time = Rc::new(Cell::new(0.0));
        let layer = ContentLayer::new(scene);
        layer.set_frame(Rect::from_size(CARD));
        let picture = images::landscape(68, 48, 2.0);
        let clock = time.clone();
        layer.set_drawer(move |cx, _| draw(cx, clock.get(), &picture));
        card.add_sublayer(&layer);
        Drawing { layer, time }
    }
}

impl Tile for Drawing {
    fn update(&mut self, t: f64) {
        self.time.set(t);
        self.layer.set_needs_display();
    }
}
