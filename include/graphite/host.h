/** Graphite scene host API: the common handle that platform hosts return. Platform headers create hosts and add
 * functions for their own hosts; those return NULL/false for a host of another platform. */
#ifndef GRAPHITE_HOST_H
#define GRAPHITE_HOST_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scene host lifetime */

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
GR_API gr_scene_host *gr_scene_host_ref(gr_scene_host *host);

/** Releases an owned reference; NULL is accepted. Final release destroys the platform host; whether its native
 * view, widget or window stays in place is documented by each platform. */
GR_API void gr_scene_host_unref(gr_scene_host *host);

/** Returns an owned reference to the hosted Scene, or NULL once its teardown starts; the host does not keep the
 * Scene alive. */
GR_API gr_scene *gr_scene_host_get_scene(const gr_scene_host *host);

#ifdef __cplusplus
}
#endif

#endif
