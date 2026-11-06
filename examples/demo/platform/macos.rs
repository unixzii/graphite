//! macOS shell: an `NSWindow` hosting the scene through graphite-macos, and
//! an `NSTimer` driving the animation.

use std::cell::RefCell;
use std::process::ExitCode;

use block2::RcBlock;
use graphite::Scene;
use objc2::rc::Retained;
use objc2::{MainThreadMarker, MainThreadOnly};
use objc2_app_kit::{NSApplication, NSApplicationActivationPolicy, NSBackingStoreType, NSWindow, NSWindowStyleMask};
use objc2_foundation::{NSPoint, NSRect, NSSize, NSTimer, ns_string};
use objc2_quartz_core::CALayer;

use crate::{App, Control, Rgba};

pub fn run(app: App) -> ExitCode {
    let mtm = MainThreadMarker::new().expect("must run on the main thread");
    let ns_app = NSApplication::sharedApplication(mtm);
    ns_app.setActivationPolicy(NSApplicationActivationPolicy::Regular);

    let size = app.scene().root().bounds().size;
    let rect = NSRect::new(NSPoint::new(0.0, 0.0), NSSize::new(size.width, size.height));
    let style = NSWindowStyleMask::Titled | NSWindowStyleMask::Closable | NSWindowStyleMask::Resizable;
    // SAFETY: standard NSWindow initialization.
    let window: Retained<NSWindow> = unsafe {
        NSWindow::initWithContentRect_styleMask_backing_defer(
            NSWindow::alloc(mtm),
            rect,
            style,
            NSBackingStoreType::Buffered,
            false,
        )
    };
    // SAFETY: we keep a strong reference to the window.
    unsafe { window.setReleasedWhenClosed(false) };
    window.setTitle(ns_string!("graphite demo · Core Animation"));

    let content = window.contentView().expect("a window has a content view");
    let _host = graphite_macos::attach_to_view(app.scene(), &content, mtm);
    window.center();
    window.makeKeyAndOrderFront(None);

    let app = RefCell::new(app);
    let window_ref = window.clone();
    let tick = RcBlock::new(move |_timer| {
        if let Control::Exit(code) = app.borrow_mut().tick() {
            std::process::exit(code);
        }
        if !window_ref.isVisible() {
            std::process::exit(0);
        }
    });
    // SAFETY: the block outlives the timer (both live until the process exits).
    let _timer = unsafe { NSTimer::scheduledTimerWithTimeInterval_repeats_block(1.0 / 60.0, true, &tick) };

    ns_app.activate();
    ns_app.run();
    ExitCode::SUCCESS
}

pub fn snapshot(scene: &Scene) -> Option<Rgba> {
    let size = scene.root().bounds().size;
    let container = CALayer::new();
    container.setAnchorPoint(NSPoint::new(0.0, 0.0));
    container.setFrame(NSRect::new(NSPoint::new(0.0, 0.0), NSSize::new(size.width, size.height)));
    let _host = graphite_macos::attach_to_layer(scene, &container);
    scene.commit();
    let px = graphite_macos::offscreen::render(&container, size.width as usize, size.height as usize)?;
    Some(Rgba { width: px.width as u32, height: px.height as u32, data: px.data })
}
