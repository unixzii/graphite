//! The `ui` scene: a typical application window (header bar with a
//! segmented control, lists, settings rows with a switch and a slider, a
//! calendar, an icon grid, buttons, a text view with a focused search
//! field, split panes). Text is shown as placeholder bars.
//!
//! Only a few small regions animate — a switch flipping every 3 s, a slider
//! drifting, a blinking caret, a spinner, a progress bar — so a renderer
//! with damage tracking redraws very little per frame. It is the
//! counterpart of the `showcase` scene for comparing redraw costs.

use std::f64::consts::{FRAC_PI_2, TAU};

use graphite::*;

use super::DemoScene;
use super::util::*;

pub const SIZE: Size = Size::new(1000.0, 600.0);

const LINE: u32 = 0xD1D1D6;
const FAINT: u32 = 0xF2F2F4;
const SELECT: u32 = 0xE95420;

/// A placeholder for a line of text.
fn text(scene: &Scene, parent: &Layer, x: f64, y: f64, width: f64) -> Layer {
    pill(scene, parent, Rect::new(x, y, width, 8.0), hex(0x3A3A3C).with_alpha(0.85), 4.0)
}

fn muted_text(scene: &Scene, parent: &Layer, x: f64, y: f64, width: f64) -> Layer {
    pill(scene, parent, Rect::new(x, y, width, 8.0), hex(0x3A3A3C).with_alpha(0.3), 4.0)
}

/// A white, bordered, rounded container that clips its content.
fn panel(scene: &Scene, parent: &Layer, frame: Rect) -> Layer {
    let panel = layer(scene, parent, frame);
    panel.set_background(Some(Color::WHITE));
    panel.set_border(Some(Border { width: 1.0, color: hex(LINE) }));
    panel.set_corner_radius(10.0);
    panel.set_masks_to_bounds(true);
    panel
}

fn hairline(scene: &Scene, parent: &Layer, frame: Rect) -> Layer {
    let line = layer(scene, parent, frame);
    line.set_background(Some(hex(LINE)));
    line
}

fn button(scene: &Scene, parent: &Layer, frame: Rect) -> Layer {
    let b = layer(scene, parent, frame);
    b.set_background(Some(Color::WHITE));
    b.set_border(Some(Border { width: 1.0, color: hex(LINE) }));
    b.set_corner_radius(6.0);
    b
}

/// Small line icons, drawn once into a content layer at `frame`.
#[derive(Clone, Copy)]
enum Glyph {
    Plus,
    Minus,
    Ring,
    Dot,
    Square,
    Lines,
    Play,
    Star,
    Download,
    Search,
    Speaker,
    Gear,
    Chevron(f64),
    Check,
    Close,
    Lock,
}

const GLYPHS: [Glyph; 12] = [
    Glyph::Ring,
    Glyph::Speaker,
    Glyph::Dot,
    Glyph::Square,
    Glyph::Lines,
    Glyph::Play,
    Glyph::Star,
    Glyph::Download,
    Glyph::Search,
    Glyph::Gear,
    Glyph::Plus,
    Glyph::Lock,
];

