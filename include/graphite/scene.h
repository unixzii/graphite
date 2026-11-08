/** Graphite scene API. */
#ifndef GRAPHITE_SCENE_H
#define GRAPHITE_SCENE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scene lifetime */

/** Creates an owned Scene with a container root and scale 1; rejects invalid size. Resolved geometry is always
 * snapped to device pixels. */
GR_API gr_scene *gr_scene_new(gr_size root_size);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
GR_API gr_scene *gr_scene_ref(gr_scene *scene);

/** Releases an owned reference; NULL is accepted. Final release invalidates
 * weak links/providers and layer Scene associations before user cleanup.
 * External layer references survive as inert handles. Do not release the
 * last Scene reference from a draw callback. Destruction uses safe boundaries. */
GR_API void gr_scene_unref(gr_scene *scene);

/** Returns the borrowed root, valid until Scene teardown; ref it to retain an inert handle afterward. */
GR_API gr_layer *gr_scene_get_root(const gr_scene *scene);

/* Commit scheduling */

/** Returns whether model changes or native retirements await a commit. */
GR_API bool gr_scene_needs_commit(const gr_scene *scene);

/** Applies pending changes; clean commits do nothing. Missing backend preserves
 * pending work and returns STATE; reentry returns REENTRANT. */
GR_API gr_result gr_scene_commit(gr_scene *scene);

/* Scale and snapping */

/** Returns the current number of device pixels per logical point. */
GR_API double gr_scene_get_backing_scale(const gr_scene *scene);

/** Sets a finite positive scale and invalidates scale-dependent contents; invalid scale returns ARGUMENT. */
GR_API gr_result gr_scene_set_backing_scale(gr_scene *scene, double scale);

/** Returns the number of live model layers, including root; excludes retired backend identity leases. */
GR_API size_t gr_scene_get_layer_count(const gr_scene *scene);

/* Weak Scene links */

/** Owned weak Scene link for event-loop callbacks; does not keep Scene alive.
 * Lock returns an owned Scene reference, or NULL after teardown starts. */
GR_API gr_scene_link *gr_scene_get_link(gr_scene *scene);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
GR_API gr_scene_link *gr_scene_link_ref(gr_scene_link *link);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
GR_API void gr_scene_link_unref(gr_scene_link *link);

/** Returns an owned Scene reference, or NULL once teardown starts; call on the owning thread. */
GR_API gr_scene *gr_scene_link_lock(gr_scene_link *link);

#ifdef __cplusplus
}
#endif

#endif
