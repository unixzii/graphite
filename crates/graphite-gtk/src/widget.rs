//! `GraphiteWidget`: the `GtkWidget` that presents a scene.

use std::cell::{OnceCell, RefCell};
use std::rc::Rc;

use graphite::WeakScene;
use gtk::glib;
use gtk::prelude::*;
use gtk::subclass::prelude::*;

use crate::backend::Shared;

mod imp {
    use super::*;

    #[derive(Default)]
    pub struct GraphiteWidget {
        pub(super) shared: OnceCell<Rc<Shared>>,
        pub(super) scene: OnceCell<WeakScene>,
        /// Signal handlers connected while realized, with their emitters.
        pub(super) handlers: RefCell<Vec<(glib::Object, glib::SignalHandlerId)>>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for GraphiteWidget {
        const NAME: &'static str = "GraphiteWidget";
        type Type = super::GraphiteWidget;
        type ParentType = gtk::Widget;
    }

    impl ObjectImpl for GraphiteWidget {
        fn constructed(&self) {
            self.parent_constructed();
            // The scene is clipped to the widget, whatever the root's size.
            self.obj().set_overflow(gtk::Overflow::Hidden);
        }
    }

    impl WidgetImpl for GraphiteWidget {
        fn realize(&self) {
            self.parent_realize();
            let obj = self.obj();
            let mut handlers = self.handlers.borrow_mut();
            if let Some(clock) = obj.frame_clock() {
                let weak = obj.downgrade();
                let id = clock.connect_layout(move |_| {
                    if let Some(widget) = weak.upgrade() {
                        widget.commit_if_pending();
                    }
                });
                handlers.push((clock.upcast(), id));
            }
            if let Some(surface) = obj.native().and_then(|n| n.surface()) {
                let weak = obj.downgrade();
                let id = surface.connect_scale_notify(move |_| {
                    if let Some(widget) = weak.upgrade() {
                        widget.sync_backing_scale();
                    }
                });
                handlers.push((surface.upcast(), id));
            }
            drop(handlers);
            obj.sync_backing_scale();
            if self.shared.get().is_some_and(|s| s.pending.get()) {
                obj.request_commit_phase();
            }
        }

        fn unrealize(&self) {
            for (emitter, id) in self.handlers.take() {
                emitter.disconnect(id);
            }
            self.parent_unrealize();
        }

        fn measure(&self, orientation: gtk::Orientation, _for_size: i32) -> (i32, i32, i32, i32) {
            // Natural size: the root layer's. The widget can be smaller; the
            // scene is clipped.
            let size = self.shared.get().and_then(|s| s.tree.borrow().root_size());
            let natural = size.map_or(0, |s| match orientation {
                gtk::Orientation::Horizontal => s.width.ceil() as i32,
                _ => s.height.ceil() as i32,
            });
            (0, natural, -1, -1)
        }

        fn snapshot(&self, snapshot: &gtk::Snapshot) {
            let Some(shared) = self.shared.get() else { return };
            if let Some(node) = shared.tree.borrow_mut().root_node() {
                snapshot.append_node(&node);
            }
        }
    }
}

glib::wrapper! {
    /// Presents a [`graphite::Scene`]; create it with [`crate::attach`].
    pub struct GraphiteWidget(ObjectSubclass<imp::GraphiteWidget>)
        @extends gtk::Widget,
        @implements gtk::Accessible, gtk::Buildable, gtk::ConstraintTarget;
}

impl GraphiteWidget {
    pub(crate) fn new(shared: Rc<Shared>, scene: WeakScene) -> GraphiteWidget {
        let widget: GraphiteWidget = glib::Object::new();
        let imp = widget.imp();
        let _ = imp.shared.set(shared);
        let _ = imp.scene.set(scene);
        widget
    }

    /// Asks the frame clock for a layout phase, where the commit runs.
    pub(crate) fn request_commit_phase(&self) {
        if let Some(clock) = self.frame_clock() {
            clock.request_phase(gtk::gdk::FrameClockPhase::LAYOUT);
        }
    }

    fn commit_if_pending(&self) {
        let imp = self.imp();
        let Some(shared) = imp.shared.get() else { return };
        if shared.pending.replace(false)
            && let Some(scene) = imp.scene.get().and_then(|s| s.upgrade())
        {
            scene.commit();
        }
    }

    /// Pushes the surface's (possibly fractional) scale to the scene.
    fn sync_backing_scale(&self) {
        let Some(surface) = self.native().and_then(|n| n.surface()) else { return };
        if let Some(scene) = self.imp().scene.get().and_then(|s| s.upgrade()) {
            scene.set_backing_scale(surface.scale());
        }
    }
}
