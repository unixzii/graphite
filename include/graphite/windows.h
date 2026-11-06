/** Windows.UI.Composition hosting. Calls use the Scene's owning thread and a message loop. */
#ifndef GRAPHITE_WINDOWS_H
#define GRAPHITE_WINDOWS_H
#include "graphite.h"
#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct gr_windows_host gr_windows_host;

    typedef struct
    {
        uint32_t width, height;
        uint8_t *data;
    } gr_windows_pixels;

    /** Returns a borrowed IInspectable compositor for this thread, or NULL on initialization failure. */
    void *gr_windows_get_compositor(void);
    /** Creates an owned child-window host filling parent HWND; tracks its DPI. NULL on failure. */
    gr_windows_host *gr_windows_attach_to_hwnd(gr_scene *scene, void *parent);
    /** Attaches to a borrowed ContainerVisual ABI pointer from this thread's compositor. Returns owned host or NULL. */
    gr_windows_host *gr_windows_attach_to_visual(gr_scene *scene, void *container);
    /** Creates an owned off-desktop window sized to root bounds at scale; caller commits before capturing. */
    gr_windows_host *gr_windows_offscreen_new(gr_scene *scene, double scale);
    /** Acquires an owned reference; NULL returns NULL. */
    gr_windows_host *gr_windows_host_ref(gr_windows_host *host);
    /** Detaches visuals/destroys host window on final unref; NULL is accepted. */
    void gr_windows_host_unref(gr_windows_host *host);
    /** Returns borrowed HWND or NULL for visual-only hosting. */
    void *gr_windows_host_get_hwnd(const gr_windows_host *host);
    /** Returns borrowed ContainerVisual ABI pointer, valid for the host lifetime. */
    void *gr_windows_host_get_visual(const gr_windows_host *host);
    /** Captures an offscreen host using black/white DWM readbacks to recover premultiplied RGBA.
     * Does not commit Scene. Output must be empty; it is cleared on failure. */
    bool gr_windows_host_capture(gr_windows_host *host, gr_windows_pixels *out);
    /** Captures a process-owned visible/covered window as opaque RGBA after composition commits. */
    bool gr_windows_capture_window(void *hwnd, gr_windows_pixels *out);
    /** Frees pixel storage and zeros the result; NULL is accepted. */
    void gr_windows_pixels_clear(gr_windows_pixels *pixels);
#ifdef __cplusplus
}
#endif
#endif
