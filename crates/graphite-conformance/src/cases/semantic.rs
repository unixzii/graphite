//! Tier A: exact semantics, probed well inside or outside flat colors.

use std::f64::consts::FRAC_PI_2;
use std::sync::Arc;

use graphite::*;

use super::{BLACK, BLUE, CLEAR, RED, WHITE, blue, red, solid};
use crate::{Ctx, Tier};

pub(crate) const TIER: Tier = Tier::Semantic;

fn p(x: f64, y: f64) -> Point {
    Point::new(x, y)
}

pub(crate) fn geometry_is_y_down(ctx: &mut Ctx) {
    let scene = ctx.scene();
    solid(&scene, Rect::new(10.0, 10.0, 30.0, 20.0), red());
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "inside", p(15.0, 15.0), RED);
    ctx.expect_color(&f, "below", p(15.0, 85.0), CLEAR);
    ctx.expect_color(&f, "right", p(45.0, 15.0), CLEAR);
}

pub(crate) fn transform_rotates_around_the_anchor_point(ctx: &mut Ctx) {
    let scene = ctx.scene();
    // A horizontal bar centered at (50, 50), turned vertical.
    let bar = solid(&scene, Rect::new(30.0, 45.0, 40.0, 10.0), red());
    bar.set_transform(Affine::rotate(FRAC_PI_2));
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "rotated bar", p(50.0, 32.0), RED);
    ctx.expect_color(&f, "former bar end", p(35.0, 50.0), CLEAR);
}

pub(crate) fn z_order_follows_sublayer_order(ctx: &mut Ctx) {
    let scene = ctx.scene();
    solid(&scene, Rect::new(10.0, 10.0, 40.0, 40.0), red());
    solid(&scene, Rect::new(30.0, 30.0, 40.0, 40.0), blue());
    let below = Layer::new(&scene);
    below.set_frame(Rect::new(4.0, 4.0, 20.0, 20.0));
    below.set_background(Some(Color::WHITE));
    scene.root().insert_sublayer(&below, 0);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "later sibling on top", p(40.0, 40.0), BLUE);
    ctx.expect_color(&f, "inserted at 0 is below", p(15.0, 15.0), RED);
    ctx.expect_color(&f, "inserted at 0 visible elsewhere", p(6.0, 6.0), WHITE);
}

pub(crate) fn hidden_hides_the_subtree(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let parent = Layer::new(&scene);
    parent.set_frame(Rect::new(0.0, 0.0, 50.0, 50.0));
    let child = Layer::new(&scene);
    child.set_frame(Rect::new(0.0, 0.0, 50.0, 50.0));
    child.set_background(Some(red()));
    parent.add_sublayer(&child);
    scene.root().add_sublayer(&parent);
    parent.set_hidden(true);
    solid(&scene, Rect::new(60.0, 60.0, 20.0, 20.0), blue());
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "hidden subtree", p(25.0, 25.0), CLEAR);
    ctx.expect_color(&f, "visible sibling", p(70.0, 70.0), BLUE);
}

pub(crate) fn masks_to_bounds_clips_sublayers(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let parent = Layer::new(&scene);
    parent.set_frame(Rect::new(10.0, 10.0, 40.0, 40.0));
    parent.set_masks_to_bounds(true);
    let child = Layer::new(&scene);
    child.set_frame(Rect::new(20.0, 20.0, 60.0, 60.0));
    child.set_background(Some(red()));
    parent.add_sublayer(&child);
    scene.root().add_sublayer(&parent);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "inside the parent", p(40.0, 40.0), RED);
    ctx.expect_color(&f, "outside the parent", p(60.0, 60.0), CLEAR);
}

pub(crate) fn masked_corners_round_only_selected_corners(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = solid(&scene, Rect::new(0.0, 0.0, 40.0, 40.0), red());
    layer.set_corner_radius(20.0);
    layer.set_masked_corners(Corners::TOP_LEFT);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "rounded top-left", p(1.0, 1.0), CLEAR);
    ctx.expect_color(&f, "square bottom-left", p(1.0, 38.0), RED);
    ctx.expect_color(&f, "square top-right", p(38.0, 1.0), RED);
    ctx.expect_color(&f, "center", p(20.0, 20.0), RED);
}

pub(crate) fn border_is_inside_and_above_sublayers(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = Layer::new(&scene);
    layer.set_frame(Rect::new(10.0, 10.0, 40.0, 40.0));
    layer.set_border(Some(Border { width: 4.0, color: blue() }));
    let fill = Layer::new(&scene);
    fill.set_frame(Rect::new(0.0, 0.0, 40.0, 40.0));
    fill.set_background(Some(red()));
    layer.add_sublayer(&fill);
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "border over the sublayer", p(12.0, 30.0), BLUE);
    ctx.expect_color(&f, "sublayer inside the border", p(30.0, 30.0), RED);
    ctx.expect_color(&f, "nothing outside the bounds", p(8.0, 30.0), CLEAR);
}

