/** Web (DOM) hosting, built with Emscripten for wasm32. Calls use the Scene's owning thread, which must be the
 * browser's main thread because the backend works on the DOM. The library is static only.
 */
#ifndef GRAPHITE_WEB_H
#define GRAPHITE_WEB_H

#include "graphite.h"

#if defined(GRAPHITE_STATIC)
#define GR_WEB_API
#elif defined(GRAPHITE_WEB_BUILDING)
#define GR_WEB_API GR_EXPORT
#else
#define GR_WEB_API GR_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Hosts are gr_scene_host handles (see host.h). The host element stays in the document until Scene teardown;
 * releasing the last host reference does not remove it. */

/** Installs the Web backend on Scene and attaches it to a new host element filling the element that target names.
 * target is a borrowed UTF-8 string resolved like Emscripten's html5 event targets: a name registered in
 * specialHTMLTargets (specialHTMLTargets['!editor'] = element in JavaScript) names that exact element, and anything
 * else is a CSS selector for the first matching element. Sets the Scene's backing scale from devicePixelRatio.
 * Returns an owned host; NULL for a NULL argument, a target that names no element (document and window do not
 * qualify), or a Scene that already has a backend. */
GR_WEB_API gr_scene_host *gr_web_attach_to_element(gr_scene *scene, const char *target);

/** Returns an owned text system that lays text out with the page's own text stack (its fonts, fallback, shaping and
 * bidi) in hidden elements of the document, or NULL on failure. Unlike other text systems, its layouts and queries
 * work only on the browser's main thread, because they use the DOM. Web backends draw its layouts; glyph font and
 * glyph identities are approximations: each grapheme cluster is one glyph, identified by its text and style. */
GR_WEB_API gr_text_system *gr_web_text_system_new(void);

#ifdef __cplusplus
}
#endif

#endif
