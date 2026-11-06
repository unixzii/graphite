//! The backend-neutral conformance suite, rendered with Core Animation.
#![cfg(target_os = "macos")]

use graphite::Scene;
use graphite_conformance::{Capture, Harness, Pixels};
use objc2::rc::Retained;
use objc2_core_foundation::{CGPoint, CGRect, CGSize};
use objc2_quartz_core::CALayer;

struct CoreAnimation;

struct OffscreenCapture {
    container: Retained<CALayer>,
    _host: graphite_macos::SceneHost,
    size: (f64, f64),
    scale: f64,
}

impl Harness for CoreAnimation {
    fn name(&self) -> &str {
        "macos"
    }

    fn attach(&self, scene: &Scene, scale: f64) -> Box<dyn Capture> {
        let size = scene.root().bounds().size;
        let container = CALayer::new();
        container.setAnchorPoint(CGPoint::new(0.0, 0.0));
        container.setFrame(CGRect::new(CGPoint::new(0.0, 0.0), CGSize::new(size.width, size.height)));
        container.setContentsScale(scale);
        let host = graphite_macos::attach_to_layer(scene, &container);
        Box::new(OffscreenCapture { container, _host: host, size: (size.width, size.height), scale })
    }
}

impl Capture for OffscreenCapture {
    fn capture(&mut self) -> Pixels {
        let px = graphite_macos::offscreen::render_scaled(&self.container, self.size.0, self.size.1, self.scale)
            .expect("a Metal device is required");
        Pixels { width: px.width as u32, height: px.height as u32, data: px.data }
    }
}

graphite_conformance::conformance_tests!(CoreAnimation);
