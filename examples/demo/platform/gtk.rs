//! Linux shell: a `GtkWindow` hosting the scene through graphite-gtk, and a
//! frame-clock tick callback driving the animation (graphite-gtk commits in
//! the layout phase that follows).

use std::cell::RefCell;
use std::process::ExitCode;

use graphite::Scene;
use graphite_gtk::gtk::prelude::*;
use graphite_gtk::gtk::{self, glib};
use graphite_gtk::offscreen::{self, RendererKind};

use crate::{App, Control, Rgba};

fn init() -> bool {
    if gtk::init().is_err() {
        eprintln!("GTK could not be initialized: run inside a graphical session");
        return false;
    }
    true
}

pub fn run(app: App) -> ExitCode {
    if !init() {
        return ExitCode::FAILURE;
    }
    let size = app.scene().root().bounds().size;
    let window = gtk::Window::new();
    window.set_title(Some("graphite demo · GTK"));
    window.set_default_size(size.width as i32, size.height as i32);
    let host = graphite_gtk::attach(app.scene());
    let widget = host.widget().expect("attach creates a widget").clone();
    window.set_child(Some(&widget));
    window.connect_close_request(|_| std::process::exit(0));

    let app = RefCell::new(app);
    widget.add_tick_callback(move |_, _| match app.borrow_mut().tick() {
        Control::Exit(code) => std::process::exit(code),
        Control::Continue => glib::ControlFlow::Continue,
    });
    window.present();
    glib::MainLoop::new(None, false).run();
    ExitCode::SUCCESS
}

pub fn snapshot(scene: &Scene) -> Option<Rgba> {
    if !init() {
        return None;
    }
    let host = graphite_gtk::attach_offscreen(scene);
    scene.set_backing_scale(1.0);
    scene.commit();
    // Vulkan is GTK's default on Wayland; fall back to GL.
    let px = offscreen::render(&host, RendererKind::Vulkan, 1.0)
        .or_else(|_| offscreen::render(&host, RendererKind::Gl, 1.0))
        .ok()?;
    Some(Rgba { width: px.width, height: px.height, data: px.data })
}
