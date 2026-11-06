//! Pixel tests: build a scene, render it offscreen with `CARenderer`, and
//! probe pixels. Every render is also written to
//! `target/graphite-macos-renders/<test>.png` for inspection.
#![cfg(target_os = "macos")]

use std::sync::Arc;

use graphite::*;
use graphite_macos::offscreen::{self, Pixels};
use objc2_core_foundation::{CGPoint, CGRect, CGSize};
use objc2_core_graphics::CGColor;
use objc2_quartz_core::CALayer;

const W: usize = 100;
const H: usize = 100;

fn save(name: &str, px: &Pixels) {
    let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../target/graphite-macos-renders");
    std::fs::create_dir_all(&dir).unwrap();
    let file = std::fs::File::create(dir.join(format!("{name}.png"))).unwrap();
    let mut enc = png::Encoder::new(std::io::BufWriter::new(file), px.width as u32, px.height as u32);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    enc.write_header().unwrap().write_image_data(&px.data).unwrap();
}

fn container() -> objc2::rc::Retained<CALayer> {
    let container = CALayer::new();
    container.setAnchorPoint(CGPoint::new(0.0, 0.0));
    container.setFrame(CGRect::new(CGPoint::new(0.0, 0.0), CGSize::new(W as f64, H as f64)));
    container.setContentsScale(1.0);
    container
}

/// Builds a 100×100 scene, commits it and renders it.
fn render(name: &str, build: impl FnOnce(&Scene)) -> Pixels {
    let scene = Scene::new(Size::new(W as f64, H as f64));
    build(&scene);
    let container = container();
    let _host = graphite_macos::attach_to_layer(&scene, &container);
    scene.commit();
    let px = offscreen::render(&container, W, H).expect("a Metal device is required");
    save(name, &px);
    px
}

#[track_caller]
fn assert_px(px: &Pixels, x: usize, y: usize, expected: [u8; 4]) {
    let got = px.at(x, y);
    let close = got.iter().zip(expected).all(|(a, b)| a.abs_diff(b) <= 8);
    assert!(close, "pixel ({x}, {y}) is {got:?}, expected {expected:?}");
}

const RED: [u8; 4] = [255, 0, 0, 255];
const BLUE: [u8; 4] = [0, 0, 255, 255];
const CLEAR: [u8; 4] = [0, 0, 0, 0];

fn red() -> Color {
    Color::rgb(1.0, 0.0, 0.0)
}
fn blue() -> Color {
    Color::rgb(0.0, 0.0, 1.0)
}

fn add(scene: &Scene, layer: &Layer, frame: Rect) {
    layer.set_frame(frame);
    scene.root().add_sublayer(layer);
}

/// Establishes the read-back orientation independently of graphite: in an
/// unflipped Core Animation tree, y = 0 is the bottom of the screen.
#[test]
fn calibration_unflipped_layer_is_at_the_bottom() {
    let container = container();
    let sub = CALayer::new();
    sub.setFrame(CGRect::new(CGPoint::new(0.0, 0.0), CGSize::new(10.0, 10.0)));
    sub.setBackgroundColor(Some(&CGColor::new_srgb(1.0, 0.0, 0.0, 1.0)));
    container.addSublayer(&sub);
    let px = offscreen::render(&container, W, H).unwrap();
    save("calibration", &px);
    assert_px(&px, 5, 95, RED);
    assert_px(&px, 5, 5, CLEAR);
}

#[test]
fn geometry_is_y_down() {
    let px = render("geometry", |scene| {
        let layer = Layer::new(scene);
        layer.set_background(Some(red()));
        add(scene, &layer, Rect::new(10.0, 10.0, 30.0, 20.0));
    });
    assert_px(&px, 15, 15, RED);
    assert_px(&px, 15, 85, CLEAR);
    assert_px(&px, 45, 15, CLEAR);
}

#[test]
fn transform_rotates_around_the_anchor_point() {
    let px = render("transform", |scene| {
        let layer = Layer::new(scene);
        layer.set_background(Some(red()));
        add(scene, &layer, Rect::new(30.0, 45.0, 40.0, 10.0)); // a horizontal bar centered at (50, 50)
        layer.set_transform(Affine::rotate(std::f64::consts::FRAC_PI_2));
    });
    assert_px(&px, 50, 32, RED); // now vertical
    assert_px(&px, 35, 50, CLEAR);
}

