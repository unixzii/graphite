//! The `CALayerDelegate` that draws a graphite layer's drawer.
//!
//! A drawn layer's body gets one of these as its delegate, so Core Animation
//! owns the backing store and decides when to display: after
//! `setNeedsDisplay`, when the contents scale changes, or whenever it has
//! dropped the backing store. Display runs the drawer through
//! [`Drawers`] and replays the recording straight into the context Core
//! Animation hands us; nothing else is kept.

use std::thread::{self, ThreadId};

use graphite::{DrawInfo, Drawers, LayerId};
use objc2::rc::Retained;
use objc2::runtime::{NSObject, NSObjectProtocol};
use objc2::{AnyThread, DefinedClass, define_class, msg_send};
use objc2_core_graphics::CGContext;
use objc2_quartz_core::{CALayer, CALayerDelegate};

use crate::convert::graphite_rect;
use crate::draw;

pub(crate) struct Ivars {
    drawers: Drawers,
    id: LayerId,
    /// The scene's thread. Scenes are single-threaded, and layers need not
    /// live on the main thread (offscreen rendering), so the class is not
    /// `MainThreadOnly`; displays requested from any other thread are
    /// skipped instead.
    thread: ThreadId,
}

define_class!(
    // SAFETY:
    // - NSObject has no subclassing requirements.
    // - `DrawingDelegate` does not implement `Drop`.
    #[unsafe(super(NSObject))]
    #[name = "GraphiteDrawingDelegate"]
    #[ivars = Ivars]
    pub(crate) struct DrawingDelegate;

    unsafe impl NSObjectProtocol for DrawingDelegate {}

    unsafe impl CALayerDelegate for DrawingDelegate {
        #[unsafe(method(drawLayer:inContext:))]
        fn draw_layer_in_context(&self, layer: &CALayer, cx: &CGContext) {
            if thread::current().id() != self.ivars().thread {
                return;
            }
            let bounds = graphite_rect(layer.bounds());
            let info = DrawInfo { bounds, scale: layer.contentsScale() };
            let Some(list) = self.ivars().drawers.draw(self.ivars().id, &info) else { return };
            let c = Some(cx);
            CGContext::save_g_state(c);
            // Core Animation sets the CTM up for the layer's bounds; it is
            // y-down only when the layer's contents are flipped (always the
            // case under graphite's flipped host layer, but don't rely on it).
            if CGContext::ctm(c).d > 0.0 {
                CGContext::translate_ctm(c, 0.0, bounds.min_y() + bounds.max_y());
                CGContext::scale_ctm(c, 1.0, -1.0);
            }
            draw::replay(cx, &list);
            CGContext::restore_g_state(c);
        }
    }
);

impl DrawingDelegate {
    /// Must be called on the scene's thread.
    pub(crate) fn new(drawers: Drawers, id: LayerId) -> Retained<DrawingDelegate> {
        let this = DrawingDelegate::alloc().set_ivars(Ivars { drawers, id, thread: thread::current().id() });
        // SAFETY: `init` is NSObject's designated initializer.
        unsafe { msg_send![super(this), init] }
    }
}
