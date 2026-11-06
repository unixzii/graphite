//! `attach_to_view` in a real window. AppKit owns a hosting view's root
//! layer (and resets its `geometryFlipped`), which offscreen tests built on
//! `attach_to_layer` cannot catch. Runs on the main thread: `harness = false`.

#[cfg(target_os = "macos")]
fn main() {
    use graphite::*;
    use objc2::rc::Retained;
    use objc2::{MainThreadMarker, MainThreadOnly};
    use objc2_app_kit::{NSApplication, NSBackingStoreType, NSWindow, NSWindowStyleMask};
    use objc2_foundation::{NSDate, NSPoint, NSRect, NSRunLoop, NSSize};
    use objc2_quartz_core::CALayer;

    fn spin() {
        NSRunLoop::currentRunLoop().runUntilDate(&NSDate::dateWithTimeIntervalSinceNow(0.2));
    }

    fn sublayer(layer: &CALayer, i: usize) -> Retained<CALayer> {
        // SAFETY: reading the sublayers array.
        unsafe { layer.sublayers() }.expect("has sublayers").objectAtIndex(i)
    }

    let mtm = MainThreadMarker::new().expect("runs on the main thread");
    let _app = NSApplication::sharedApplication(mtm);
    let rect = NSRect::new(NSPoint::new(100.0, 100.0), NSSize::new(200.0, 100.0));
    // SAFETY: standard NSWindow initialization.
    let window = unsafe {
        NSWindow::initWithContentRect_styleMask_backing_defer(
            NSWindow::alloc(mtm),
            rect,
            NSWindowStyleMask::Titled | NSWindowStyleMask::Resizable,
            NSBackingStoreType::Buffered,
            false,
        )
    };
    // SAFETY: we keep a strong reference to the window.
    unsafe { window.setReleasedWhenClosed(false) };

    let scene = Scene::new(Size::new(200.0, 100.0));
    let top_left = Layer::new(&scene);
    top_left.set_frame(Rect::new(0.0, 0.0, 10.0, 10.0));
    scene.root().add_sublayer(&top_left);
    let content = window.contentView().unwrap();
    let scene_host = graphite_macos::attach_to_view(&scene, &content, mtm);
    window.orderFrontRegardless();
    scene.commit();
    spin();

    let flipped = scene_host.layer();
    let host = flipped.superlayer().expect("the host layer is a sublayer of the view's layer");
    assert!(flipped.isGeometryFlipped(), "the host layer must stay flipped");
    assert!(!host.isGeometryFlipped(), "AppKit keeps the view's own layer unflipped");

    // host layer → root outer → root body → top_left's outer
    let outer = sublayer(&sublayer(&sublayer(flipped, 0), 0), 0);
    let in_host = outer.convertRect_toLayer(outer.bounds(), Some(&host));
    let host_height = host.bounds().size.height;
    // The host is y-up, so "top-left" means y = height - 10.
    assert_eq!((in_host.origin.x, in_host.origin.y), (0.0, host_height - 10.0), "top-left layer is at {in_host:?}");

    // The host layer follows the view when the window resizes.
    window.setContentSize(NSSize::new(300.0, 160.0));
    spin();
    assert_eq!(flipped.bounds().size, host.bounds().size);
    assert_eq!(host.bounds().size.height, 160.0);
    assert_eq!(scene.backing_scale(), window.backingScaleFactor());

    window.close();
    println!("view: ok");
}

#[cfg(not(target_os = "macos"))]
fn main() {}
