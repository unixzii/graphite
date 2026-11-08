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

/** Read-only property snapshots in commit updates. Pointer members borrow
 * resources from the batch; copy values and ref resources to retain them.
 * Applications change individual properties through the layer API. */
typedef struct
{
    gr_rect bounds;
    gr_point position, anchor_point;
    gr_affine transform;
    float opacity;
    gr_corners masked_corners;
    bool hidden, masks_to_bounds, has_background, has_border, has_shadow, has_mask;
    gr_color background;
    double corner_radius;
    gr_border border;
    gr_shadow shadow;
    gr_mask mask;
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

/** A layer as backends apply it: its properties plus what core derives from them, so every backend interprets the
 * model identically. Box space runs from (0, 0) to props.bounds.size and holds the background, border, image
 * placement, clip and default shadow. Local space is box space offset by props.bounds.origin and holds sublayers,
 * paths, drawings, path masks and custom shadows. Pointer members borrow from the batch. */
typedef struct
{
    gr_layer_props props; /**< props.bounds and props.border.width are snapped to device pixels. */
    gr_affine transform;  /**< Snapped box-to-parent-local transform; identity for the root. */
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

/* Scene hosts */

/** A platform host behind the common gr_scene_host handle. Platform host functions recognize their hosts by the
 * vtable address passed to gr_scene_host_new, so pass a static vtable. destroy releases the platform host on final
 * release, after the event handler; it must not call Scene APIs or use the host.
 *
 * An interactive host sets every event callback; a host without an input surface leaves them all NULL. Core
 * validates arguments, coalesces frame requests and calls these on the Scene's owning thread:
 * - request_frame schedules one GR_EVENT_FRAME for the next display refresh, delivered with
 *   gr_scene_host_dispatch_event before that frame's commit point. Core calls it once per pending request.
 * - now returns the frame clock's current time in seconds.
 * - set_cursor, set_text_input (NULL disables) and request_focus apply to the native surface. */
typedef struct
{
    void (*destroy)(void *data);
    void (*request_frame)(void *data);
    double (*now)(void *data);
    void (*set_cursor)(void *data, gr_cursor cursor);
    void (*set_text_input)(void *data, const gr_text_input *input);
    void (*request_focus)(void *data);
} gr_scene_host_vtable;

/** Creates an owned host of scene. Copies the vtable and takes ownership of data on success; returns NULL and leaves
 * data with the caller for missing arguments, a missing destroy or a partial set of event callbacks. Holds a
 * gr_scene_link, not the Scene. */
GR_API gr_scene_host *gr_scene_host_new(gr_scene *scene, const gr_scene_host_vtable *vtable, void *data);

/** Returns the vtable address passed to gr_scene_host_new. */
GR_API const gr_scene_host_vtable *gr_scene_host_get_vtable(const gr_scene_host *host);

/** Returns the borrowed platform data passed to gr_scene_host_new, valid while the host is alive. */
GR_API void *gr_scene_host_get_data(const gr_scene_host *host);

/** Delivers event to the handler and returns whether it consumed it; event storage is borrowed for the call.
 * Events must be valid (finite values, known enums, in-range fields); builds without NDEBUG check this and drop
 * invalid ones, release builds trust the backend. Returns false without delivery for non-interactive hosts, a
 * missing handler, FRAME events that were not requested, and VIEWPORT/FOCUS events that repeat the current state,
 * so platforms may report state liberally. VIEWPORT and FOCUS update the state that gr_scene_host_get_viewport and
 * gr_scene_host_has_focus return even without a handler. FRAME clears the pending request before delivery, so the
 * handler can request the next one. Events dispatched while a handler runs (for example focus changes the platform
 * sends synchronously) are copied, delivered in order after it returns, and report false. Holds a host reference until
 * delivery ends. */
GR_API bool gr_scene_host_dispatch_event(gr_scene_host *host, const gr_event *event);

/** Returns whether a frame was requested and not yet delivered. */
GR_API bool gr_scene_host_frame_requested(const gr_scene_host *host);

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

/** Returns an owned box-space outline of the shadow: the rounded box, or the custom path moved out of local space.
 * Returns NULL without a shadow. */
GR_API gr_path *gr_resolved_new_shadow_path(const gr_resolved *resolved);

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
