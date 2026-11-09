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

/** Copies the host size and scale last reported by GR_EVENT_VIEWPORT, which the host sends whenever they change.
 * Returns false and zeros out before the first report (some platforms report on first layout) and for
 * non-interactive hosts. */
GR_API bool gr_scene_host_get_viewport(const gr_scene_host *host, gr_viewport_event *out);

/** Returns whether the host has keyboard focus, as last reported by FOCUS_IN/FOCUS_OUT. */
GR_API bool gr_scene_host_has_focus(const gr_scene_host *host);

/** Returns the current time on the frame clock in seconds, or NAN for non-interactive hosts. */
GR_API double gr_scene_host_get_time(const gr_scene_host *host);

/** Sets the pointer cursor shown over the host. An unknown cursor returns GR_ERROR_ARGUMENT. */
GR_API gr_result gr_scene_host_set_cursor(gr_scene_host *host, gr_cursor cursor);

/** Asks the platform to give the host keyboard focus; FOCUS_IN follows if it does. */
GR_API gr_result gr_scene_host_request_focus(gr_scene_host *host);

/* Text input
 *
 * A focused text field turns text input on by sharing its editing state, and off with NULL when it loses focus.
 * While it is on, unconsumed KEY_DOWN events go to the platform's text input, which answers with TEXT_EDIT events
 * (typed text, compositions, accent menus) and TEXT_COMMAND events (editing key bindings). While a composition is in
 * progress, keys go to the input method whatever the handler returns, flagged as composing.
 *
 * The platform keeps its own copy of the shared state and applies each TEXT_EDIT to it before delivery, so input
 * methods see consistent text between the application's updates. The field applies the same edit to its own text and
 * shares the result, and shares again whenever its text, selection, composition or layout change for any other
 * reason. Ending or replacing a composition itself (on a click elsewhere, say) makes the input method drop it. */

/** Turns text input on with input, or off with NULL. The platform copies the text and references the layout. input
 * must be valid: UTF-8 text, ranges in order within it on code point boundaries, a layout of exactly that text and
 * finite geometry. */
GR_API gr_result gr_scene_host_set_text_input(gr_scene_host *host, const gr_text_input *input);

#ifdef __cplusplus
}
#endif

#endif
