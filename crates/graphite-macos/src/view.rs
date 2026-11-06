//! The layer-hosting view used by [`crate::attach_to_view`].

use graphite::WeakScene;
use objc2::rc::Retained;
use objc2::{DefinedClass, MainThreadMarker, MainThreadOnly, define_class, msg_send};
use objc2_app_kit::{NSResponder, NSView};
use objc2_foundation::{NSObject, NSRect};
use objc2_quartz_core::CALayer;

pub(crate) struct Ivars {
    scene: WeakScene,
    /// The flipped host layer (a sublayer of the view's own layer).
    host_layer: Retained<CALayer>,
}

define_class!(
    // SAFETY:
    // - NSView has no subclassing requirements we violate.
    // - `GraphiteView` does not implement `Drop`.
    #[unsafe(super(NSView, NSResponder, NSObject))]
    #[thread_kind = MainThreadOnly]
    #[ivars = Ivars]
    pub(crate) struct GraphiteView;

    impl GraphiteView {
        #[unsafe(method(viewDidChangeBackingProperties))]
        fn view_did_change_backing_properties(&self) {
            // SAFETY: calling the superclass implementation is always valid.
            let _: () = unsafe { msg_send![super(self), viewDidChangeBackingProperties] };
            sync_backing_scale(self);
        }

        #[unsafe(method(viewDidMoveToWindow))]
        fn view_did_move_to_window(&self) {
            // SAFETY: calling the superclass implementation is always valid.
            let _: () = unsafe { msg_send![super(self), viewDidMoveToWindow] };
            sync_backing_scale(self);
        }
    }
);

impl GraphiteView {
    pub(crate) fn new(
        mtm: MainThreadMarker,
        scene: WeakScene,
        host_layer: Retained<CALayer>,
        frame: NSRect,
    ) -> Retained<GraphiteView> {
        let this = GraphiteView::alloc(mtm).set_ivars(Ivars { scene, host_layer });
        // SAFETY: `initWithFrame:` is NSView's designated initializer.
        unsafe { msg_send![super(this), initWithFrame: frame] }
    }
}

/// Pushes the window's backing scale to the scene and the host layer.
pub(crate) fn sync_backing_scale(view: &GraphiteView) {
    let Some(window) = view.window() else { return };
    let scale = window.backingScaleFactor();
    if let Some(layer) = view.layer() {
        layer.setContentsScale(scale);
    }
    view.ivars().host_layer.setContentsScale(scale);
    if let Some(scene) = view.ivars().scene.upgrade() {
        scene.set_backing_scale(scale);
    }
}