fn draw_glyph(cx: &mut Canvas, glyph: Glyph, c: Point, color: Color) {
    let mut pen = Stroke::new(color, 1.6);
    pen.style.cap = LineCap::Round;
    pen.style.join = LineJoin::Round;
    let line = |cx: &mut Canvas, a: (f64, f64), b: (f64, f64)| {
        let mut p = Path::builder();
        p.move_to(Point::new(c.x + a.0, c.y + a.1)).line_to(Point::new(c.x + b.0, c.y + b.1));
        cx.stroke_path(&p.build(), &pen);
    };
    match glyph {
        Glyph::Plus => {
            line(cx, (-6.0, 0.0), (6.0, 0.0));
            line(cx, (0.0, -6.0), (0.0, 6.0));
        }
        Glyph::Minus => line(cx, (-6.0, 0.0), (6.0, 0.0)),
        Glyph::Ring => cx.stroke_path(&circle(c, 6.0), &pen),
        Glyph::Dot => cx.fill_path(&circle(c, 5.0), FillRule::NonZero, color),
        Glyph::Square => {
            cx.stroke_path(&Path::rounded_rect(Rect::new(c.x - 6.0, c.y - 6.0, 12.0, 12.0), 3.0, Corners::ALL), &pen)
        }
        Glyph::Lines => {
            for dy in [-5.0, 0.0, 5.0] {
                line(cx, (-6.0, dy), (6.0, dy));
            }
        }
        Glyph::Play => {
            let mut p = Path::builder();
            p.move_to(Point::new(c.x - 4.0, c.y - 6.0))
                .line_to(Point::new(c.x + 6.0, c.y))
                .line_to(Point::new(c.x - 4.0, c.y + 6.0))
                .close();
            cx.fill_path(&p.build(), FillRule::NonZero, color);
        }
        Glyph::Star => cx.fill_path(&star(c, 7.0, 3.2, 5, 0.0), FillRule::NonZero, color),
        Glyph::Download => {
            line(cx, (0.0, -7.0), (0.0, 3.0));
            line(cx, (-4.0, -1.0), (0.0, 3.0));
            line(cx, (4.0, -1.0), (0.0, 3.0));
            line(cx, (-6.0, 7.0), (6.0, 7.0));
        }
        Glyph::Search => {
            cx.stroke_path(&circle(Point::new(c.x - 1.5, c.y - 1.5), 5.0), &pen);
            line(cx, (2.0, 2.0), (6.5, 6.5));
        }
        Glyph::Speaker => {
            let mut p = Path::builder();
            p.move_to(Point::new(c.x - 7.0, c.y - 3.0))
                .line_to(Point::new(c.x - 3.0, c.y - 3.0))
                .line_to(Point::new(c.x + 2.0, c.y - 7.0))
                .line_to(Point::new(c.x + 2.0, c.y + 7.0))
                .line_to(Point::new(c.x - 3.0, c.y + 3.0))
                .line_to(Point::new(c.x - 7.0, c.y + 3.0))
                .close();
            cx.fill_path(&p.build(), FillRule::NonZero, color);
            line(cx, (5.0, -3.0), (5.0, 3.0));
        }
        Glyph::Gear => {
            cx.stroke_path(&circle(c, 5.0), &pen);
            for i in 0..6 {
                let a = i as f64 * TAU / 6.0;
                line(cx, (5.0 * a.cos(), 5.0 * a.sin()), (7.5 * a.cos(), 7.5 * a.sin()));
            }
        }
        Glyph::Chevron(angle) => {
            let (s, co) = angle.sin_cos();
            let r = |x: f64, y: f64| (x * co - y * s, x * s + y * co);
            line(cx, r(-2.5, -5.0), r(2.5, 0.0));
            line(cx, r(2.5, 0.0), r(-2.5, 5.0));
        }
        Glyph::Check => {
            line(cx, (-6.0, 0.0), (-2.0, 4.0));
            line(cx, (-2.0, 4.0), (6.0, -5.0));
        }
        Glyph::Close => {
            line(cx, (-4.0, -4.0), (4.0, 4.0));
            line(cx, (4.0, -4.0), (-4.0, 4.0));
        }
        Glyph::Lock => {
            cx.fill_path(
                &Path::rounded_rect(Rect::new(c.x - 6.0, c.y - 1.0, 12.0, 9.0), 2.0, Corners::ALL),
                FillRule::NonZero,
                color,
            );
            cx.stroke_path(
                &Path::rounded_rect(
                    Rect::new(c.x - 4.0, c.y - 8.0, 8.0, 10.0),
                    4.0,
                    Corners::TOP_LEFT | Corners::TOP_RIGHT,
                ),
                &pen,
            );
        }
    }
}

/// A static content layer drawing `glyphs` (positions in its local space).
fn glyphs(scene: &Scene, parent: &Layer, frame: Rect, items: Vec<(Glyph, Point)>, color: Color) -> ContentLayer {
    let layer = ContentLayer::new(scene);
    layer.set_frame(frame);
    layer.set_drawer(move |cx, _| {
        for (glyph, at) in &items {
            draw_glyph(cx, *glyph, *at, color);
        }
    });
    parent.add_sublayer(&layer);
    layer
}

/// Deterministic pseudo-random widths for placeholder text.
fn width(i: usize, min: f64, max: f64) -> f64 {
    let x = ((i as f64 + 1.0) * 12.9898).sin() * 43_758.545;
    min + (x - x.floor()) * (max - min)
}

