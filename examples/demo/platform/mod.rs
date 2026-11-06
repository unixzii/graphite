//! Platform shells. Each provides:
//!
//! - `run(app) -> ExitCode`: open a window showing `app.scene()` at its size,
//!   call `app.tick()` about 60 times per second until it returns
//!   [`Control::Exit`](crate::Control::Exit) or the window is closed;
//! - `snapshot(scene) -> Option<Rgba>`: attach the scene to an offscreen
//!   host, commit, and read the pixels back.

#[cfg(target_os = "macos")]
mod macos;
#[cfg(target_os = "macos")]
pub use macos::{run, snapshot};

#[cfg(target_os = "linux")]
mod gtk;
#[cfg(target_os = "linux")]
pub use gtk::{run, snapshot};

#[cfg(not(any(target_os = "macos", target_os = "linux")))]
mod unsupported {
    use std::process::ExitCode;

    use graphite::Scene;

    use crate::{App, Rgba};

    pub fn run(_app: App) -> ExitCode {
        eprintln!("graphite has no backend for this platform yet");
        ExitCode::FAILURE
    }

    pub fn snapshot(_scene: &Scene) -> Option<Rgba> {
        None
    }
}
#[cfg(not(any(target_os = "macos", target_os = "linux")))]
pub use unsupported::{run, snapshot};
