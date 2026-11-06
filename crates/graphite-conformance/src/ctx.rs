//! The per-case, per-scale context: renders scenes and records failures.

use std::path::PathBuf;

use graphite::{Point, Scene, Size};

use crate::frame::{Frame, Rgba8};
use crate::{Capture, Harness};

/// Default scene size used by the cases.
pub const SIZE: Size = Size::new(100.0, 100.0);

/// Where renders of `backend` are written: `$GRAPHITE_CONFORMANCE_OUT` or
/// `<workspace>/target/conformance`, plus the backend name.
pub(crate) fn output_dir(backend: &str) -> PathBuf {
    let base = std::env::var_os("GRAPHITE_CONFORMANCE_OUT")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../target/conformance"));
    base.join(backend)
}

/// Handed to every case.
pub struct Ctx<'a> {
    harness: &'a dyn Harness,
    case: &'static str,
    scale: f64,
    hosts: Vec<(Scene, Box<dyn Capture>)>,
    failures: Vec<String>,
    renders: usize,
}

impl<'a> Ctx<'a> {
    pub(crate) fn new(harness: &'a dyn Harness, case: &'static str, scale: f64) -> Ctx<'a> {
        Ctx { harness, case, scale, hosts: Vec::new(), failures: Vec::new(), renders: 0 }
    }

    pub(crate) fn into_failures(self) -> Vec<String> {
        self.failures
    }

    pub fn scale(&self) -> f64 {
        self.scale
    }

    /// A new scene of the default 100 × 100 size.
    pub fn scene(&self) -> Scene {
        Scene::new(SIZE)
    }

    /// Commits `scene` and captures it (attaching it first if needed). The
    /// render is also saved as a PNG.
    pub fn render(&mut self, scene: &Scene) -> Frame {
        if !self.hosts.iter().any(|(s, _)| s.ptr_eq(scene)) {
            let capture = self.harness.attach(scene, self.scale);
            self.hosts.push((scene.clone(), capture));
        }
        scene.commit();
        let capture = &mut self.hosts.iter_mut().find(|(s, _)| s.ptr_eq(scene)).unwrap().1;
        let pixels = capture.capture();
        self.save(&pixels);
        Frame { pixels, scale: self.scale }
    }

    fn save(&mut self, pixels: &crate::Pixels) {
        let suffix = if self.renders == 0 { String::new() } else { format!("-{}", self.renders) };
        self.renders += 1;
        let dir = output_dir(self.harness.name());
        let path = dir.join(format!("{}@{}x{suffix}.png", self.case, self.scale));
        let write = || -> std::io::Result<()> {
            std::fs::create_dir_all(&dir)?;
            let file = std::io::BufWriter::new(std::fs::File::create(&path)?);
            let mut encoder = png::Encoder::new(file, pixels.width, pixels.height);
            encoder.set_color(png::ColorType::Rgba);
            encoder.set_depth(png::BitDepth::Eight);
            encoder.write_header()?.write_image_data(&pixels.data)?;
            Ok(())
        };
        if let Err(err) = write() {
            eprintln!("conformance: cannot save {}: {err}", path.display());
        }
    }

    /// Records a failure unless `ok`.
    pub fn check(&mut self, ok: bool, message: impl FnOnce() -> String) {
        if !ok {
            self.failures.push(message());
        }
    }

    /// Expects the pixel at `p` (in points) to be `expected`, per channel
    /// within `tolerance`.
    pub fn expect_color_within(&mut self, frame: &Frame, label: &str, p: Point, expected: Rgba8, tolerance: u8) {
        match frame.at(p) {
            None => self.failures.push(format!("{label}: probe ({}, {}) is outside the frame", p.x, p.y)),
            Some(got) => {
                let ok = got.iter().zip(expected).all(|(a, b)| a.abs_diff(b) <= tolerance);
                self.check(ok, || format!("{label}: pixel at ({}, {}) is {got:?}, expected {expected:?}", p.x, p.y));
            }
        }
    }

    /// [`expect_color_within`](Self::expect_color_within) with the default
    /// tolerance for flat colors.
    pub fn expect_color(&mut self, frame: &Frame, label: &str, p: Point, expected: Rgba8) {
        self.expect_color_within(frame, label, p, expected, 8);
    }

    /// Expects a measured value within `tolerance` of `expected`.
    pub fn expect_near(&mut self, label: &str, measured: Option<f64>, expected: f64, tolerance: f64) {
        match measured {
            None => self.failures.push(format!("{label}: could not be measured (expected {expected:.3})")),
            Some(m) => {
                // Visible with `--nocapture`; useful when calibrating a backend.
                eprintln!(
                    "[{}] {}@{}x {label}: {m:.3} (expected {expected:.3} ± {tolerance:.3})",
                    self.harness.name(),
                    self.case,
                    self.scale
                );
                self.check((m - expected).abs() <= tolerance, || {
                    format!("{label}: measured {m:.3}, expected {expected:.3} ± {tolerance:.3}")
                })
            }
        }
    }
}
