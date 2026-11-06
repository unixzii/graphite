//! Backend-neutral conformance tests for graphite backends.
//!
//! Backends render the same scenes offscreen; the cases then check what
//! must agree across platforms, in two tiers:
//!
//! - **Semantic** ([`Tier::Semantic`]): position, orientation, clipping,
//!   z-order, masks, opacity grouping… probed at points well inside or
//!   outside flat-colored areas, never on anti-aliased edges. Every backend
//!   must pass these exactly.
//! - **Measured** ([`Tier::Measured`]): effects that legitimately differ in
//!   detail between rasterizers (blur, gradients, stroke coverage, arcs) are
//!   reduced to a number (a blur's σ, a stroke's width, …) and compared to
//!   the value the API promises, with a semantic tolerance.
//!
//! What cannot be compared across platforms (text appearance, anti-aliasing
//! style) is out of scope here; every render is saved as a PNG under
//! `target/conformance/<backend>/` for same-backend snapshots and side-by-side
//! review.
//!
//! # Running the suite for a backend
//!
//! Implement [`Harness`] and expand [`conformance_tests!`] in a test target:
//!
//! ```ignore
//! graphite_conformance::conformance_tests!(my_backend::ConformanceHarness);
//! ```
//!
//! Backends bound to the main thread use [`run_main`] from a test target
//! with `harness = false` instead.
//!
//! Every case runs at 1×, 1.5× and 2× backing scale.

mod cases;
mod ctx;
mod frame;

use graphite::Scene;

pub use ctx::Ctx;
pub use frame::{Frame, Pixels, Rgba8};

/// Connects a backend to the suite.
pub trait Harness {
    /// Short backend name, used in reports and output paths (`"macos"`).
    fn name(&self) -> &str;

    /// Presents `scene` on an offscreen host rasterizing at `scale` device
    /// pixels per point (the backend should also call
    /// [`Scene::set_backing_scale`]). Called once per scene, before its
    /// first commit.
    fn attach(&self, scene: &Scene, scale: f64) -> Box<dyn Capture>;
}

/// Reads back what a host shows.
pub trait Capture {
    /// Renders the host's current, committed state: the root layer's bounds
    /// at the attach scale, as premultiplied RGBA8 with row 0 at the top.
    fn capture(&mut self) -> Pixels;
}

/// How a case judges a backend.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tier {
    /// Exact semantics, probed away from edges.
    Semantic,
    /// A measured quantity within a tolerance.
    Measured,
}

/// One conformance case.
pub struct Case {
    pub name: &'static str,
    pub tier: Tier,
    pub run: fn(&mut Ctx),
}

/// Backing scales every case runs at, including a fractional one.
pub const SCALES: [f64; 3] = [1.0, 1.5, 2.0];

/// All cases.
pub fn cases() -> &'static [Case] {
    cases::ALL
}

/// Runs one case at every scale in [`SCALES`]; returns the failures.
pub fn run(harness: &dyn Harness, case: &Case) -> Vec<String> {
    let mut failures = Vec::new();
    for scale in SCALES {
        let mut ctx = Ctx::new(harness, case.name, scale);
        (case.run)(&mut ctx);
        failures.extend(ctx.into_failures().into_iter().map(|f| format!("@{scale}x: {f}")));
    }
    failures
}

/// Runs the case called `name`, panicking with a report if it fails.
#[track_caller]
pub fn run_named(harness: &dyn Harness, name: &str) {
    let case = cases().iter().find(|c| c.name == name).unwrap_or_else(|| panic!("no conformance case `{name}`"));
    let failures = run(harness, case);
    if !failures.is_empty() {
        panic!(
            "[{}] {} ({:?}) failed:\n  - {}\n(renders in {})",
            harness.name(),
            case.name,
            case.tier,
            failures.join("\n  - "),
            ctx::output_dir(harness.name()).display()
        );
    }
}

/// Runs every case with every harness, sequentially on the calling thread,
/// and reports like libtest. For backends whose objects are bound to the
/// main thread (GTK): use it from a test target with `harness = false`.
///
/// Positional command-line arguments filter cases by substring (matched
/// against `<harness>::<case>`); flags such as `--nocapture` are ignored.
pub fn run_main(harnesses: &[&dyn Harness]) -> std::process::ExitCode {
    let filters: Vec<String> = std::env::args().skip(1).filter(|a| !a.starts_with('-')).collect();
    let selected: Vec<(&dyn Harness, &Case)> = harnesses
        .iter()
        .flat_map(|h| cases().iter().map(move |c| (*h, c)))
        .filter(|(h, c)| {
            let full = format!("{}::{}", h.name(), c.name);
            filters.is_empty() || filters.iter().any(|f| full.contains(f.as_str()))
        })
        .collect();
    println!("\nrunning {} conformance cases", selected.len());
    let mut failed = Vec::new();
    for (harness, case) in &selected {
        let full = format!("{}::{}", harness.name(), case.name);
        let failures = run(*harness, case);
        if failures.is_empty() {
            println!("test {full} ... ok");
        } else {
            println!("test {full} ... FAILED");
            failed.push((full, failures));
        }
    }
    for (full, failures) in &failed {
        println!("\n---- {full} ----\n  - {}", failures.join("\n  - "));
    }
    let passed = selected.len() - failed.len();
    let verdict = if failed.is_empty() { "ok" } else { "FAILED" };
    println!("\ntest result: {verdict}. {passed} passed; {} failed\n", failed.len());
    if failed.is_empty() { std::process::ExitCode::SUCCESS } else { std::process::ExitCode::FAILURE }
}

/// Expands to one `#[test]` per conformance case, each running the case
/// with the harness `$harness` evaluates to.
#[macro_export]
macro_rules! conformance_tests {
    ($harness:expr) => {
        $crate::__with_case_names!(__conformance_emit_tests; $harness);
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! __conformance_emit_tests {
    ($harness:expr; $($module:ident $name:ident)*) => {
        $(
            #[test]
            fn $name() {
                $crate::run_named(&$harness, stringify!($name));
            }
        )*
    };
}
