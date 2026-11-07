/** Core Animation hosting and offscreen rendering. All host calls use the Scene's owning thread.
 * View attachment additionally requires AppKit's main thread. Native pointers are borrowed Objective-C objects.
 */
#ifndef GRAPHITE_MACOS_H
#define GRAPHITE_MACOS_H

#include "graphite.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gr_macos_host gr_macos_host;

typedef struct
{
    size_t width, height;
    uint8_t *data;
} gr_macos_pixels;

/** Attaches Scene to a borrowed CALayer. Returns an owned host; NULL on invalid input/backend installation failure.
 * The host tracks container bounds. Update Scene backing scale when container contentsScale changes.
 */
gr_macos_host *gr_macos_attach_to_layer(gr_scene *scene, void *container);

/** Creates a layer-hosting NSView filling parent. Returns an owned host; automatically follows window backing
 * scale. */
gr_macos_host *gr_macos_attach_to_view(gr_scene *scene, void *parent);

/** Creates an owned offscreen host and installs its backend on Scene; Scene must not already have a backend. */
gr_macos_host *gr_macos_offscreen_new(gr_scene *scene, gr_size size, double scale);

/** Acquires a host reference; NULL returns NULL. */
gr_macos_host *gr_macos_host_ref(gr_macos_host *host);

/** Releases a host reference. Native parents retain attached views/layers; this does not detach them. */
void gr_macos_host_unref(gr_macos_host *host);

/** Returns the borrowed flipped host CALayer, valid while host remains alive. */
void *gr_macos_host_get_layer(const gr_macos_host *host);

/** Returns the borrowed hosting NSView, or NULL for layer/offscreen hosts. */
void *gr_macos_host_get_view(const gr_macos_host *host);

/** Commits and renders an offscreen host at size/scale into owned top-down premultiplied RGBA8.
 * Output must not own an existing buffer. Returns false on invalid size, expired Scene or unavailable Metal.
 * Rendered dimensions are round(size * scale). Use pixels_clear after success.
 */
bool gr_macos_host_render(gr_macos_host *host, gr_size size, double scale, gr_macos_pixels *out);

/** Renders a borrowed standalone CALayer, without a superlayer, into owned top-down RGBA8 pixels.
 * Does not commit a Graphite Scene. Output must not own an existing buffer.
 */
bool gr_macos_render_layer(void *layer, gr_size size, double scale, gr_macos_pixels *out);

/** Releases the pixel allocation and zeros the result; NULL is accepted. */
void gr_macos_pixels_clear(gr_macos_pixels *pixels);

/** Creates an owned CoreText text system. Layouts it creates are drawn by the Core Animation backend and may be
 * created, queried and released on any thread. Generic families map to the system designs: "system-ui" and
 * "sans-serif" to SF, "serif" to New York and "monospace" to SF Mono. */
gr_text_system *gr_macos_text_system_new(void);

#ifdef __cplusplus
}
#endif

#endif
