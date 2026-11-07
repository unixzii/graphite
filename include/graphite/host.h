/** Graphite scene host API: the common handle that platform hosts return. Platform headers create hosts and add
 * functions for their own hosts; those return NULL/false for a host of another platform. */
#ifndef GRAPHITE_HOST_H
#define GRAPHITE_HOST_H

#include "event.h"
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

/* Events
 *
 * Interactive hosts (a native view, widget or window) deliver input and frame events to one handler; see event.h.
 * Hosts without a native input surface (layer, visual and offscreen hosts) return GR_ERROR_UNSUPPORTED, false or NAN
 * from these functions. */

/** Returns whether the host delivers events. */
GR_API bool gr_scene_host_is_interactive(const gr_scene_host *host);

/** Installs handler, or clears it with NULL. Copies the descriptor and takes userdata ownership on GR_OK; a missing
 * handle callback returns GR_ERROR_ARGUMENT. The new handler is published before the old one is destroyed; during
 * event delivery the old one is destroyed after the outermost delivery returns. Final host release destroys the
 * installed handler. */
GR_API gr_result gr_scene_host_set_event_handler(gr_scene_host *host, const gr_event_handler *handler);

/** Requests one GR_EVENT_FRAME at the next display refresh, delivered before that frame's commit. Requests made
 * before delivery coalesce; request again from the FRAME handler to keep animating. With no request the host does
 * not wake for frames. */
GR_API gr_result gr_scene_host_request_frame(gr_scene_host *host);

/** Returns the current time on the frame clock in seconds, or NAN for non-interactive hosts. */
GR_API double gr_scene_host_get_time(const gr_scene_host *host);

/** Sets the pointer cursor shown over the host. An unknown cursor returns GR_ERROR_ARGUMENT. */
GR_API gr_result gr_scene_host_set_cursor(gr_scene_host *host, gr_cursor cursor);

/** Asks the platform to give the host keyboard focus; FOCUS_IN follows if it does. */
GR_API gr_result gr_scene_host_request_focus(gr_scene_host *host);

/** Enables text input with the caret where the input method should place its windows, or disables it with NULL.
 * Call again whenever the caret moves. A nonfinite or negative-size caret returns GR_ERROR_ARGUMENT. */
GR_API gr_result gr_scene_host_set_text_input(gr_scene_host *host, const gr_text_input *input);

#ifdef __cplusplus
}
#endif

#endif
