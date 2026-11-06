//! A window showing graphite on the native backend of the current platform,
//! animated at 60 Hz. Two scenes:
//!
//! - `showcase` (default): everything graphite supports so far — geometry
//!   and nested transforms, clipping and scrolling, corners, borders,
//!   shadows, masks, opacity and z-order, images, shapes, gradients and
//!   immediate-mode drawing — with nearly the whole window animating.
//! - `ui`: a typical application window (header bar, lists, switches,
//!   sliders, a calendar, buttons, a text view) where only a few small
//!   regions animate; the counterpart for comparing redraw costs.
//!
//! ```sh
//! cargo run --example demo                                  # open the showcase
//! cargo run --example demo -- --scene ui                    # open the UI scene
//! cargo run --example demo -- --scene ui --fps              # …printing the frame rate
//! cargo run --example demo -- --selftest                    # animate 60 frames, verify commits, exit
//! cargo run --example demo -- --scene ui --snapshot ui.png  # render offscreen into a PNG, exit
//! ```
//!
//! Everything except `platform/` is platform independent: the scenes, the
//! animation and the self-test only use the graphite API.

mod platform;
mod scenes;

use std::path::PathBuf;
use std::process::ExitCode;
use std::time::Instant;

use graphite::Scene;

use crate::scenes::{DemoScene, SceneKind};

/// Ticks per second the shells aim for.
const FPS: f64 = 60.0;
/// The moment `--snapshot` renders, in seconds.
const SNAPSHOT_TIME: f64 = 2.4;
/// Frames animated by `--selftest`.
const SELFTEST_FRAMES: u32 = 60;
/// How often `--fps` reports.
const FPS_REPORT_SECONDS: f64 = 2.0;

/// What the platform shell should do after a tick.
pub enum Control {
    Continue,
    Exit(i32),
}

/// The demo's state, driven by the platform shell.
pub struct App {
    scene: Scene,
    content: Box<dyn DemoScene>,
    frame: u32,
    selftest: bool,
    /// Ticks at which the previous tick's changes were still uncommitted.
    unflushed: Vec<u32>,
    /// With `--fps`: the start of the current measuring window and the
    /// ticks counted in it.
    fps: Option<(Instant, u32)>,
}

impl App {
    fn new(kind: SceneKind, selftest: bool, fps: bool) -> App {
        let scene = Scene::new(kind.size());
        let mut content = kind.build(&scene);
        content.update(0.0);
        App { scene, content, frame: 0, selftest, unflushed: Vec::new(), fps: fps.then(|| (Instant::now(), 0)) }
    }

    pub fn scene(&self) -> &Scene {
        &self.scene
    }

    /// Advances the animation by one frame. The shell calls this about 60
    /// times per second and must commit the scene in between, at its
    /// platform's commit point (backends schedule that themselves).
    pub fn tick(&mut self) -> Control {
        self.frame += 1;
        // From the second tick on, the previous tick's changes must have
        // been committed. (The first tick can run before the platform's
        // first commit point, e.g. GTK ticks before its layout phase.)
        if self.frame > 1 && self.scene.needs_commit() {
            self.unflushed.push(self.frame);
        }
        self.content.update(self.frame as f64 / FPS);

        if let Some((since, ticks)) = &mut self.fps {
            *ticks += 1;
            let elapsed = since.elapsed().as_secs_f64();
            if elapsed >= FPS_REPORT_SECONDS {
                println!("fps: {:.1}", *ticks as f64 / elapsed);
                *since = Instant::now();
                *ticks = 0;
            }
        }

        if self.selftest && self.frame == SELFTEST_FRAMES {
            println!(
                "selftest: frames={} uncommitted={:?} backing_scale={}",
                self.frame,
                self.unflushed,
                self.scene.backing_scale()
            );
            return Control::Exit(if self.unflushed.is_empty() { 0 } else { 1 });
        }
        Control::Continue
    }
}

/// Premultiplied RGBA8 pixels, row 0 at the top.
pub struct Rgba {
    pub width: u32,
    pub height: u32,
    pub data: Vec<u8>,
}

fn write_png(path: &PathBuf, image: &Rgba) -> std::io::Result<()> {
    let file = std::io::BufWriter::new(std::fs::File::create(path)?);
    let mut encoder = png::Encoder::new(file, image.width, image.height);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.write_header()?.write_image_data(&image.data)?;
    Ok(())
}

struct Options {
    scene: SceneKind,
    selftest: bool,
    fps: bool,
    snapshot: Option<PathBuf>,
}

const USAGE: &str = "usage: demo [--scene showcase|ui] [--selftest | --fps | --snapshot <out.png>]";

fn parse_args() -> Result<Options, String> {
    let mut options = Options { scene: SceneKind::Showcase, selftest: false, fps: false, snapshot: None };
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--scene" => {
                let name = args.next().ok_or("--scene needs a name")?;
                options.scene = SceneKind::parse(&name).ok_or(format!("unknown scene `{name}`"))?;
            }
            "--selftest" => options.selftest = true,
            "--fps" => options.fps = true,
            "--snapshot" => options.snapshot = Some(args.next().ok_or("--snapshot needs a path")?.into()),
            other => return Err(format!("unknown argument `{other}`")),
        }
    }
    Ok(options)
}

fn main() -> ExitCode {
    let options = match parse_args() {
        Ok(options) => options,
        Err(message) => {
            eprintln!("{message}\n{USAGE}");
            return ExitCode::from(2);
        }
    };
    let Some(path) = options.snapshot else {
        return platform::run(App::new(options.scene, options.selftest, options.fps));
    };
    let mut app = App::new(options.scene, false, false);
    app.content.update(SNAPSHOT_TIME);
    let Some(image) = platform::snapshot(&app.scene) else {
        eprintln!("offscreen rendering is not available on this platform");
        return ExitCode::FAILURE;
    };
    match write_png(&path, &image) {
        Ok(()) => ExitCode::SUCCESS,
        Err(err) => {
            eprintln!("cannot write {}: {err}", path.display());
            ExitCode::FAILURE
        }
    }
}
