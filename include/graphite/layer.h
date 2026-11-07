/** Graphite layer API. */
#ifndef GRAPHITE_LAYER_H
#define GRAPHITE_LAYER_H

#include "resource.h"
#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Layer conventions:
 * Setters compare values and coalesce dependent changes into a commit.
 * Kind-specific setters reject incompatible kinds with GR_ERROR_KIND;
 * kind-specific getters require the correct kind. Optional value getters
 * require an output pointer, return presence and zero absent output values.
 * After Scene teardown, surviving layer getters return defaults/empty
 * relationships and value setters are successful no-ops. Kind is retained;
 * drawer installation instead rejects ownership transfer with GR_ERROR_STATE.
 */

/* Property descriptions */

typedef struct
{
    double width;
    gr_color color;
} gr_border;

typedef struct
{
    gr_color color;
    gr_point offset;
    double blur_sigma, spread;
    gr_path *path; /**< Optional, local-space custom shadow; spread ignored if set. */
} gr_shadow;

/* Layer lifetime */

/** Creates an owned detached layer of a fixed kind in a live Scene.
 * Returns NULL for an invalid kind. Defaults: zero bounds/position, anchor
 * (0.5,0.5), identity transform, opacity 1, visible, no clip/background/border/
 * shadow/blur, radius 0 and all corners. Shapes have no path/fill/stroke, nonzero
 * fill rule and stroke range [0,1]. Content gravity defaults to resize. */
gr_layer *gr_layer_new(gr_scene *scene, gr_layer_kind kind);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_layer *gr_layer_ref(gr_layer *layer);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
void gr_layer_unref(gr_layer *layer);

/** Returns the creation-time kind, including for an inert layer. */
gr_layer_kind gr_layer_get_kind(const gr_layer *layer);

/** Returns whether the layer still belongs to a live Scene. */
bool gr_layer_is_alive(const gr_layer *layer);

/** Returns the borrowed Scene, or NULL for an inert layer; the layer does not keep Scene alive. */
gr_scene *gr_layer_get_scene(const gr_layer *layer);

/* Geometry */

/* Property setters compare only their own value and mark dependent dirty groups.
 * Multiple setters coalesce into the next commit. Inert-layer setters are no-ops;
 * getters return the documented defaults. Scene mutation during draw is rejected. */

/** Returns local bounds; default is a zero rectangle. */
gr_rect gr_layer_get_bounds(const gr_layer *layer);

/** Sets local bounds, including the scroll origin; rejects nonfinite values or negative size. */
gr_result gr_layer_set_bounds(gr_layer *layer, gr_rect bounds);

/** Returns the anchor position in parent coordinates; default is (0,0). */
gr_point gr_layer_get_position(const gr_layer *layer);

/** Sets the anchor position in parent coordinates; rejects nonfinite values. */
gr_result gr_layer_set_position(gr_layer *layer, gr_point position);

/** Returns the unit-coordinate anchor; default is (0.5,0.5). */
gr_point gr_layer_get_anchor_point(const gr_layer *layer);

/** Sets the anchor without clamping, allowing positions outside [0,1]; rejects nonfinite values. */
gr_result gr_layer_set_anchor_point(gr_layer *layer, gr_point anchor_point);

/** Returns the affine transform; default is identity. */
gr_affine gr_layer_get_transform(const gr_layer *layer);

/** Sets the affine transform around the anchor; rejects nonfinite values. */
gr_result gr_layer_set_transform(gr_layer *layer, gr_affine transform);

/** Returns the transformed axis-aligned frame in parent coordinates; inert layers return a zero frame. */
gr_rect gr_layer_get_frame(const gr_layer *layer);

/** Sets position and bounds size, preserving bounds origin and transform; assumes identity transform. */
gr_result gr_layer_set_frame(gr_layer *layer, gr_rect frame);

/* Appearance */

/** Returns group opacity; default is 1. */
float gr_layer_get_opacity(const gr_layer *layer);

/** Sets group opacity, clamping finite input to [0,1]. */
gr_result gr_layer_set_opacity(gr_layer *layer, float opacity);

/** Returns whether the layer is hidden; default is false. */
bool gr_layer_get_hidden(const gr_layer *layer);

/** Changes visibility without detaching the layer. */
gr_result gr_layer_set_hidden(gr_layer *layer, bool hidden);

/** Returns whether contents/children are clipped to rounded bounds; default is false. */
bool gr_layer_get_masks_to_bounds(const gr_layer *layer);

