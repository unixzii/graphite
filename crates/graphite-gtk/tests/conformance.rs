//! The backend-neutral conformance suite, rendered with GSK.
//!
//! GTK objects are bound to the main thread, so this target runs the cases
//! sequentially from its own `main` (`harness = false`). It needs a display:
//! run it inside a graphical session (or set `WAYLAND_DISPLAY` / `DISPLAY`).
//!
//! `GRAPHITE_GSK_RENDERERS` selects renderers (comma separated, from `gl`,
//! `vulkan`, `cairo`); the default is `gl,vulkan`.

#[cfg(target_os = "linux")]
fn main() -> std::process::ExitCode {
    use graphite::Scene;
    use graphite_conformance::{Capture, Harness, Pixels};
    use std::rc::Rc;

    use graphite_gtk::offscreen::{Renderer, RendererKind};

    /// One realized renderer per harness, shared by every capture.
    struct Gsk {
        renderer: Rc<Renderer>,
        name: String,
    }

    struct OffscreenCapture {
        host: graphite_gtk::SceneHost,
        renderer: Rc<Renderer>,
        scale: f64,
    }

    impl Harness for Gsk {
        fn name(&self) -> &str {
            &self.name
        }

        fn attach(&self, scene: &Scene, scale: f64) -> Box<dyn Capture> {
            let host = graphite_gtk::attach_offscreen(scene);
            scene.set_backing_scale(scale);
            Box::new(OffscreenCapture { host, renderer: self.renderer.clone(), scale })
        }
    }

    impl Capture for OffscreenCapture {
        fn capture(&mut self) -> Pixels {
            let px = self.renderer.render(&self.host, self.scale);
            Pixels { width: px.width, height: px.height, data: px.data }
        }
    }

    if graphite_gtk::gtk::init().is_err() {
        eprintln!("GTK could not be initialized: run inside a graphical session (WAYLAND_DISPLAY / DISPLAY)");
        return std::process::ExitCode::FAILURE;
    }
    let selection = std::env::var("GRAPHITE_GSK_RENDERERS").unwrap_or_else(|_| "gl,vulkan".into());
    let harnesses: Vec<Gsk> = selection
        .split(',')
        .map(|name| {
            let kind = match name.trim() {
                "gl" => RendererKind::Gl,
                "vulkan" => RendererKind::Vulkan,
                "cairo" => RendererKind::Cairo,
                other => panic!("unknown GSK renderer `{other}`"),
            };
            let renderer = Renderer::new(kind).unwrap_or_else(|err| panic!("cannot realize {kind:?}: {err}"));
            Gsk { renderer: Rc::new(renderer), name: format!("gtk-{}", name.trim()) }
        })
        .collect();
    let refs: Vec<&dyn Harness> = harnesses.iter().map(|h| h as &dyn Harness).collect();
    graphite_conformance::run_main(&refs)
}

#[cfg(not(target_os = "linux"))]
fn main() {}
