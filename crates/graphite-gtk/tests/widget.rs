//! `attach` in a real window: commit scheduling through the frame clock,
//! backing-scale tracking, clipping, and what the widget actually paints.
//! GTK needs the main thread, so this target has its own `main`
//! (`harness = false`), and a graphical session.

#[cfg(target_os = "linux")]
fn main() -> std::process::ExitCode {
    use std::time::{Duration, Instant};

    use graphite::*;
    use graphite_gtk::gtk::prelude::*;
    use graphite_gtk::gtk::{self, gdk, glib, graphene, gsk};

    fn spin_until(what: &str, mut done: impl FnMut() -> bool) {
        let deadline = Instant::now() + Duration::from_secs(5);
        let ctx = glib::MainContext::default();
        while !done() {
            assert!(Instant::now() < deadline, "timed out waiting for {what}");
            ctx.iteration(false);
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    /// Renders what `widget` paints, at 1 pixel per point.
    fn paint(widget: &gtk::Widget) -> (u32, Vec<u8>) {
        let (w, h) = (widget.width(), widget.height());
        let snapshot = gtk::Snapshot::new();
        gtk::WidgetPaintable::new(Some(widget)).snapshot(&snapshot, w as f64, h as f64);
        let node = snapshot.to_node().expect("the widget paints something");
        let renderer = gsk::CairoRenderer::new();
        renderer.realize_for_display(&gdk::Display::default().unwrap()).unwrap();
        let texture = renderer.render_texture(&node, Some(&graphene::Rect::new(0.0, 0.0, w as f32, h as f32)));
        renderer.unrealize();
        let mut downloader = gdk::TextureDownloader::new(&texture);
        downloader.set_format(gdk::MemoryFormat::R8g8b8a8Premultiplied);
        let (bytes, stride) = downloader.download_bytes();
        (stride as u32, bytes.to_vec())
    }

    if gtk::init().is_err() {
        eprintln!("GTK could not be initialized: run inside a graphical session");
        return std::process::ExitCode::FAILURE;
    }

    let scene = Scene::new(Size::new(120.0, 80.0));
    let top_left = Layer::new(&scene);
    top_left.set_frame(Rect::new(0.0, 0.0, 10.0, 10.0));
    top_left.set_background(Some(Color::rgb(1.0, 0.0, 0.0)));
    scene.root().add_sublayer(&top_left);

    let host = graphite_gtk::attach(&scene);
    let widget = host.widget().unwrap().clone();
    assert_eq!(widget.overflow(), gtk::Overflow::Hidden, "the scene is clipped to the widget");

    let window = gtk::Window::new();
    window.set_child(Some(&widget));
    window.present();

    // The frame clock commits without any explicit Scene::commit.
    spin_until("the first commit", || !scene.needs_commit() && widget.width() > 0);
    let (_, natural) = widget.preferred_size();
    assert_eq!((natural.width(), natural.height()), (120, 80), "natural size is the root layer's");
    let surface_scale = widget.native().unwrap().surface().unwrap().scale();
    assert_eq!(scene.backing_scale(), surface_scale, "the scene follows the surface scale");

    let (stride, pixels) = paint(&widget);
    let at = |x: u32, y: u32| &pixels[(y * stride + x * 4) as usize..][..4];
    assert_eq!(at(5, 5), [255, 0, 0, 255], "top-left layer painted at the top-left");
    assert_eq!(at(5, 50)[3], 0, "nothing below it");

    // Later changes are committed by the frame clock too, even once it has
    // gone idle (no frames are produced unless the backend asks for one).
    let settle = Instant::now() + Duration::from_millis(1500);
    spin_until("the window to settle", || Instant::now() > settle);
    top_left.set_position(Point::new(65.0, 45.0));
    spin_until("the second commit", || !scene.needs_commit());
    let (stride, pixels) = paint(&widget);
    let at = |x: u32, y: u32| &pixels[(y * stride + x * 4) as usize..][..4];
    assert_eq!(at(65, 45), [255, 0, 0, 255], "the moved layer");
    assert_eq!(at(5, 5)[3], 0, "its old position");

    window.close();
    println!("widget: ok (surface scale {surface_scale})");
    std::process::ExitCode::SUCCESS
}

#[cfg(not(target_os = "linux"))]
fn main() {}