fn mix(a: Color, b: Color, t: f64) -> Color {
    let t = t as f32;
    Color::rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t)
}

pub struct Ui {
    switch_track: Layer,
    switch_knob: Layer,
    slider_fill: Layer,
    slider_knob: Layer,
    caret: Layer,
    spinner: ShapeLayer,
    progress: Layer,
}

const SWITCH: Rect = Rect::new(164.0, 9.0, 44.0, 24.0);
const SLIDER: (f64, f64) = (96.0, 208.0);
const PROGRESS_WIDTH: f64 = 150.0;

impl Ui {
    pub fn build(scene: &Scene) -> Ui {
        let root = scene.root();
        root.set_background(Some(hex(0xFAFAFA)));
        let ink = hex(INK).with_alpha(0.85);

        // ── Header bar ──
        let header = layer(scene, &root, Rect::new(0.0, 0.0, SIZE.width, 44.0));
        header.set_background(Some(hex(0xEBEBED)));
        hairline(scene, &root, Rect::new(0.0, 44.0, SIZE.width, 1.0));
        let segments = layer(scene, &header, Rect::new(356.0, 8.0, 288.0, 28.0));
        segments.set_background(Some(Color::WHITE));
        segments.set_border(Some(Border { width: 1.0, color: hex(0xC8C8CC) }));
        segments.set_corner_radius(7.0);
        segments.set_masks_to_bounds(true);
        layer(scene, &segments, Rect::new(96.0, 0.0, 96.0, 28.0)).set_background(Some(hex(0xD9D9DD)));
        for i in 0..3 {
            text(scene, &segments, i as f64 * 96.0 + 28.0, 10.0, 40.0);
        }
        let menu = button(scene, &header, Rect::new(864.0, 8.0, 34.0, 28.0));
        glyphs(scene, &menu, Rect::new(0.0, 0.0, 34.0, 28.0), vec![(Glyph::Lines, Point::new(17.0, 14.0))], ink);
        for (i, glyph) in [Glyph::Minus, Glyph::Square, Glyph::Close].iter().enumerate() {
            let c = pill(scene, &header, Rect::new(910.0 + i as f64 * 30.0, 11.0, 22.0, 22.0), hex(0xDCDCE0), 11.0);
            glyphs(scene, &c, Rect::new(0.0, 0.0, 22.0, 22.0), vec![(*glyph, Point::new(11.0, 11.0))], ink);
        }

        // ── Left column ──
        for (i, enabled) in [true, false].iter().enumerate() {
            let spin = panel(scene, &root, Rect::new(12.0 + i as f64 * 52.0, 56.0, 44.0, 120.0));
            hairline(scene, &spin, Rect::new(0.0, 40.0, 44.0, 1.0));
            hairline(scene, &spin, Rect::new(0.0, 80.0, 44.0, 1.0));
            text(scene, &spin, 13.0, 56.0, 18.0);
            glyphs(
                scene,
                &spin,
                Rect::new(0.0, 0.0, 44.0, 120.0),
                vec![(Glyph::Plus, Point::new(22.0, 20.0)), (Glyph::Minus, Point::new(22.0, 100.0))],
                ink,
            );
            if !enabled {
                // A disabled control: the whole group fades together.
                spin.set_opacity(0.4);
            }
        }
        glyphs(
            scene,
            &root,
            Rect::new(116.0, 56.0, 120.0, 120.0),
            vec![(Glyph::Speaker, Point::new(24.0, 32.0)), (Glyph::Ring, Point::new(24.0, 82.0))],
            ink,
        );

        let list = panel(scene, &root, Rect::new(12.0, 188.0, 224.0, 198.0));
        let rows = layer(scene, &list, Rect::new(0.0, 0.0, 224.0, 152.0));
        rows.set_masks_to_bounds(true);
        text(scene, &rows, 12.0, 10.0, 150.0);
        for i in 0..8 {
            text(scene, &rows, 18.0, 28.0 + i as f64 * 17.0, width(i, 50.0, 140.0));
        }
        hairline(scene, &list, Rect::new(0.0, 152.0, 224.0, 1.0));
        let toggles = layer(scene, &list, Rect::new(76.0, 160.0, 72.0, 30.0));
        toggles.set_border(Some(Border { width: 1.0, color: hex(LINE) }));
        toggles.set_corner_radius(6.0);
        toggles.set_masks_to_bounds(true);
        layer(scene, &toggles, Rect::new(0.0, 0.0, 36.0, 30.0)).set_background(Some(hex(0xE5E5EA)));
        glyphs(
            scene,
            &toggles,
            Rect::new(0.0, 0.0, 72.0, 30.0),
            vec![(Glyph::Ring, Point::new(18.0, 15.0)), (Glyph::Square, Point::new(54.0, 15.0))],
            ink,
        );
        let gear = button(scene, &list, Rect::new(180.0, 160.0, 32.0, 30.0));
        glyphs(scene, &gear, Rect::new(0.0, 0.0, 32.0, 30.0), vec![(Glyph::Gear, Point::new(16.0, 15.0))], ink);

        let settings = panel(scene, &root, Rect::new(12.0, 398.0, 224.0, 190.0));
        for i in 0..5 {
            let y = i as f64 * 38.0;
            if i > 0 {
                hairline(scene, &settings, Rect::new(0.0, y, 224.0, 1.0));
            }
            text(scene, &settings, 14.0, y + 15.0, 38.0);
        }
        // Row 1: a switch.
        let switch_track = pill(scene, &settings, SWITCH, hex(0xE0E0E4), 12.0);
        let switch_knob = pill(
            scene,
            &settings,
            Rect::new(SWITCH.min_x() + 2.0, SWITCH.min_y() + 2.0, 20.0, 20.0),
            Color::WHITE,
            10.0,
        );
        switch_knob.set_shadow(Some(Shadow::new(Color::BLACK.with_alpha(0.25), Vec2::new(0.0, 1.0), 1.0)));
        // Row 2: a slider.
        pill(scene, &settings, Rect::new(SLIDER.0, 38.0 + 18.0, SLIDER.1 - SLIDER.0, 3.0), hex(0xE0E0E4), 1.5);
        let slider_fill = pill(scene, &settings, Rect::new(SLIDER.0, 38.0 + 18.0, 60.0, 3.0), hex(SELECT), 1.5);
        let slider_knob = pill(scene, &settings, Rect::new(0.0, 0.0, 18.0, 18.0), Color::WHITE, 9.0);
        slider_knob.set_border(Some(Border { width: 1.0, color: hex(LINE) }));
        slider_knob.set_shadow(Some(Shadow::new(Color::BLACK.with_alpha(0.2), Vec2::new(0.0, 1.0), 1.0)));
        // Row 3: a check mark; row 4: a spin box; row 5: a lock button.
        let spin = button(scene, &settings, Rect::new(116.0, 3.0 * 38.0 + 5.0, 96.0, 28.0));
        hairline(scene, &spin, Rect::new(40.0, 0.0, 1.0, 28.0));
        hairline(scene, &spin, Rect::new(68.0, 0.0, 1.0, 28.0));
        text(scene, &spin, 10.0, 10.0, 16.0);
        let lock = button(scene, &settings, Rect::new(180.0, 4.0 * 38.0 + 5.0, 32.0, 28.0));
        glyphs(scene, &lock, Rect::new(0.0, 0.0, 32.0, 28.0), vec![(Glyph::Lock, Point::new(16.0, 15.0))], ink);
        glyphs(
            scene,
            &settings,
            Rect::new(0.0, 0.0, 224.0, 190.0),
            vec![
                (Glyph::Check, Point::new(200.0, 2.0 * 38.0 + 19.0)),
                (Glyph::Minus, Point::new(116.0 + 54.0, 3.0 * 38.0 + 19.0)),
                (Glyph::Plus, Point::new(116.0 + 82.0, 3.0 * 38.0 + 19.0)),
            ],
            ink,
        );

        // ── Center column ──
        let editor = panel(scene, &root, Rect::new(248.0, 56.0, 464.0, 330.0));
        for (i, w) in [26.0, 26.0, 30.0, 28.0].iter().enumerate() {
            text(scene, &editor, 14.0 + i as f64 * 52.0, 14.0, *w);
        }
        glyphs(
            scene,
            &editor,
            Rect::new(0.0, 36.0, 220.0, 40.0),
            vec![
                (Glyph::Square, Point::new(24.0, 20.0)),
                (Glyph::Download, Point::new(60.0, 20.0)),
                (Glyph::Search, Point::new(110.0, 20.0)),
                (Glyph::Lines, Point::new(146.0, 20.0)),
            ],
            ink,
        );
        hairline(scene, &editor, Rect::new(86.0, 46.0, 1.0, 20.0));
        pill(scene, &editor, Rect::new(180.0, 45.0, 40.0, 22.0), hex(0x9B30B5), 3.0);
        layer(scene, &editor, Rect::new(0.0, 80.0, 464.0, 46.0)).set_background(Some(hex(FAINT)));
        let search = layer(scene, &editor, Rect::new(150.0, 88.0, 164.0, 30.0));
        search.set_background(Some(Color::WHITE));
        search.set_corner_radius(7.0);
        search.set_border(Some(Border { width: 2.0, color: hex(SELECT).with_alpha(0.8) }));
        glyphs(scene, &search, Rect::new(0.0, 0.0, 30.0, 30.0), vec![(Glyph::Search, Point::new(15.0, 15.0))], ink);
        let caret = pill(scene, &search, Rect::new(30.0, 7.0, 1.5, 16.0), hex(INK), 0.0);
        for i in 0..10 {
            text(scene, &editor, 14.0, 138.0 + i as f64 * 15.5, width(i + 20, 180.0, 430.0));
        }
        hairline(scene, &editor, Rect::new(0.0, 296.0, 464.0, 1.0));
        layer(scene, &editor, Rect::new(0.0, 297.0, 464.0, 33.0)).set_background(Some(hex(FAINT)));
        text(scene, &editor, 14.0, 309.0, 170.0);
        pill(scene, &editor, Rect::new(300.0, 310.0, PROGRESS_WIDTH, 6.0), hex(0xE0E0E4), 3.0);
        let progress = pill(scene, &editor, Rect::new(300.0, 310.0, 0.0, 6.0), hex(SELECT), 3.0);

        let left_right = panel(scene, &root, Rect::new(248.0, 398.0, 228.0, 190.0));
        hairline(scene, &left_right, Rect::new(114.0, 0.0, 1.0, 190.0));
        text(scene, &left_right, 42.0, 91.0, 30.0);
        text(scene, &left_right, 154.0, 91.0, 34.0);
        let top_bottom = panel(scene, &root, Rect::new(488.0, 398.0, 224.0, 190.0));
        hairline(scene, &top_bottom, Rect::new(0.0, 95.0, 224.0, 1.0));
        text(scene, &top_bottom, 96.0, 44.0, 28.0);
        text(scene, &top_bottom, 90.0, 138.0, 40.0);
        let spinner = ShapeLayer::new(scene);
        spinner.set_frame(Rect::new(186.0, 154.0, 24.0, 24.0));
        spinner.set_path(Some(circle(Point::new(12.0, 12.0), 9.0)));
        let mut arc = Stroke::new(hex(INK).with_alpha(0.7), 2.5);
        arc.style.cap = LineCap::Round;
        spinner.set_stroke(Some(arc));
        spinner.set_stroke_end(0.7);
        top_bottom.add_sublayer(&spinner);

        // ── Right column ──
        let calendar = panel(scene, &root, Rect::new(724.0, 56.0, 264.0, 204.0));
        text(scene, &calendar, 34.0, 16.0, 62.0);
        text(scene, &calendar, 176.0, 16.0, 34.0);
        for i in 0..7 {
            text(scene, &calendar, 12.0 + i as f64 * 36.0 + 6.0, 42.0, 22.0);
        }
        let day_of = |row: usize, col: usize| row * 7 + col;
        for row in 0..6 {
            for col in 0..7 {
                let x = 12.0 + col as f64 * 36.0;
                let y = 66.0 + row as f64 * 22.0;
                let n = day_of(row, col);
                let outside = !(6..36).contains(&n);
                if n == 11 {
                    pill(scene, &calendar, Rect::new(x + 2.0, y - 4.0, 28.0, 18.0), hex(SELECT), 4.0);
                    pill(scene, &calendar, Rect::new(x + 10.0, y + 1.0, 12.0, 8.0), Color::WHITE, 4.0);
                } else if outside {
                    muted_text(scene, &calendar, x + 9.0, y + 1.0, 14.0);
                } else {
                    text(scene, &calendar, x + 9.0, y + 1.0, if (n - 5) < 10 { 8.0 } else { 14.0 });
                }
            }
        }
        glyphs(
            scene,
            &calendar,
            Rect::new(0.0, 0.0, 264.0, 40.0),
            vec![
                (Glyph::Chevron(std::f64::consts::PI), Point::new(18.0, 20.0)),
                (Glyph::Chevron(0.0), Point::new(112.0, 20.0)),
                (Glyph::Chevron(std::f64::consts::PI), Point::new(158.0, 20.0)),
                (Glyph::Chevron(0.0), Point::new(246.0, 20.0)),
            ],
            ink,
        );
        glyphs(
            scene,
            &root,
            Rect::new(724.0, 266.0, 20.0, 20.0),
            vec![(Glyph::Chevron(FRAC_PI_2), Point::new(10.0, 10.0))],
            ink,
        );
        text(scene, &root, 744.0, 272.0, 56.0);
        let icons = panel(scene, &root, Rect::new(724.0, 292.0, 264.0, 112.0));
        let mut items = Vec::new();
        for row in 0..3 {
            for col in 0..7 {
                let glyph = GLYPHS[(row * 7 + col * 5) % GLYPHS.len()];
                items.push((glyph, Point::new(22.0 + col as f64 * 36.5, 20.0 + row as f64 * 36.0)));
            }
        }
        glyphs(scene, &icons, Rect::new(0.0, 0.0, 264.0, 112.0), items, ink);

        let group = layer(scene, &root, Rect::new(724.0, 414.0, 108.0, 30.0));
        group.set_border(Some(Border { width: 1.0, color: hex(LINE) }));
        group.set_corner_radius(6.0);
        group.set_masks_to_bounds(true);
        group.set_background(Some(Color::WHITE));
        hairline(scene, &group, Rect::new(36.0, 0.0, 1.0, 30.0));
        hairline(scene, &group, Rect::new(72.0, 0.0, 1.0, 30.0));
        glyphs(
            scene,
            &group,
            Rect::new(0.0, 0.0, 108.0, 30.0),
            vec![
                (Glyph::Minus, Point::new(18.0, 15.0)),
                (Glyph::Plus, Point::new(54.0, 15.0)),
                (Glyph::Dot, Point::new(90.0, 15.0)),
            ],
            ink,
        );
        for (i, w) in [44.0, 30.0, 62.0, 42.0].iter().enumerate() {
            let b = button(scene, &root, Rect::new(724.0, 454.0 + i as f64 * 34.0, 264.0, 28.0));
            text(scene, &b, 132.0 - w / 2.0, 10.0, *w);
        }

        Ui { switch_track, switch_knob, slider_fill, slider_knob, caret, spinner, progress }
    }
}

