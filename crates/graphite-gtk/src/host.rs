//! [`SceneHost`]: bridges a [`Scene`] into a GTK widget hierarchy, or into
//! an offscreen renderer.

use std::rc::Rc;

use graphite::Scene;
use gtk::gsk;
use gtk::prelude::*;

use crate::backend::{GskBackend, Shared};
use crate::widget::GraphiteWidget;

/// Where a [`Scene`] is presented.
///
/// A host draws nothing itself: it keeps one cached render node per layer
/// and hands the composed node tree to its widget's `snapshot()`, or to
/// [`crate::offscreen::render`].
pub struct SceneHost {
    shared: Rc<Shared>,
    widget: Option<GraphiteWidget>,
}

impl SceneHost {
    /// The widget presenting the scene; put it anywhere in your widget tree.
    /// `None` for offscreen hosts.
    pub fn widget(&self) -> Option<&gtk::Widget> {
        self.widget.as_ref().map(|w| w.upcast_ref())
    }

    /// The composed node tree of the last commit, or `None` if nothing is
    /// visible.
    pub fn root_node(&self) -> Option<gsk::RenderNode> {
        self.shared.tree.borrow_mut().root_node()
    }

    /// The root layer's size, once committed.
    pub fn size(&self) -> Option<graphite::Size> {
        self.shared.tree.borrow().root_size()
    }
}

fn install(scene: &Scene, widget: bool) -> SceneHost {
    let shared = Rc::new(Shared::default());
    let widget = widget.then(|| {
        let widget = GraphiteWidget::new(shared.clone(), scene.downgrade());
        *shared.widget.borrow_mut() = Some(widget.downgrade());
        widget
    });
    scene.set_backend(Box::new(GskBackend { shared: shared.clone() }));
    SceneHost { shared, widget }
}

/// Presents `scene` in a new widget ([`SceneHost::widget`]). The widget's
/// natural size is the root layer's; when allocated less, the scene is
/// clipped. Commits run in the frame clock's layout phase, and the scene
/// follows the surface's (fractional) scale.
///
/// GTK must be initialized, and this must run on its main thread.
///
/// # Panics
/// If the scene already has a backend.
pub fn attach(scene: &Scene) -> SceneHost {
    install(scene, true)
}

/// Presents `scene` without a widget, for [`crate::offscreen::render`].
/// Nothing schedules commits: call [`Scene::commit`] and
/// [`Scene::set_backing_scale`] yourself.
///
/// # Panics
/// If the scene already has a backend.
pub fn attach_offscreen(scene: &Scene) -> SceneHost {
    install(scene, false)
}