#[test]
fn image_contents_are_upright() {
    let px = render("image", |scene| {
        // 1×2 image: top pixel red, bottom pixel blue.
        let image = Image::from_rgba8_premultiplied(1, 2, 1.0, vec![255, 0, 0, 255, 0, 0, 255, 255]);
        let layer = ContentLayer::new(scene);
        layer.set_contents(Contents::Image(image));
        add(scene, &layer, Rect::new(0.0, 0.0, 40.0, 40.0));
    });
    assert_px(&px, 20, 5, RED);
    assert_px(&px, 20, 35, BLUE);
}

#[test]
fn image_gravity_places_contents() {
    let px = render("image_gravity", |scene| {
        let image = Image::from_rgba8_premultiplied(1, 1, 1.0, vec![255, 0, 0, 255]);
        let layer = ContentLayer::new(scene);
        layer.set_contents(Contents::Image(image));
        layer.set_gravity(Gravity::BottomRight);
        add(scene, &layer, Rect::new(0.0, 0.0, 40.0, 40.0));
    });
    assert_px(&px, 39, 39, RED);
    assert_px(&px, 0, 0, CLEAR);
    assert_px(&px, 38, 38, CLEAR);
}

#[test]
fn drawings_are_upright() {
    let px = render("drawing", |scene| {
        let layer = ContentLayer::new(scene);
        layer.set_drawer(|cx, info| {
            let b = info.bounds;
            cx.fill_rect(Rect::new(b.min_x(), b.min_y(), b.width(), b.height() / 2.0), Color::rgb(1.0, 0.0, 0.0));
            let image = Image::from_rgba8_premultiplied(1, 2, 1.0, vec![255, 0, 0, 255, 0, 0, 255, 255]);
            cx.draw_image(&image, Rect::new(b.min_x() + 20.0, b.min_y() + 20.0, 20.0, 20.0));
        });
        add(scene, &layer, Rect::new(0.0, 0.0, 40.0, 40.0));
    });
    assert_px(&px, 10, 5, RED);
    assert_px(&px, 10, 35, CLEAR);
    assert_px(&px, 30, 22, RED);
    assert_px(&px, 30, 38, BLUE);
}

#[test]
fn bounds_origin_scrolls_sublayers_and_drawings() {
    let px = render("bounds_origin", |scene| {
        let parent = Layer::new(scene);
        parent.set_frame(Rect::new(0.0, 0.0, 50.0, 50.0));
        parent.set_bounds(Rect::new(0.0, 20.0, 50.0, 50.0)); // scrolled down by 20
        let child = Layer::new(scene);
        child.set_background(Some(red()));
        child.set_frame(Rect::new(0.0, 20.0, 10.0, 10.0)); // appears at the top
        parent.add_sublayer(&child);
        let drawn = ContentLayer::new(scene);
        drawn.set_frame(Rect::new(20.0, 0.0, 20.0, 20.0));
        drawn.set_bounds(Rect::new(0.0, 10.0, 20.0, 20.0));
        drawn.set_drawer(|cx, _| cx.fill_rect(Rect::new(0.0, 10.0, 20.0, 5.0), Color::rgb(0.0, 0.0, 1.0)));
        scene.root().add_sublayer(&parent);
        scene.root().add_sublayer(&drawn);
    });
    assert_px(&px, 5, 5, RED);
    assert_px(&px, 5, 25, CLEAR);
    assert_px(&px, 30, 2, BLUE);
    assert_px(&px, 30, 7, CLEAR);
}

#[test]
fn linear_gradient_unit_space_is_y_down() {
    let px = render("gradient", |scene| {
        let layer = GradientLayer::new(scene);
        layer.set_gradient(Some(Gradient::Linear {
            start: Point::new(0.0, 0.0),
            end: Point::new(0.0, 1.0),
            stops: Arc::from([GradientStop::new(0.0, red()), GradientStop::new(1.0, blue())]),
        }));
        add(scene, &layer, Rect::new(0.0, 0.0, 40.0, 40.0));
    });
    let top = px.at(20, 1);
    let bottom = px.at(20, 38);
    assert!(top[0] > 230 && top[2] < 25, "top should be red, got {top:?}");
    assert!(bottom[2] > 230 && bottom[0] < 25, "bottom should be blue, got {bottom:?}");
}

