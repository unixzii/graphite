/** Graphite scene API. Ownership contracts: ../../src/CONTRACT.md. */
#ifndef GRAPHITE_SCENE_H
#define GRAPHITE_SCENE_H
#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Scene lifetime */

    /** Creates an owned Scene with a container root, scale 1 and pixel snapping enabled; rejects invalid size. */
    gr_scene *gr_scene_new(gr_size root_size);

    /** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
    gr_scene *gr_scene_ref(gr_scene *scene);

    /** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
    void gr_scene_unref(gr_scene *scene);

    /** Returns the borrowed root, valid until Scene teardown; ref it to retain an inert handle afterward. */
    gr_layer *gr_scene_get_root(const gr_scene *scene);

    /* Commit scheduling */

    /** Returns whether model changes or native retirements await a commit. */
    bool gr_scene_needs_commit(const gr_scene *scene);

    /** Applies pending changes; clean commits do nothing. Missing backend returns STATE; reentry returns REENTRANT. */
    gr_result gr_scene_commit(gr_scene *scene);

    /* Scale and snapping */

    /** Returns the current number of device pixels per logical point. */
    double gr_scene_get_backing_scale(const gr_scene *scene);

    /** Sets a finite positive scale and invalidates scale-dependent contents; invalid scale returns ARGUMENT. */
    gr_result gr_scene_set_backing_scale(gr_scene *scene, double scale);

    /** Returns whether resolved geometry is snapped to device pixels. */
    bool gr_scene_get_pixel_snapping(const gr_scene *scene);

    /** Enables/disables snapping and invalidates dependent resolved properties. */
    gr_result gr_scene_set_pixel_snapping(gr_scene *scene, bool enabled);

    /** Returns the number of live model layers, including root; excludes retired backend identity leases. */
    size_t gr_scene_get_layer_count(const gr_scene *scene);

    /* Weak Scene links */

    /** Owned weak Scene link for event-loop callbacks; does not keep Scene alive.
     * Lock returns an owned Scene reference, or NULL after teardown starts. */
    gr_scene_link *gr_scene_get_link(gr_scene *scene);

    /** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
    gr_scene_link *gr_scene_link_ref(gr_scene_link *link);

    /** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
    void gr_scene_link_unref(gr_scene_link *link);

    /** Returns an owned Scene reference, or NULL once teardown starts; call on the owning thread. */
    gr_scene *gr_scene_link_lock(gr_scene_link *link);

#ifdef __cplusplus
}
#endif
#endif
