#ifndef GRAPHITE_GTK_INTERNAL_H
#define GRAPHITE_GTK_INTERNAL_H
#include <graphite/gtk.h>
#include <graphite/backend.h>
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

struct gr_gtk_host
{
    size_t refs;
    gr_gtk_shared *shared;
    GtkWidget *widget;
};

GdkRGBA gr_gdk_color(gr_color color);
graphene_rect_t gr_graphene_rect(gr_rect rect);
graphene_point_t gr_graphene_point(gr_point point);
GskRoundedRect gr_gsk_rounded(gr_rect rect, double radius, gr_corners corners);
GskTransform *gr_gsk_transform(gr_affine matrix);
GskPath *gr_gsk_path(const gr_path *path);
GskPath *gr_gsk_trim(GskPath *path, double start, double end);
GskStroke *gr_gsk_stroke(const gr_stroke_style *style);
GdkTexture *gr_gdk_texture(const gr_image *image);
void gr_gsk_paint(GtkSnapshot *snapshot, gr_paint *paint, const graphene_rect_t *bounds, gr_size units);
void gr_gsk_paint_path(GtkSnapshot *snapshot, GskPath *path, gr_fill_rule rule, GskStroke *stroke, gr_paint *paint);
GskRenderNode *gr_gsk_recording(const gr_display_list *list);

gr_gtk_shared *gr_gtk_shared_new(gr_scene *scene);
gr_gtk_shared *gr_gtk_shared_ref(gr_gtk_shared *shared);
void gr_gtk_shared_unref(gr_gtk_shared *shared);
void gr_gtk_tree_clear(gr_gtk_shared *shared);
void gr_gtk_tree_apply(gr_gtk_shared *shared, const gr_commit_batch *batch);
bool gr_gtk_tree_size(gr_gtk_shared *shared, gr_size *out);
GskRenderNode *gr_gtk_tree_root(gr_gtk_shared *shared);
GtkWidget *gr_gtk_widget_new(gr_gtk_shared *shared);
void gr_gtk_widget_request(GtkWidget *widget);
#endif
