//! The `graphite::Backend` implementation.

use std::cell::{Cell, RefCell};
use std::rc::Rc;

use graphite::{Backend, CommitBatch};
use gtk::glib;
use gtk::prelude::*;

use crate::tree::Tree;
use crate::widget::GraphiteWidget;

/// State shared by the backend (owned by the scene), the widget and the
/// [`crate::SceneHost`].
#[derive(Default)]
pub(crate) struct Shared {
    pub tree: RefCell<Tree>,
    /// A commit was requested and has not run yet.
    pub pending: Cell<bool>,
    pub widget: RefCell<Option<glib::WeakRef<GraphiteWidget>>>,
}

impl Shared {
    pub(crate) fn widget(&self) -> Option<GraphiteWidget> {
        self.widget.borrow().as_ref().and_then(|w| w.upgrade())
    }
}

pub(crate) struct GskBackend {
    pub shared: Rc<Shared>,
}

impl Backend for GskBackend {
    /// Commits run in the frame clock's layout phase: after tick callbacks
    /// (where animations change the scene), before painting.
    fn request_commit(&mut self) {
        self.shared.pending.set(true);
        if let Some(widget) = self.shared.widget() {
            widget.request_commit_phase();
        }
    }

    fn apply(&mut self, batch: CommitBatch) {
        let applied = self.shared.tree.borrow_mut().apply(&batch);
        if let Some(widget) = self.shared.widget() {
            if applied.resize {
                widget.queue_resize();
            }
            if applied.redraw {
                widget.queue_draw();
            }
        }
    }
}
