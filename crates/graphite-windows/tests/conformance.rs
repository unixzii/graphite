//! The backend-neutral conformance suite, rendered with Windows.UI.Composition.
//!
//! Composition objects belong to the thread that created them, so this
//! target runs the cases sequentially from its own `main` (`harness =
//! false`). Rendering happens in the system compositor: run it inside an
//! interactive desktop session (not from an SSH logon, which has no desktop).

#[cfg(windows)]
fn main() -> std::process::ExitCode {
    use graphite::Scene;
    use graphite_conformance::{Capture, Harness, Pixels};
    use graphite_windows::offscreen::{self, OffscreenHost};

    struct WinComp;

    struct OffscreenCapture(OffscreenHost);

    impl Harness for WinComp {
        fn name(&self) -> &str {
            "windows"
        }

        fn attach(&self, scene: &Scene, scale: f64) -> Box<dyn Capture> {
            let host = offscreen::attach(scene, scale).expect("cannot create an offscreen host");
            Box::new(OffscreenCapture(host))
        }
    }

    impl Capture for OffscreenCapture {
        fn capture(&mut self) -> Pixels {
            let px = self.0.capture().expect("cannot read back the offscreen window");
            Pixels { width: px.width, height: px.height, data: px.data }
        }
    }

    graphite_conformance::run_main(&[&WinComp])
}

#[cfg(not(windows))]
fn main() {}
