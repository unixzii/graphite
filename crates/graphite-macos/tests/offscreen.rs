//! The offscreen renderer itself, independent of graphite: read-back
//! orientation and scaling. Graphite semantics are covered by the
//! conformance suite (`tests/conformance.rs`).
#![cfg(target_os = "macos")]

use graphite_macos::offscreen;
use objc2::rc::Retained;
use objc2_core_foundation::{CGPoint, CGRect, CGSize};
use objc2_core_graphics::CGColor;
use objc2_quartz_core::CALayer;

fn container_with_red_square() -> Retained<CALayer> {
    let container = CALayer::new();
    container.setAnchorPoint(CGPoint::new(0.0, 0.0));
    container.setFrame(CGRect::new(CGPoint::new(0.0, 0.0), CGSize::new(100.0, 100.0)));
    let square = CALayer::new();
    // Unflipped Core Animation: y = 0 is the bottom of the screen.
    square.setFrame(CGRect::new(CGPoint::new(0.0, 0.0), CGSize::new(10.0, 10.0)));
    square.setBackgroundColor(Some(&CGColor::new_srgb(1.0, 0.0, 0.0, 1.0)));
    container.addSublayer(&square);
    container
}

#[test]
fn rows_are_top_down() {
    let px = offscreen::render(&container_with_red_square(), 100, 100).expect("a Metal device is required");
    assert_eq!(px.at(5, 95), [255, 0, 0, 255]);
    assert_eq!(px.at(5, 5), [0, 0, 0, 0]);
}

#[test]
fn scaled_rendering_covers_the_same_area() {
    let px = offscreen::render_scaled(&container_with_red_square(), 100.0, 100.0, 2.0).unwrap();
    assert_eq!((px.width, px.height), (200, 200));
    assert_eq!(px.at(19, 180), [255, 0, 0, 255]);
    assert_eq!(px.at(21, 180), [0, 0, 0, 0]);
    assert_eq!(px.at(19, 178), [0, 0, 0, 0]);
}
