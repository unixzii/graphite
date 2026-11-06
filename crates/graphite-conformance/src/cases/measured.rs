//! Tier B: effects reduced to a measured quantity, compared to what the API
//! promises with a semantic tolerance.

use std::f64::consts::SQRT_2;
use std::sync::Arc;

use graphite::*;

use super::{blue, red, solid};
use crate::frame::{coverage, crossing};
use crate::{Ctx, Tier};

pub(crate) const TIER: Tier = Tier::Measured;

const ALPHA: usize = 3;

/// Outside a straight edge, a Gaussian shadow of standard deviation σ has
/// alpha `Φ(-d/σ)` at distance `d`: 15.87 % at d = σ and 2.28 % at d = 2σ.
pub(crate) fn shadow_blur_matches_sigma(ctx: &mut Ctx) {
    measure_shadow_blur(ctx, Some(Color::WHITE));
}

/// Same measurement without a background: backends may draw the shadow
/// differently when its interior can show (GSK: blur vs outset shadow).
pub(crate) fn shadow_blur_matches_sigma_without_background(ctx: &mut Ctx) {
    measure_shadow_blur(ctx, None);
}

fn measure_shadow_blur(ctx: &mut Ctx, background: Option<Color>) {
    const SIGMA: f64 = 4.0;
    let scene = ctx.scene();
    let layer = Layer::new(&scene);
    layer.set_frame(Rect::new(10.0, 10.0, 40.0, 80.0));
    layer.set_background(background);
    scene.root().add_sublayer(&layer);
    layer.set_shadow(Some(Shadow::new(Color::BLACK, Vec2::ZERO, SIGMA)));
    let f = ctx.render(&scene);
    // Outward from the right edge, far from the corners.
    let profile = f.profile(Point::new(50.0, 50.0), Point::new(95.0, 50.0));
    ctx.expect_near("distance to 15.87% alpha (= σ)", crossing(&profile, ALPHA, 0.1587), SIGMA, 0.2 * SIGMA);
    ctx.expect_near(
        "distance to 2.28% alpha (= 2σ), halved",
        crossing(&profile, ALPHA, 0.0228).map(|d| d / 2.0),
        SIGMA,
        0.2 * SIGMA,
    );
}

/// The edge of a spread shadow (its 50 % alpha point) sits `spread` outside
/// the layer, with or without a background.
pub(crate) fn shadow_spread_moves_the_edge(ctx: &mut Ctx) {
    const SPREAD: f64 = 6.0;
    for (label, background) in [("opaque", Some(Color::WHITE)), ("hollow", None)] {
        let scene = ctx.scene();
        let layer = Layer::new(&scene);
        layer.set_frame(Rect::new(10.0, 10.0, 40.0, 80.0));
        layer.set_background(background);
        layer.set_shadow(Some(Shadow::new(Color::BLACK, Vec2::ZERO, 4.0).with_spread(SPREAD)));
        scene.root().add_sublayer(&layer);
        let f = ctx.render(&scene);
        let profile = f.profile(Point::new(50.0, 50.0), Point::new(95.0, 50.0));
        ctx.expect_near(
            &format!("{label}: distance to 50% alpha (= spread)"),
            crossing(&profile, ALPHA, 0.5),
            SPREAD,
            0.6,
        );
    }
}

pub(crate) fn linear_gradient_interpolates_monotonically(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = GradientLayer::new(&scene);
    layer.set_frame(Rect::new(10.0, 40.0, 80.0, 20.0));
    layer.set_gradient(Some(Gradient::Linear {
        start: Point::new(0.0, 0.5),
        end: Point::new(1.0, 0.5),
        stops: Arc::from([GradientStop::new(0.0, red()), GradientStop::new(1.0, blue())]),
    }));
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    let profile = f.profile(Point::new(10.0, 50.0), Point::new(90.0, 50.0));
    const NOISE: f64 = 0.02;
    let red_falls = profile.windows(2).all(|w| w[1].1[0] <= w[0].1[0] + NOISE);
    let blue_rises = profile.windows(2).all(|w| w[1].1[2] + NOISE >= w[0].1[2]);
    ctx.check(red_falls && blue_rises, || "red should fall and blue rise monotonically".into());
    let (first, last) = (profile.first().map(|s| s.1), profile.last().map(|s| s.1));
    ctx.expect_near("red at the start", first.map(|c| c[0]), 1.0, 0.05);
    ctx.expect_near("blue at the end", last.map(|c| c[2]), 1.0, 0.05);
    // Interpolation space (sRGB vs linear) legitimately varies; the
    // midpoint only has to be a genuine mix.
    let mid = f.at(Point::new(50.0, 50.0)).map(|c| c.map(|v| v as f64 / 255.0));
    ctx.expect_near("red at the midpoint", mid.map(|c| c[0]), 0.5, 0.25);
    ctx.expect_near("blue at the midpoint", mid.map(|c| c[2]), 0.5, 0.25);
}

pub(crate) fn stroke_width_matches(ctx: &mut Ctx) {
    const WIDTH: f64 = 6.0;
    let scene = ctx.scene();
    let shape = ShapeLayer::new(&scene);
    shape.set_frame(Rect::new(0.0, 0.0, 100.0, 100.0));
    let mut b = Path::builder();
    b.move_to(Point::new(10.0, 50.3)).line_to(Point::new(90.0, 50.3)); // off the pixel grid on purpose
    shape.set_path(Some(b.build()));
    shape.set_stroke(Some(Stroke::new(red(), WIDTH)));
    scene.root().add_sublayer(&shape);
    let f = ctx.render(&scene);
    let profile = f.profile(Point::new(50.0, 40.0), Point::new(50.0, 60.0));
    ctx.expect_near("stroke coverage", Some(coverage(&profile, ALPHA, f.scale)), WIDTH, 0.5);
}

/// Along the diagonal, a corner of radius r is cut back by r·(√2 − 1).
pub(crate) fn corner_radius_follows_a_circular_arc(ctx: &mut Ctx) {
    const RADIUS: f64 = 20.0;
    let scene = ctx.scene();
    solid(&scene, Rect::new(0.0, 0.0, 60.0, 60.0), red()).set_corner_radius(RADIUS);
    let f = ctx.render(&scene);
    let profile = f.profile(Point::new(0.0, 0.0), Point::new(20.0, 20.0));
    // Profile distances run along the diagonal.
    ctx.expect_near("50% coverage along the diagonal", crossing(&profile, ALPHA, 0.5), RADIUS * (SQRT_2 - 1.0), 0.75);
}

pub(crate) fn group_opacity_halves_alpha(ctx: &mut Ctx) {
    let scene = ctx.scene();
    solid(&scene, Rect::new(0.0, 0.0, 40.0, 40.0), red()).set_opacity(0.5);
    let f = ctx.render(&scene);
    let alpha = f.at(Point::new(20.0, 20.0)).map(|c| c[ALPHA] as f64 / 255.0);
    ctx.expect_near("alpha", alpha, 0.5, 0.02);
}
