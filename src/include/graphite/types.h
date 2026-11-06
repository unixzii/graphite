/** Units: logical points, radians; origin top-left, y down; straight-alpha sRGB.
 * Scene/layer/link operations: owning thread only, without thread checks.
 * Immutable resources: ref/unref and reads may cross threads after publication.
 * Builders/canvases: exclusive access. Never throw/unwind through C callbacks.
 * Unless documented otherwise: pointers are required, inputs are borrowed for
 * the call, setters copy values and ref stored objects, getters borrow objects.
 * ref(NULL) returns NULL; unref(NULL) is harmless. Other NULLs require explicit
 * permission. Borrowed arrays live until their immutable owner is unref'd.
 * OOM aborts; constructors return NULL for invalid arguments. Result-returning
 * operations leave state/ownership unchanged on failure. No global last-error.
 */
#ifndef GRAPHITE_TYPES_H
#define GRAPHITE_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Defines a borrowed, read-only contiguous list view with data and count.
 * This type owns neither its storage nor references to its elements. NULL data
 * is valid only when count is zero. The API returning/accepting a view specifies
 * its lifetime. For pointer elements, the slots are const, not the pointees.
 */
#define GR_DECLARE_LIST(name, element_type)                                                                            \
    typedef struct                                                                                                     \
    {                                                                                                                  \
        element_type const *data;                                                                                      \
        size_t count;                                                                                                  \
    } name

    GR_DECLARE_LIST(gr_bytes, uint8_t);

    GR_DECLARE_LIST(gr_double_list, double);

    typedef struct gr_scene gr_scene;
    typedef struct gr_scene_link gr_scene_link;
    typedef struct gr_layer gr_layer;
    typedef struct gr_path gr_path;
    typedef struct gr_path_builder gr_path_builder;
    typedef struct gr_image gr_image;
    typedef struct gr_paint gr_paint;
    typedef struct gr_stroke gr_stroke;
    typedef struct gr_canvas gr_canvas;
    typedef struct gr_display_list gr_display_list;

    GR_DECLARE_LIST(gr_layer_list, gr_layer *);

    typedef enum
    {
        GR_OK,
        GR_ERROR_ARGUMENT,
        GR_ERROR_KIND,
        GR_ERROR_SCENE,
        GR_ERROR_CYCLE,
        GR_ERROR_STATE,
        GR_ERROR_REENTRANT
    } gr_result;

    typedef struct
    {
        double x, y;
    } gr_point;

    typedef struct
    {
        double width, height;
    } gr_size;

    typedef struct
    {
        gr_point origin;
        gr_size size;
    } gr_rect;

    /** x' = a*x + c*y + tx; y' = b*x + d*y + ty. */
    typedef struct
    {
        double a, b, c, d, tx, ty;
    } gr_affine;

    typedef struct
    {
        float r, g, b, a;
    } gr_color;

    typedef uint32_t gr_corners;

    enum
    {
        GR_CORNER_TOP_LEFT = 1u,
        GR_CORNER_TOP_RIGHT = 2u,
        GR_CORNER_BOTTOM_LEFT = 4u,
        GR_CORNER_BOTTOM_RIGHT = 8u,
        GR_CORNERS_ALL = 15u
    };

    typedef enum
    {
        GR_LAYER_CONTAINER,
        GR_LAYER_CONTENT,
        GR_LAYER_SHAPE,
        GR_LAYER_GRADIENT
    } gr_layer_kind;

    typedef enum
    {
        GR_FILL_NONZERO,
        GR_FILL_EVEN_ODD
    } gr_fill_rule;

    typedef enum
    {
        GR_CAP_BUTT,
        GR_CAP_ROUND,
        GR_CAP_SQUARE
    } gr_line_cap;

    typedef enum
    {
        GR_JOIN_MITER,
        GR_JOIN_ROUND,
        GR_JOIN_BEVEL
    } gr_line_join;

    typedef enum
    {
        GR_GRAVITY_CENTER,
        GR_GRAVITY_TOP,
        GR_GRAVITY_BOTTOM,
        GR_GRAVITY_LEFT,
        GR_GRAVITY_RIGHT,
        GR_GRAVITY_TOP_LEFT,
        GR_GRAVITY_TOP_RIGHT,
        GR_GRAVITY_BOTTOM_LEFT,
        GR_GRAVITY_BOTTOM_RIGHT,
        GR_GRAVITY_RESIZE,
        GR_GRAVITY_ASPECT,
        GR_GRAVITY_ASPECT_FILL
    } gr_gravity;

#ifdef __cplusplus
}
#endif
#endif
