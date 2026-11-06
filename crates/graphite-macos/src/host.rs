//! [`SceneHost`]: bridges a [`Scene`]'s layer tree into an existing
//! Core Animation / AppKit hierarchy.

use graphite::Scene;
use objc2::MainThreadMarker;
use objc2::rc::Retained;
use objc2_app_kit::{NSAutoresizingMaskOptions, NSView};
use objc2_core_foundation::CGPoint;
use objc2_quartz_core::{CAAutoresizingMask, CALayer};

use crate::backend::CaBackend;
use crate::view::GraphiteView;

/// Where a [`Scene`] is presented: the bridge between its layer tree and a
/// native `CALayer` (and, optionally, `NSView`) hierarchy.
///
/// A host does not draw anything itself. It owns a flipped (y-down),
/// clipping *host layer* whose only sublayer is the scene's root layer,
/// placed at its top-left corner. The host layer follows the size of
/// whatever contains it, not of the root layer: anything extending past it
/// is clipped.
///
/// Dropping a `SceneHost` does not detach anything; the native hierarchy
/// keeps the host layer (and view) alive.
pub struct SceneHost {
    layer: Retained<CALayer>,
    view: Option<Retained<NSView>>,
}

impl SceneHost {
    /// The host layer; the scene's root layer is its only sublayer.
    pub fn layer(&self) -> &CALayer {
        &self.layer
    }

    /// The hosting view, for hosts created by [`attach_to_view`].
    pub fn view(&self) -> Option<&NSView> {
        self.view.as_deref()
    }
}

fn new_host_layer() -> Retained<CALayer> {
    let layer = CALayer::new();
    layer.setGeometryFlipped(true);
    layer.setMasksToBounds(true);
    layer.setAnchorPoint(CGPoint::new(0.0, 0.0));
    layer.setAutoresizingMask(CAAutoresizingMask::LayerWidthSizable | CAAutoresizingMask::LayerHeightSizable);
    layer
}

/// Presents `scene` in `container` through a new host sublayer that tracks
/// the container's bounds. Rasterizes at the container's `contentsScale`;
/// call [`Scene::set_backing_scale`] when it changes.
///
/// # Panics
/// If the scene already has a backend.
pub fn attach_to_layer(scene: &Scene, container: &CALayer) -> SceneHost {
    let layer = new_host_layer();
    layer.setFrame(container.bounds());
    layer.setContentsScale(container.contentsScale());
    container.addSublayer(&layer);
    scene.set_backend(Box::new(CaBackend::new(layer.clone(), scene.downgrade())));
    scene.set_backing_scale(container.contentsScale());
    SceneHost { layer, view: None }
}

/// Presents `scene` in a new layer-hosting view that fills `parent` (and
/// resizes with it). The scene follows the window's backing scale.
///
/// # Panics
/// If the scene already has a backend.
pub fn attach_to_view(scene: &Scene, parent: &NSView, mtm: MainThreadMarker) -> SceneHost {
    let layer = new_host_layer();
    let view = GraphiteView::new(mtm, scene.downgrade(), layer.clone(), parent.bounds());
    view.setAutoresizingMask(
        NSAutoresizingMaskOptions::ViewWidthSizable | NSAutoresizingMaskOptions::ViewHeightSizable,
    );
    // Layer-hosting: assign the layer before enabling layers. AppKit owns
    // the view's own layer and resets its `geometryFlipped`, so the flipped
    // host layer is a sublayer that fills it.
    let view_layer = CALayer::new();
    view.setLayer(Some(&view_layer));
    view.setWantsLayer(true);
    layer.setFrame(view_layer.bounds());
    view_layer.addSublayer(&layer);
    parent.addSubview(&view);
    scene.set_backend(Box::new(CaBackend::new(layer.clone(), scene.downgrade())));
    crate::view::sync_backing_scale(&view);
    SceneHost { layer, view: Some(Retained::into_super(view)) }
}
