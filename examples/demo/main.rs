//! A window showing the main layer features, animated at 60 Hz, on the
//! native backend of the current platform.
//!
//! ```sh
//! cargo run --example demo                        # open a window
//! cargo run --example demo -- --selftest          # animate 60 frames, verify commits, exit
//! cargo run --example demo -- --snapshot out.png  # render offscreen into a PNG, exit
//! ```
//!
//! Everything except `platform/` is platform independent: the scene, the
//! animation and the self-test only use the graphite API.

mod platform;
mod scene;

use std::path::PathBuf;
use std::process::ExitCode;

use graphite::Scene;

use crate::scene::Demo;

/// Frames per animation cycle.
const CYCLE: u32 = 120;
/// Frames animated by `--selftest`.
const SELFTEST_FRAMES: u32 = 60;

/// What the platform shell should do after a tick.
pub enum Control {
    Continue,
    Exit(i32),
}

/// The demo's state, driven by the platform shell.
pub struct App {
    scene: Scene,
    demo: Demo,
    frame: u32,
    selftest: bool,
    /// Ticks at which the previous tick's changes were still uncommitted.
    unflushed: u32,
}

impl App {
    fn new(selftest: bool) -> App {
        let scene = Scene::new(scene::SIZE);
        let demo = Demo::build(&scene);
        App { scene, demo, frame: 0, selftest, unflushed: 0 }
    }

    pub fn scene(&self) -> &Scene {
        &self.scene
    }

    /// Advances the animation by one frame. The shell calls this about 60
    /// times per second and must commit the scene in between, at its
    /// platform's commit point (backends schedule that themselves).
    pub fn tick(&mut self) -> Control {
        self.frame += 1;
        if self.scene.needs_commit() {
            self.unflushed += 1;
        }
        self.demo.pose((self.frame % CYCLE) as f64 / CYCLE as f64);

        if self.selftest && self.frame == SELFTEST_FRAMES {
            println!(
                "selftest: frames={} uncommitted={} backing_scale={}",
                self.frame,
                self.unflushed,
                self.scene.backing_scale()
            );
            return Control::Exit(if self.unflushed == 0 { 0 } else { 1 });
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

enum Command {
    Window { selftest: bool },
    Snapshot(PathBuf),
}

fn parse_args() -> Result<Command, String> {
    let mut args = std::env::args().skip(1);
    match args.next().as_deref() {
        None => Ok(Command::Window { selftest: false }),
        Some("--selftest") => Ok(Command::Window { selftest: true }),
        Some("--snapshot") => args.next().map(|p| Command::Snapshot(p.into())).ok_or("--snapshot needs a path".into()),
        Some(other) => Err(format!("unknown argument `{other}`")),
    }
}

fn main() -> ExitCode {
    match parse_args() {
        Err(message) => {
            eprintln!("{message}\nusage: demo [--selftest | --snapshot <out.png>]");
            ExitCode::from(2)
        }
        Ok(Command::Window { selftest }) => platform::run(App::new(selftest)),
        Ok(Command::Snapshot(path)) => {
            let app = App::new(false);
            app.demo.pose(0.7);
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
    }
}
