/** Windows.UI.Composition hosting. Calls use the Scene's owning thread and a message loop. */
#ifndef GRAPHITE_WINDOWS_H
#define GRAPHITE_WINDOWS_H

#include "graphite.h"

#if defined(GRAPHITE_STATIC)
#define GR_WINDOWS_API
#elif defined(GRAPHITE_WINDOWS_BUILDING)
#define GR_WINDOWS_API GR_EXPORT
#else
#define GR_WINDOWS_API GR_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t width, height;
    uint8_t *data;
} gr_windows_pixels;

/** Returns a borrowed IInspectable compositor for this thread, or NULL on initialization failure. */
GR_WINDOWS_API void *gr_windows_get_compositor(void);
/* Hosts are gr_scene_host handles (see host.h). Releasing the last host reference detaches its visuals and destroys
 * its host window. Functions taking a host return NULL/false for hosts of other platforms. */

/** Creates an owned child-window host filling parent HWND; tracks its DPI. NULL on failure. */
GR_WINDOWS_API gr_scene_host *gr_windows_attach_to_hwnd(gr_scene *scene, void *parent);
/** Attaches to a borrowed ContainerVisual ABI pointer from this thread's compositor. Returns owned host or NULL. */
GR_WINDOWS_API gr_scene_host *gr_windows_attach_to_visual(gr_scene *scene, void *container);
/** Creates an owned off-desktop window sized to root bounds at scale; caller commits before capturing. */
GR_WINDOWS_API gr_scene_host *gr_windows_offscreen_new(gr_scene *scene, double scale);
/** Returns borrowed HWND or NULL for visual-only hosting. */
GR_WINDOWS_API void *gr_windows_host_get_hwnd(const gr_scene_host *host);
/** Returns borrowed ContainerVisual ABI pointer, valid for the host lifetime. */
GR_WINDOWS_API void *gr_windows_host_get_visual(const gr_scene_host *host);
/** Captures an offscreen host using black/white DWM readbacks to recover premultiplied RGBA.
 * Does not commit Scene. Output must be empty; it is cleared on failure. */
GR_WINDOWS_API bool gr_windows_host_capture(gr_scene_host *host, gr_windows_pixels *out);
/** Captures a process-owned visible/covered window as opaque RGBA after composition commits. */
GR_WINDOWS_API bool gr_windows_capture_window(void *hwnd, gr_windows_pixels *out);
/** Frees pixel storage and zeros the result; NULL is accepted. */
GR_WINDOWS_API void gr_windows_pixels_clear(gr_windows_pixels *pixels);

/** Creates an owned DirectWrite text system, or NULL on failure; its layouts are drawn by this backend and may be
 * created, queried and released on any thread. Generic families map to Segoe UI ("system-ui", "sans-serif"),
 * Times New Roman and Consolas. Automatic direction follows the first strong character of the whole text. */
GR_WINDOWS_API gr_text_system *gr_windows_text_system_new(void);

#ifdef __cplusplus
}
#endif

#endif
