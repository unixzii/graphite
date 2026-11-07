/** Core Animation hosting and offscreen rendering. All host calls use the Scene's owning thread.
 * View attachment additionally requires AppKit's main thread. Native pointers are borrowed Objective-C objects.
 */
#ifndef GRAPHITE_MACOS_H
#define GRAPHITE_MACOS_H

#include "graphite.h"

#if defined(GRAPHITE_STATIC)
#define GR_MACOS_API
#elif defined(GRAPHITE_MACOS_BUILDING)
#define GR_MACOS_API GR_EXPORT
#else
#define GR_MACOS_API GR_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    size_t width, height;
    uint8_t *data;
} gr_macos_pixels;

/* Hosts are gr_scene_host handles (see host.h). Native parents retain attached views/layers; releasing the last
 * host reference does not detach them. Functions taking a host return NULL/false for hosts of other platforms. */

/** Attaches Scene to a borrowed CALayer. Returns an owned host; NULL on invalid input/backend installation failure.
 * The host tracks container bounds. Update Scene backing scale when container contentsScale changes.
 */
GR_MACOS_API gr_scene_host *gr_macos_attach_to_layer(gr_scene *scene, void *container);

/** Creates a layer-hosting NSView filling parent. Returns an owned host; automatically follows window backing
 * scale. */
GR_MACOS_API gr_scene_host *gr_macos_attach_to_view(gr_scene *scene, void *parent);

/** Creates an owned offscreen host and installs its backend on Scene; Scene must not already have a backend. */
GR_MACOS_API gr_scene_host *gr_macos_offscreen_new(gr_scene *scene, gr_size size, double scale);

/** Returns the borrowed flipped host CALayer, valid while host remains alive. */
GR_MACOS_API void *gr_macos_host_get_layer(const gr_scene_host *host);

/** Returns the borrowed hosting NSView, or NULL for layer/offscreen hosts. */
GR_MACOS_API void *gr_macos_host_get_view(const gr_scene_host *host);

/** Commits and renders an offscreen host at size/scale into owned top-down premultiplied RGBA8.
 * Output must not own an existing buffer. Returns false on invalid size, expired Scene, a non-offscreen host or
 * unavailable Metal. Rendered dimensions are round(size * scale). Use pixels_clear after success.
 */
GR_MACOS_API bool gr_macos_host_render(gr_scene_host *host, gr_size size, double scale, gr_macos_pixels *out);

/** Renders a borrowed standalone CALayer, without a superlayer, into owned top-down RGBA8 pixels.
 * Does not commit a Graphite Scene. Output must not own an existing buffer.
 */
GR_MACOS_API bool gr_macos_render_layer(void *layer, gr_size size, double scale, gr_macos_pixels *out);

/** Releases the pixel allocation and zeros the result; NULL is accepted. */
GR_MACOS_API void gr_macos_pixels_clear(gr_macos_pixels *pixels);

/** Creates an owned CoreText text system. Layouts it creates are drawn by the Core Animation backend and may be
 * created, queried and released on any thread. Generic families map to the system designs: "system-ui" and
 * "sans-serif" to SF, "serif" to New York and "monospace" to SF Mono. */
GR_MACOS_API gr_text_system *gr_macos_text_system_new(void);

#ifdef __cplusplus
}
#endif

#endif