/** Enables clipping to rounded bounds; the outer shadow remains outside this clip. */
gr_result gr_layer_set_masks_to_bounds(gr_layer *layer, bool masks_to_bounds);

/** Returns the corner radius in points; default is zero. */
double gr_layer_get_corner_radius(const gr_layer *layer);

/** Sets a finite nonnegative corner radius. */
gr_result gr_layer_set_corner_radius(gr_layer *layer, double corner_radius);

/** Returns the rounded corner selection; default is GR_CORNERS_ALL. */
gr_corners gr_layer_get_masked_corners(const gr_layer *layer);

/** Sets which corners are rounded; rejects unknown flag bits. */
gr_result gr_layer_set_masked_corners(gr_layer *layer, gr_corners masked_corners);

/** Copies the optional background into required out; returns true if present.
 * If absent (including an inert layer), returns false and zeroes out. */
bool gr_layer_get_background(const gr_layer *layer, gr_color *out);

/** Copies background, or clears it with NULL; rejects invalid numeric values. */
gr_result gr_layer_set_background(gr_layer *layer, const gr_color *background);

/** Copies the optional border into required out; returns true if present.
 * If absent (including an inert layer), returns false and zeroes out. */
bool gr_layer_get_border(const gr_layer *layer, gr_border *out);

/** Copies border, or clears it with NULL; rejects invalid numeric values. */
gr_result gr_layer_set_border(gr_layer *layer, const gr_border *border);

/** Returns the layer blur's Gaussian standard deviation in points; default is zero (no blur). */
double gr_layer_get_blur(const gr_layer *layer);

/** Blurs the layer's whole rendering, including its shadow, mask, clip and sublayers; the result bleeds past the
 * bounds. Sets a finite nonnegative sigma; zero removes the blur. Core Animation scales sigma with the layer's and its
 * ancestors' transforms; GTK and Windows apply it in screen space. */
gr_result gr_layer_set_blur(gr_layer *layer, double sigma);

/** Copies the optional shadow into required out; returns true if present.
 * If absent (including an inert layer), returns false and zeroes out. Any path in the copied value is borrowed
 * until shadow replacement or Scene teardown. */
bool gr_layer_get_shadow(const gr_layer *layer, gr_shadow *out);

/** Copies shadow, or clears it with NULL; rejects invalid numeric values. Refs the new optional path before
 * releasing the previous one. */
gr_result gr_layer_set_shadow(gr_layer *layer, const gr_shadow *shadow);

/* Hierarchy and masks */

/** Returns the number of ordered children, or zero for an inert layer. */
size_t gr_layer_get_child_count(const gr_layer *layer);

/** Returns a borrowed child, or NULL out of range; ref before retaining across hierarchy changes. */
gr_layer *gr_layer_get_child(const gr_layer *layer, size_t index);

/** Returns the borrowed sublayer parent, or NULL; mask ownership is not a sublayer parent. */
gr_layer *gr_layer_get_parent(const gr_layer *layer);

/** Appends/reparents child and acquires parent ownership; rejects cross-Scene, cyclic or illegal root/mask
 * attachment. */
gr_result gr_layer_add_child(gr_layer *parent, gr_layer *child);

/** index is interpreted after removing child from its old position; clamp to count. */
gr_result gr_layer_insert_child(gr_layer *parent, gr_layer *child, size_t index);

/** Detaches child or mask ownership; a detached layer is unchanged. Caller must hold a valid reference. */
gr_result gr_layer_remove_from_parent(gr_layer *layer);

/** Returns the borrowed mask, or NULL; valid until replacement, removal or Scene teardown. */
gr_layer *gr_layer_get_mask(const gr_layer *layer);

/** Refs mask, or clears with NULL; rejects cross-Scene, cyclic, parented or nested masks without changing state. */
gr_result gr_layer_set_mask(gr_layer *layer, gr_layer *mask);

/* Content layers */

/** Returns the borrowed image or NULL, including while a drawer is installed; requires a content layer. */
gr_image *gr_content_layer_get_image(const gr_layer *layer);

/** Refs image (NULL clears) and removes any drawer; wrong kind returns KIND. */
gr_result gr_content_layer_set_image(gr_layer *layer, gr_image *image);

/** Returns image placement gravity, defaulting to resize for an inert content layer. */
gr_gravity gr_content_layer_get_gravity(const gr_layer *layer);