pub(crate) fn opacity_is_group_opacity(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let group = Layer::new(&scene);
    group.set_frame(Rect::new(0.0, 0.0, 40.0, 40.0));
    group.set_opacity(0.5);
    scene.root().add_sublayer(&group);
    for x in [0.0, 10.0] {
        let square = Layer::new(&scene);
        square.set_frame(Rect::new(x, 0.0, 20.0, 20.0));
        square.set_background(Some(red()));
        group.add_sublayer(&square);
    }
    let f = ctx.render(&scene);
    let (single, overlap) = (f.at(p(5.0, 10.0)), f.at(p(15.0, 10.0)));
    ctx.check(single.is_some_and(|c| c[3] > 0), || format!("the group is invisible: {single:?}"));
    ctx.check(single == overlap, || format!("overlap {overlap:?} differs from a single square {single:?}"));
}

pub(crate) fn masks_clip_by_alpha(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = solid(&scene, Rect::new(0.0, 0.0, 40.0, 40.0), red());
    let mask = Layer::new(&scene);
    mask.set_frame(Rect::new(0.0, 0.0, 20.0, 40.0));
    mask.set_background(Some(Color::WHITE));
    layer.set_mask(Some(&mask));
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "under the mask", p(10.0, 20.0), RED);
    ctx.expect_color(&f, "outside the mask", p(30.0, 20.0), CLEAR);
}

pub(crate) fn bounds_origin_scrolls_sublayers_and_drawings(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let parent = Layer::new(&scene);
    parent.set_frame(Rect::new(0.0, 0.0, 50.0, 50.0));
    parent.set_bounds(Rect::new(0.0, 20.0, 50.0, 50.0)); // scrolled down by 20
    let child = Layer::new(&scene);
    child.set_background(Some(red()));
    child.set_frame(Rect::new(0.0, 20.0, 10.0, 10.0)); // appears at the top
    parent.add_sublayer(&child);
    scene.root().add_sublayer(&parent);
    let drawn = ContentLayer::new(&scene);
    drawn.set_frame(Rect::new(60.0, 0.0, 20.0, 20.0));
    drawn.set_bounds(Rect::new(0.0, 10.0, 20.0, 20.0));
    drawn.set_drawer(|cx, _| cx.fill_rect(Rect::new(0.0, 10.0, 20.0, 5.0), Color::rgb(0.0, 0.0, 1.0)));
    scene.root().add_sublayer(&drawn);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "scrolled sublayer", p(5.0, 5.0), RED);
    ctx.expect_color(&f, "below the scrolled sublayer", p(5.0, 25.0), CLEAR);
    ctx.expect_color(&f, "scrolled drawing", p(70.0, 2.0), BLUE);
    ctx.expect_color(&f, "below the scrolled drawing", p(70.0, 7.0), CLEAR);
}

/// 1 × 2 pixels: red on top, blue below.
fn red_over_blue() -> Image {
    Image::from_rgba8_premultiplied(1, 2, 1.0, vec![255, 0, 0, 255, 0, 0, 255, 255])
}

pub(crate) fn image_contents_are_upright(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = ContentLayer::new(&scene);
    layer.set_frame(Rect::new(0.0, 0.0, 40.0, 40.0));
    layer.set_contents(Contents::Image(red_over_blue()));
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "top half", p(20.0, 5.0), RED);
    ctx.expect_color(&f, "bottom half", p(20.0, 35.0), BLUE);
}

pub(crate) fn image_gravity_places_contents(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = ContentLayer::new(&scene);
    layer.set_frame(Rect::new(0.0, 0.0, 40.0, 40.0));
    layer.set_contents(Contents::Image(Image::from_rgba8_premultiplied(2, 2, 1.0, [255, 0, 0, 255].repeat(4))));
    layer.set_gravity(Gravity::BottomRight);
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "bottom-right corner", p(39.0, 39.0), RED);
    ctx.expect_color(&f, "left of the image", p(36.5, 39.0), CLEAR);
    ctx.expect_color(&f, "top-left", p(1.0, 1.0), CLEAR);
}

