#ifndef GRAPHITE_GTK_INTERNAL_H
#define GRAPHITE_GTK_INTERNAL_H

#include <graphite/backend.h>
#include <graphite/gtk.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct gr_gtk_shared
{
    size_t refs;
    GHashTable *nodes, *textures;
    gr_layer *root;
    gr_scene_link *scene;
    GWeakRef widget;
    bool pending, closed;
} gr_gtk_shared;

typedef struct
{
    gr_gtk_shared *shared;
    GtkWidget *widget;
} gr_gtk_host;

/** Returns the GTK host behind host, or NULL for NULL and hosts of other platforms. */
gr_gtk_host *gr_gtk_host_data(const gr_scene_host *host);

GdkRGBA gr_gdk_color(gr_color color);
graphene_rect_t gr_graphene_rect(gr_rect rect);
graphene_point_t gr_graphene_point(gr_point point);
GskRoundedRect gr_gsk_rounded(gr_rect rect, double radius, gr_corners corners);
GskTransform *gr_gsk_transform(gr_affine matrix);
GskPath *gr_gsk_path(const gr_path *path);
GskPath *gr_gsk_trim(GskPath *path, double start, double end);
GskStroke *gr_gsk_stroke(const gr_stroke_style *style);
GdkTexture *gr_gdk_texture(gr_image *image);
void gr_gsk_paint(GtkSnapshot *snapshot, gr_paint *paint, const graphene_rect_t *bounds, gr_size units);
void gr_gsk_paint_path(GtkSnapshot *snapshot, GskPath *path, gr_fill_rule rule, GskStroke *stroke, gr_paint *paint);
GskRenderNode *gr_gsk_recording(const gr_display_list *list);
extern const gr_text_layout_vtable gr_pango_layout_vtable;
void gr_pango_draw_layout(GtkSnapshot *snapshot, gr_text_layout *layout, gr_point origin, gr_color color);
void gr_pango_draw_glyphs(GtkSnapshot *snapshot, gr_text_layout *layout, size_t line, gr_glyph_range glyphs,
                          gr_point origin, gr_color color);

gr_gtk_shared *gr_gtk_shared_new(gr_scene *scene);
gr_gtk_shared *gr_gtk_shared_ref(gr_gtk_shared *shared);
void gr_gtk_shared_unref(gr_gtk_shared *shared);
void gr_gtk_tree_clear(gr_gtk_shared *shared);
void gr_gtk_tree_apply(gr_gtk_shared *shared, const gr_commit_batch *batch);
bool gr_gtk_tree_size(gr_gtk_shared *shared, gr_size *out);
GskRenderNode *gr_gtk_tree_root(gr_gtk_shared *shared);
GtkWidget *gr_gtk_widget_new(gr_gtk_shared *shared);
void gr_gtk_widget_request(GtkWidget *widget);
/* Points the widget's events at host (borrowed), or detaches it with NULL. */
void gr_gtk_widget_set_host(GtkWidget *widget, gr_scene_host *host);
void gr_gtk_widget_request_frame(GtkWidget *widget);
void gr_gtk_widget_set_timer(GtkWidget *widget, double deadline);
void gr_gtk_widget_set_cursor(GtkWidget *widget, gr_cursor cursor);
/* Shares the application's text input state with the widget's input method (NULL turns it off). */
void gr_gtk_widget_set_text_input(GtkWidget *widget, const gr_text_input *input);
/* Replaces the widget's input method context (a GtkIMMulticontext), for tests. */
void gr_gtk_widget_set_im_context(GtkWidget *widget, GtkIMContext *im);
/* Physical key of an X11/Wayland keycode (evdev code + 8). */
gr_key_code gr_gtk_key_code(guint keycode);
/* Named key of a keyval, or GR_KEY_UNKNOWN for keys that type text. */
gr_key gr_gtk_named_key(guint keyval);

#endif