/** Changes image placement; rejects invalid gravity or a non-content layer. */
gr_result gr_content_layer_set_gravity(gr_layer *layer, gr_gravity gravity);

typedef struct
{
    gr_rect bounds;
    double scale;
} gr_draw_info;

/** Drawer callback ownership and lifetime:
 * A drawer may borrow its own layer as userdata through its destruction;
 * core keeps that allocation valid. Other captures need owned references or
 * externally guaranteed lifetimes. Strong self/ancestor captures can cycle.
 * During draw, do not release the last Scene reference; balanced temporary
 * ref/unref is allowed. Nested draw and Scene commit/mutation are rejected.
 * Replacement publishes the new state before invoking the old destructor.
 * Destructors may release other objects under normal Scene reentry rules.
 */
typedef struct
{
    void *userdata;
    void (*draw)(void *userdata, gr_canvas *canvas, const gr_draw_info *info);
    void (*destroy)(void *userdata); /**< Optional; exactly once after successful transfer. */
} gr_drawer;

/** Copies descriptor, transfers userdata ownership only on GR_OK and live layer.
 * Inert layer returns GR_ERROR_STATE here (cannot accept ownership).
 * Draw is pure with respect to Scene: reads allowed, mutations rejected.
 * Canvas/info borrowed for callback only; never unref/finish the supplied canvas.
 * State changes outside draw require set_needs_display. */
gr_result gr_content_layer_set_drawer(gr_layer *layer, const gr_drawer *drawer);

/** Removes the drawer and its contents; destroys userdata at a safe boundary. Rejects mutation during draw. */
gr_result gr_content_layer_clear_drawer(gr_layer *layer);

/** Returns whether a drawer is installed; requires a content layer. */
bool gr_content_layer_has_drawer(const gr_layer *layer);

/** Advances the installed drawer's display version and schedules an update; no-op without a drawer. */
gr_result gr_content_layer_set_needs_display(gr_layer *layer);

/* Shape layers */

/* Getters require a shape layer. Setters return GR_ERROR_KIND for other kinds.
 * Borrowed resources remain valid until replacement or Scene teardown; ref them
 * before retaining across either. NULL resource setters clear the property. */

/** Returns the borrowed path, or NULL when unset or inert. */
gr_path *gr_shape_layer_get_path(const gr_layer *layer);

/** Sets path, acquiring its reference before releasing the old value; NULL clears. */
gr_result gr_shape_layer_set_path(gr_layer *layer, gr_path *path);

/** Returns the borrowed fill, or NULL when unset or inert. */
gr_paint *gr_shape_layer_get_fill(const gr_layer *layer);

/** Sets fill, acquiring its reference before releasing the old value; NULL clears. */
gr_result gr_shape_layer_set_fill(gr_layer *layer, gr_paint *fill);

/** Returns the borrowed stroke, or NULL when unset or inert. */
gr_stroke *gr_shape_layer_get_stroke(const gr_layer *layer);

/** Sets stroke, acquiring its reference before releasing the old value; NULL clears. */
gr_result gr_shape_layer_set_stroke(gr_layer *layer, gr_stroke *stroke);

/** Returns the fill rule; default is GR_FILL_NONZERO. */
gr_fill_rule gr_shape_layer_get_fill_rule(const gr_layer *layer);

/** Sets the fill rule; rejects unknown values without changing state. */
gr_result gr_shape_layer_set_fill_rule(gr_layer *layer, gr_fill_rule rule);

/** Returns the stroke start fraction; default is 0. */
double gr_shape_layer_get_stroke_start(const gr_layer *layer);

/** Sets the stroke start fraction, clamping finite input to [0,1]. */
gr_result gr_shape_layer_set_stroke_start(gr_layer *layer, double fraction);

/** Returns the stroke end fraction; default is 1. */
double gr_shape_layer_get_stroke_end(const gr_layer *layer);

/** Sets the stroke end fraction, clamping finite input to [0,1]. */
gr_result gr_shape_layer_set_stroke_end(gr_layer *layer, double fraction);

/* Gradient layers */

/** Gradient layer uses LINEAR/RADIAL paint in unit coordinates of its bounds. */
gr_paint *gr_gradient_layer_get_gradient(const gr_layer *layer);

/** Refs a linear/radial gradient, or clears with NULL; rejects solid paint or wrong layer kind. */
gr_result gr_gradient_layer_set_gradient(gr_layer *layer, gr_paint *gradient);

#ifdef __cplusplus
}
#endif

#endif