pub(crate) fn drawings_are_upright(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = ContentLayer::new(&scene);
    layer.set_frame(Rect::new(0.0, 0.0, 40.0, 40.0));
    layer.set_drawer(|cx, info| {
        let b = info.bounds;
        cx.fill_rect(Rect::new(b.min_x(), b.min_y(), b.width(), b.height() / 2.0), Color::rgb(1.0, 0.0, 0.0));
        cx.draw_image(&red_over_blue(), Rect::new(b.min_x() + 20.0, b.min_y() + 20.0, 20.0, 20.0));
    });
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "filled top half", p(10.0, 5.0), RED);
    ctx.expect_color(&f, "empty bottom half", p(10.0, 35.0), CLEAR);
    ctx.expect_color(&f, "drawn image top", p(30.0, 22.0), RED);
    ctx.expect_color(&f, "drawn image bottom", p(30.0, 38.0), BLUE);
}

pub(crate) fn canvas_state_is_saved_and_restored(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = ContentLayer::new(&scene);
    layer.set_frame(Rect::new(0.0, 0.0, 50.0, 50.0));
    layer.set_drawer(|cx, _| {
        cx.with_saved(|cx| {
            cx.concat_transform(Affine::translate(Vec2::new(20.0, 0.0)));
            cx.fill_rect(Rect::new(0.0, 0.0, 10.0, 10.0), Color::rgb(1.0, 0.0, 0.0));
        });
        cx.fill_rect(Rect::new(0.0, 20.0, 10.0, 10.0), Color::rgb(0.0, 0.0, 1.0));
    });
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "translated fill", p(25.0, 5.0), RED);
    ctx.expect_color(&f, "untranslated origin", p(5.0, 5.0), CLEAR);
    ctx.expect_color(&f, "fill after restore", p(5.0, 25.0), BLUE);
}

pub(crate) fn canvas_clip_and_alpha_apply(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = ContentLayer::new(&scene);
    layer.set_frame(Rect::new(0.0, 0.0, 60.0, 80.0));
    layer.set_drawer(|cx, _| {
        cx.with_saved(|cx| {
            cx.clip_rect(Rect::new(0.0, 0.0, 20.0, 40.0));
            cx.fill_rect(Rect::new(0.0, 0.0, 40.0, 40.0), Color::rgb(1.0, 0.0, 0.0));
        });
        cx.with_saved(|cx| {
            cx.set_alpha(0.5);
            cx.fill_rect(Rect::new(0.0, 50.0, 20.0, 20.0), Color::rgb(0.0, 0.0, 1.0));
        });
        cx.fill_rect(Rect::new(30.0, 50.0, 20.0, 20.0), Color::rgb(0.0, 0.0, 1.0));
    });
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "inside the clip", p(10.0, 10.0), RED);
    ctx.expect_color(&f, "outside the clip", p(30.0, 10.0), CLEAR);
    ctx.expect_color_within(&f, "half alpha", p(10.0, 60.0), [0, 0, 128, 128], 10);
    ctx.expect_color(&f, "alpha restored", p(40.0, 60.0), BLUE);
}

pub(crate) fn shapes_use_local_coordinates(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let shape = ShapeLayer::new(&scene);
    shape.set_frame(Rect::new(10.0, 10.0, 40.0, 40.0));
    shape.set_path(Some(Path::rect(Rect::new(0.0, 0.0, 40.0, 10.0))));
    shape.set_fill(Some(red().into()));
    scene.root().add_sublayer(&shape);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "filled path", p(30.0, 15.0), RED);
    ctx.expect_color(&f, "rest of the layer", p(30.0, 30.0), CLEAR);
}

pub(crate) fn stroke_end_trims_the_path(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let shape = ShapeLayer::new(&scene);
    shape.set_frame(Rect::new(0.0, 0.0, 100.0, 100.0));
    let mut b = Path::builder();
    b.move_to(p(10.0, 50.0)).line_to(p(90.0, 50.0));
    shape.set_path(Some(b.build()));
    shape.set_stroke(Some(Stroke::new(red(), 6.0)));
    shape.set_stroke_end(0.5);
    scene.root().add_sublayer(&shape);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "stroked first half", p(30.0, 50.0), RED);
    ctx.expect_color(&f, "trimmed second half", p(70.0, 50.0), CLEAR);
}

fn red_to_blue() -> Arc<[GradientStop]> {
    Arc::from([GradientStop::new(0.0, red()), GradientStop::new(1.0, blue())])
}

pub(crate) fn linear_gradient_unit_space_is_y_down(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = GradientLayer::new(&scene);
    layer.set_frame(Rect::new(0.0, 0.0, 40.0, 40.0));
    layer.set_gradient(Some(Gradient::Linear { start: p(0.0, 0.0), end: p(0.0, 1.0), stops: red_to_blue() }));
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    let (top, bottom) = (f.at(p(20.0, 0.5)), f.at(p(20.0, 39.5)));
    ctx.check(top.is_some_and(|c| c[0] > 220 && c[2] < 35), || format!("top should be red, got {top:?}"));
    ctx.check(bottom.is_some_and(|c| c[2] > 220 && c[0] < 35), || format!("bottom should be blue, got {bottom:?}"));
}