#[test]
fn shapes_use_local_coordinates() {
    let px = render("shape", |scene| {
        let layer = ShapeLayer::new(scene);
        layer.set_path(Some(Path::rect(Rect::new(0.0, 0.0, 40.0, 10.0))));
        layer.set_fill(Some(red().into()));
        add(scene, &layer, Rect::new(10.0, 10.0, 40.0, 40.0));
    });
    assert_px(&px, 30, 15, RED);
    assert_px(&px, 30, 30, CLEAR);
}

#[test]
fn shadow_offset_is_y_down_and_escapes_masks_to_bounds() {
    let px = render("shadow", |scene| {
        let layer = Layer::new(scene);
        layer.set_background(Some(Color::WHITE));
        layer.set_masks_to_bounds(true);
        layer.set_corner_radius(4.0);
        layer.set_shadow(Some(Shadow::new(Color::BLACK, Vec2::new(0.0, 15.0), 0.5)));
        add(scene, &layer, Rect::new(30.0, 30.0, 20.0, 20.0));
    });
    assert_px(&px, 40, 40, [255, 255, 255, 255]);
    assert_px(&px, 40, 60, [0, 0, 0, 255]); // shadow below
    assert_px(&px, 40, 20, CLEAR);
}

#[test]
fn corner_radius_clips_with_masks_to_bounds() {
    let px = render("corners", |scene| {
        let layer = Layer::new(scene);
        layer.set_background(Some(red()));
        layer.set_corner_radius(20.0);
        layer.set_masked_corners(Corners::TOP_LEFT);
        layer.set_masks_to_bounds(true);
        add(scene, &layer, Rect::new(0.0, 0.0, 40.0, 40.0));
    });
    assert_px(&px, 1, 1, CLEAR); // rounded top-left
    assert_px(&px, 1, 38, RED); // square bottom-left
    assert_px(&px, 38, 1, RED); // square top-right
}

#[test]
fn masks_clip_by_alpha() {
    let px = render("mask", |scene| {
        let layer = Layer::new(scene);
        layer.set_background(Some(red()));
        let mask = Layer::new(scene);
        mask.set_background(Some(Color::WHITE));
        mask.set_frame(Rect::new(0.0, 0.0, 20.0, 40.0));
        layer.set_mask(Some(&mask));
        add(scene, &layer, Rect::new(0.0, 0.0, 40.0, 40.0));
    });
    assert_px(&px, 10, 20, RED);
    assert_px(&px, 30, 20, CLEAR);
}

#[test]
fn opacity_is_group_opacity() {
    let px = render("group_opacity", |scene| {
        let group = Layer::new(scene);
        group.set_opacity(0.5);
        add(scene, &group, Rect::new(0.0, 0.0, 40.0, 40.0));
        for x in [0.0, 10.0] {
            let square = Layer::new(scene);
            square.set_background(Some(red()));
            square.set_frame(Rect::new(x, 0.0, 20.0, 20.0));
            group.add_sublayer(&square);
        }
    });
    // Overlap is not darker than a single square: the group fades as a whole.
    assert_eq!(px.at(15, 10), px.at(5, 10));
    assert_px(&px, 15, 10, [128, 0, 0, 128]);
}

#[test]
fn updates_and_removals_reach_the_screen() {
    let scene = Scene::new(Size::new(W as f64, H as f64));
    let container = container();
    let _host = graphite_macos::attach_to_layer(&scene, &container);
    let layer = Layer::new(&scene);
    layer.set_background(Some(red()));
    add(&scene, &layer, Rect::new(0.0, 0.0, 20.0, 20.0));
    scene.commit();
    assert_px(&offscreen::render(&container, W, H).unwrap(), 5, 5, RED);

    layer.set_background(Some(blue()));
    layer.set_position(Point::new(60.0, 60.0));
    scene.commit();
    let px = offscreen::render(&container, W, H).unwrap();
    assert_px(&px, 5, 5, CLEAR);
    assert_px(&px, 55, 55, BLUE);

    layer.remove_from_superlayer();
    drop(layer);
    scene.commit();
    assert_px(&offscreen::render(&container, W, H).unwrap(), 55, 55, CLEAR);
}