impl DemoScene for Ui {
    fn update(&mut self, t: f64) {
        // The switch flips every 3 s, sliding for 0.25 s.
        let period = 3.0;
        let on = (t / period) as usize % 2 == 1;
        let slide = smooth((t % period) / 0.25);
        let p = if on { slide } else { 1.0 - slide };
        let p = if t < period { 0.0 } else { p };
        self.switch_knob.set_position(Point::new(SWITCH.min_x() + 12.0 + p * 20.0, SWITCH.min_y() + 12.0));
        self.switch_track.set_background(Some(mix(hex(0xE0E0E4), hex(SELECT), p)));

        // The slider drifts slowly.
        let value = 0.2 + 0.6 * smooth(wave(t, 9.0, 0.0));
        let x = SLIDER.0 + value * (SLIDER.1 - SLIDER.0);
        self.slider_fill.set_bounds(Rect::new(0.0, 0.0, x - SLIDER.0, 3.0));
        self.slider_fill.set_position(Point::new(SLIDER.0 + (x - SLIDER.0) / 2.0, 38.0 + 19.5));
        self.slider_knob.set_position(Point::new(x, 38.0 + 19.5));

        self.caret.set_hidden((t * 2.0) as usize % 2 == 1);
        self.spinner.set_transform(rotation(t * TAU));
        let progress = (t % 5.0) / 5.0 * PROGRESS_WIDTH;
        self.progress.set_bounds(Rect::new(0.0, 0.0, progress, 6.0));
        self.progress.set_position(Point::new(300.0 + progress / 2.0, 313.0));
    }
}
