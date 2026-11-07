/** GTK 4.22 / GSK hosting. Initialize GTK before use; all calls run on its main thread. */
#ifndef GRAPHITE_GTK_H
#define GRAPHITE_GTK_H

#include "graphite.h"

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gr_gtk_host gr_gtk_host;
typedef struct gr_gtk_renderer gr_gtk_renderer;

typedef enum
{
    GR_GTK_GL,
    GR_GTK_VULKAN,
    GR_GTK_CAIRO
} gr_gtk_renderer_kind;

typedef struct
{
    uint32_t width, height;
    uint8_t *data;
} gr_gtk_pixels;

/** Installs a backend and creates a widget. Returns an owned host, or NULL if already attached/uninitialized. */
gr_gtk_host *gr_gtk_attach(gr_scene *scene);
/** Installs a widget-less backend. Caller commits and sets scale explicitly. Returns an owned host or NULL. */
gr_gtk_host *gr_gtk_attach_offscreen(gr_scene *scene);
/** Acquires an owned reference; NULL returns NULL. */
gr_gtk_host *gr_gtk_host_ref(gr_gtk_host *host);
/** Releases ownership; NULL is accepted. A parented widget keeps its shared rendering state alive. */
void gr_gtk_host_unref(gr_gtk_host *host);
/** Returns the borrowed widget, or NULL for an offscreen host. */
GtkWidget *gr_gtk_host_get_widget(const gr_gtk_host *host);
/** Returns an owned node of the last committed state, or NULL for empty/expired scenes. Unref with
 * gsk_render_node_unref. */
GskRenderNode *gr_gtk_host_get_root_node(gr_gtk_host *host);
/** Copies the committed root size to out; returns false and zeros out before first commit or after Scene teardown.
 */
bool gr_gtk_host_get_size(const gr_gtk_host *host, gr_size *out);

/** Realizes a reusable renderer for the default display. Returns owned renderer or NULL; optional error follows
 * GError rules. */
gr_gtk_renderer *gr_gtk_renderer_new(gr_gtk_renderer_kind kind, GError **error);
/** Acquires an owned reference; NULL returns NULL. */
gr_gtk_renderer *gr_gtk_renderer_ref(gr_gtk_renderer *renderer);
/** Unrealizes on final release after all readbacks; NULL is accepted. */
void gr_gtk_renderer_unref(gr_gtk_renderer *renderer);
/** Renders last committed state at scale into owned top-down premultiplied RGBA8.
 * Does not commit. Output must not own a buffer; cleared on failure. Returns false for invalid/empty dimensions.
 */
bool gr_gtk_renderer_render(gr_gtk_renderer *renderer, gr_gtk_host *host, double scale, gr_gtk_pixels *out);
/** Frees pixel storage and zeros the result; NULL is accepted. */
void gr_gtk_pixels_clear(gr_gtk_pixels *pixels);

/** Creates an owned Pango text system; its layouts are drawn by the GTK backend. Unlike the rest of this header,
 * layouts may be created, queried and released on any thread (a mutex serializes Pango). "system-ui" maps to GTK's
 * interface font when GTK is initialized; other generic families go to fontconfig. */
gr_text_system *gr_gtk_text_system_new(void);

#ifdef __cplusplus
}
#endif

#endif