pub(crate) fn radial_gradient_is_centered(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = GradientLayer::new(&scene);
    layer.set_frame(Rect::new(0.0, 0.0, 80.0, 40.0));
    layer.set_gradient(Some(Gradient::Radial { center: p(0.5, 0.5), radius: 0.5, stops: red_to_blue() }));
    scene.root().add_sublayer(&layer);
    let f = ctx.render(&scene);
    let center = f.at(p(40.0, 20.0));
    // The ellipse follows the layer's aspect ratio: x radius 40, y radius 20.
    let (right, bottom) = (f.at(p(78.5, 20.0)), f.at(p(40.0, 19.0 + 19.5)));
    ctx.check(center.is_some_and(|c| c[0] > 220 && c[2] < 35), || format!("center should be red, got {center:?}"));
    ctx.check(right.is_some_and(|c| c[2] > 200), || format!("near the x radius should be blue, got {right:?}"));
    ctx.check(bottom.is_some_and(|c| c[2] > 200), || format!("near the y radius should be blue, got {bottom:?}"));
}

pub(crate) fn shadow_offset_is_y_down_and_escapes_masks_to_bounds(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = solid(&scene, Rect::new(30.0, 30.0, 20.0, 20.0), Color::WHITE);
    layer.set_masks_to_bounds(true);
    layer.set_corner_radius(4.0);
    layer.set_shadow(Some(Shadow::new(Color::BLACK, Vec2::new(0.0, 15.0), 0.5)));
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "the layer", p(40.0, 40.0), WHITE);
    ctx.expect_color(&f, "shadow below", p(40.0, 60.0), BLACK);
    ctx.expect_color(&f, "nothing above", p(40.0, 20.0), CLEAR);
}

pub(crate) fn fractional_positions_snap_to_device_pixels(ctx: &mut Ctx) {
    let s = ctx.scale();
    let snap = |v: f64| (v * s).round() / s;
    let px = 1.0 / s;
    let scene = ctx.scene();
    // A top-level layer at x = 10.3.
    solid(&scene, Rect::new(10.3, 10.0, 20.0, 20.0), red());
    // Nested: parent at y = 40.3 scrolled by 0.4, child at y = 0.2. Each
    // level snaps on its own.
    let parent = Layer::new(&scene);
    parent.set_frame(Rect::new(0.0, 40.3, 60.0, 50.0));
    parent.set_bounds(Rect::new(0.0, 0.4, 60.0, 50.0));
    let child = Layer::new(&scene);
    child.set_frame(Rect::new(10.0, 0.2, 20.0, 20.0));
    child.set_background(Some(blue()));
    parent.add_sublayer(&child);
    scene.root().add_sublayer(&parent);
    let f = ctx.render(&scene);

    let left = snap(10.3);
    ctx.expect_color_within(&f, "first column", p(left + px / 2.0, 20.0), RED, 3);
    ctx.expect_color_within(&f, "column before", p(left - px / 2.0, 20.0), CLEAR, 3);
    ctx.expect_color_within(&f, "last column", p(left + 20.0 - px / 2.0, 20.0), RED, 3);
    ctx.expect_color_within(&f, "column after", p(left + 20.0 + px / 2.0, 20.0), CLEAR, 3);

    let top = snap(40.3) - snap(0.4) + snap(0.2);
    ctx.expect_color_within(&f, "nested first row", p(20.0, top + px / 2.0), BLUE, 3);
    ctx.expect_color_within(&f, "nested row before", p(20.0, top - px / 2.0), CLEAR, 3);
}

pub(crate) fn updates_and_removals_reach_the_screen(ctx: &mut Ctx) {
    let scene = ctx.scene();
    let layer = solid(&scene, Rect::new(0.0, 0.0, 20.0, 20.0), red());
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "initial", p(5.0, 5.0), RED);

    layer.set_background(Some(blue()));
    layer.set_position(p(70.0, 70.0));
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "old position", p(5.0, 5.0), CLEAR);
    ctx.expect_color(&f, "new position and color", p(65.0, 65.0), BLUE);

    let image = ContentLayer::new(&scene);
    image.set_frame(Rect::new(0.0, 0.0, 20.0, 20.0));
    image.set_contents(Contents::Image(red_over_blue()));
    scene.root().add_sublayer(&image);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "added contents", p(10.0, 2.0), RED);

    image.set_contents(Contents::None);
    layer.remove_from_superlayer();
    drop(layer);
    let f = ctx.render(&scene);
    ctx.expect_color(&f, "cleared contents", p(10.0, 2.0), CLEAR);
    ctx.expect_color(&f, "removed layer", p(65.0, 65.0), CLEAR);
}
