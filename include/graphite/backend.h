/** Graphite backend interface and drawing replay data. */
#ifndef GRAPHITE_BACKEND_H
#define GRAPHITE_BACKEND_H

#include "graphite.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Commit snapshots */

typedef struct gr_drawer_provider gr_drawer_provider;

typedef uint32_t gr_dirty_bits;

enum
{
    GR_DIRTY_GEOMETRY = 1u,
    GR_DIRTY_APPEARANCE = 2u,
    GR_DIRTY_SHADOW = 4u,
    GR_DIRTY_CONTENT = 8u,
    GR_DIRTY_MASK = 16u,
    GR_DIRTY_CHILDREN = 32u
};

/** Read-only raw property snapshots in commit updates. Pointer members borrow
 * resources from the batch; copy values and ref resources to retain them.
 * Applications change individual properties through the layer API. */
typedef struct
{
    gr_rect bounds;
    gr_point position, anchor_point;
    gr_affine transform;
    float opacity;
    bool hidden, masks_to_bounds;
    bool has_background;
    gr_color background;
    double corner_radius;
    gr_corners masked_corners;
    bool has_border;
    gr_border border;
    bool has_shadow;
    gr_shadow shadow;
    double blur_sigma;
} gr_layer_props;

typedef struct
{
    gr_path *path;  /**< Optional. */
    gr_paint *fill; /**< Optional. */
    gr_fill_rule fill_rule;
    gr_stroke *stroke; /**< Optional. */
    double stroke_start, stroke_end;
} gr_shape_props;

typedef enum
{
    GR_CONTENT_NONE,
    GR_CONTENT_IMAGE,
    GR_CONTENT_DRAWN,
    GR_CONTENT_SHAPE,
    GR_CONTENT_GRADIENT
} gr_content_kind;

typedef struct
{
    gr_content_kind kind;

    union
    {
        struct
        {
            gr_image *image;
            gr_rect rect;
        } image; /**< Box-space placement. */

        uint64_t display_version;
        gr_shape_props shape; /**< Local-space path. */
        gr_paint *gradient;   /**< Unit coordinates. */
    } value;
} gr_resolved_content;

typedef enum
{
    GR_SHADOW_ROUNDED_RECT,
    GR_SHADOW_PATH
} gr_shadow_shape_kind;

typedef struct
{
    gr_shadow_shape_kind kind;

    union
    {
        struct
        {
            gr_rect rect;
            double radius;
            gr_corners corners;
            double spread;
        } rounded_rect;

        gr_path *path; /**< Local coordinates; translate by bounds_offset for box space. */
    } value;
} gr_shadow_shape;

typedef struct
{
    gr_layer_props props;
    gr_affine transform; /**< Snapped box-to-parent transform; root identity. */
    gr_point snap_offset, bounds_offset;
    gr_size size; /**< Resolved size, not raw props.bounds.size. */
    bool has_border;
    gr_border border; /**< Snapped width. */
    gr_layer *mask;   /**< Nullable. */
    bool has_shadow;
    gr_shadow_shape shadow_shape;
    gr_resolved_content content;
} gr_resolved;

typedef struct
{
    gr_layer *layer;
    gr_layer_kind kind;
} gr_realized_layer;

typedef struct
{
    gr_layer *parent;
    gr_layer_list children;
} gr_children_update;

typedef struct
{
    gr_layer *layer;
    gr_dirty_bits changed; /**< Never GR_DIRTY_CHILDREN. */
    gr_resolved resolved;
} gr_layer_update;

GR_DECLARE_LIST(gr_realized_layer_list, gr_realized_layer);

GR_DECLARE_LIST(gr_children_update_list, gr_children_update);

GR_DECLARE_LIST(gr_layer_update_list, gr_layer_update);

typedef struct
{
    gr_layer *root; /**< First commit only; otherwise NULL. */
    double backing_scale;
    gr_realized_layer_list realized;
    gr_children_update_list children;
    gr_layer_update_list updates;
    gr_layer_list released;
    gr_drawer_provider *drawers;
} gr_commit_batch;

/* Backend integration */

/** Borrowed batch and snapshot pointers expire on return from apply. Copy values
 * and ref immutable resources to retain them. Layer identity pointers have a
 * DIFFERENT lease: core keeps their allocation until the apply carrying their
 * released entry returns (or backend destroy returns during Scene teardown).
 * Backend maps may use those pointers within that lease WITHOUT gr_layer_ref.
 * released pointers are identity-only: do not dereference/ref/resurrect them.
 * Backend must forget released pointers before returning. Strong references
 * acquired by backend code are ordinary public ownership and delay retirement.
 *
 * Apply order: realize; children in TWO passes (detach missing children only
 * when still natively parented here, then install complete new lists); updates;
 * release. Detached realized layers retain native identity until retirement.
 * No Scene reentry from vtable calls except provider_draw. Read resolved data
 * instead. request_commit schedules work; it must NOT synchronously commit.
 * destroy cancels scheduled work and releases native maps; it must not call
 * Scene APIs or dereference retiring identities. apply is non-reentrant.
 * Native failures are handled/reported by backend;
 * there is no automatic rollback/retry of a partially applied batch.
 */
