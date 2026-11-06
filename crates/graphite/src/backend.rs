//! The backend interface.

use crate::commit::CommitBatch;

/// Renders a [`crate::Scene`] with a platform's native technology.
///
/// A backend is owned by its scene and is only ever called on the scene's
/// thread. Implementations must not call back into the scene from inside
/// these methods; anything that needs the scene (typically running
/// [`crate::Scene::commit`] at the platform's commit point) should hold a
/// [`crate::WeakScene`] and do it later, from the event loop.
///
/// The one exception is drawing: [`CommitBatch::drawers`] runs layer
/// drawers, inside [`apply`](Self::apply) or later (keep a clone), whenever
/// the backend needs a drawing. Backends keep only their own rendering of
/// it (a texture, a render node, a layer's backing store) and may drop that
/// and draw again at any time.
pub trait Backend {
    /// The scene went from clean to dirty. Arrange for [`crate::Scene::commit`]
    /// to run at the platform's commit point: the end of the current run-loop
    /// iteration (macOS), a `DispatcherQueue` callback (Windows), the next
    /// `snapshot()` after `gtk_widget_queue_draw` (GTK), or the next
    /// `requestAnimationFrame` (Web). Called at most once per commit.
    fn request_commit(&mut self);

    /// Applies one batch. See [`CommitBatch`] for the ordering contract.
    fn apply(&mut self, batch: CommitBatch);
}