typedef struct
{
    void (*request_commit)(void *backend);
    void (*apply)(void *backend, const gr_commit_batch *batch);
    void (*destroy)(void *backend);
} gr_backend_vtable;

/** Install once, before first backend apply. All callbacks required. Copies
 * vtable; transfers userdata only on success. Scene invokes destroy exactly
 * once on teardown, including if no commit occurred. Does not call apply here.
 * Backend schedules via a gr_scene_link, never a permanent strong Scene ref. */
GR_API gr_result gr_scene_set_backend(gr_scene *scene, const gr_backend_vtable *vtable, void *backend);

/* Deferred drawing */

/** Owned provider references do not keep Scene/layers/drawers alive. Calls stay
 * on Scene's owning thread. Scene teardown invalidates all providers first. */
GR_API gr_drawer_provider *gr_drawer_provider_ref(gr_drawer_provider *provider);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
GR_API void gr_drawer_provider_unref(gr_drawer_provider *provider);

/** layer must have a valid backend identity lease or an owned live reference.
 * expected_version must equal the CURRENT drawer version; stale/missing/inert
 * targets return GR_ERROR_STATE, *out=NULL. This avoids caching a replacement
 * drawer's output under an older version. Wrong Scene returns GR_ERROR_SCENE.
 * Success produces one owned list. No list is cached by core. Backend may cache
 * its rendering under (layer, version, info) and invoke draw again at any time.
 * Nested draw/commit/mutations while a drawer runs return GR_ERROR_REENTRANT.
 * Property reads and resource recording are permitted during draw.
 * All failure paths set *out=NULL; out is required. */
GR_API gr_result gr_drawer_provider_draw(gr_drawer_provider *provider, gr_layer *layer, uint64_t expected_version,
                                         const gr_draw_info *info, gr_display_list **out);

/** Returns the resolved box in local coordinates and the supplied positive raster scale. */
GR_API gr_draw_info gr_resolved_get_draw_info(const gr_resolved *resolved, double scale);

/** Owned path, grown rounded rect or translated custom path, in box space. */
GR_API gr_path *gr_shadow_shape_new_path(const gr_shadow_shape *shape, gr_point bounds_offset);

/* DisplayList replay */

typedef enum
{
    GR_DRAW_SAVE,
    GR_DRAW_RESTORE,
    GR_DRAW_TRANSFORM,
    GR_DRAW_ALPHA,
    GR_DRAW_CLIP_RECT,
    GR_DRAW_CLIP_PATH,
    GR_DRAW_FILL_PATH,
    GR_DRAW_STROKE_PATH,
    GR_DRAW_IMAGE,
    GR_DRAW_TEXT_LAYOUT,
    GR_DRAW_TEXT_GLYPHS
} gr_draw_op_kind;

typedef struct
{
    gr_draw_op_kind kind;

    union
    {
        gr_affine transform;
        float alpha;
        gr_rect clip_rect;

        struct
        {
            gr_path *path;
            gr_fill_rule rule;
        } clip_path;

        struct
        {
            gr_path *path;
            gr_fill_rule rule;
            gr_paint *paint;
        } fill_path;

        struct
        {
            gr_path *path;
            gr_stroke *stroke;
        } stroke_path;

        struct
        {
            gr_image *image;
            gr_rect destination;
        } image;

        struct
        {
            gr_text_layout *layout;
            gr_point origin; /**< Layout box origin in user coordinates. */
            gr_color color;  /**< Straight-alpha color of runs without their own. */
        } text_layout;

        struct
        {
            gr_text_layout *layout;
            size_t line;
            gr_glyph_range glyphs; /**< Validated against the line's glyph count when recorded. */
            gr_point origin;       /**< Layout box origin in user coordinates. */
            gr_color color;        /**< Straight-alpha color of runs without their own. */
        } text_glyphs;
    } value;
} gr_draw_op;

GR_DECLARE_LIST(gr_draw_op_list, gr_draw_op);

/** Borrowed ops/resources, valid until list's final unref. */
GR_API gr_draw_op_list gr_display_list_get_ops(const gr_display_list *list);

#ifdef __cplusplus
}
#endif

#endif
