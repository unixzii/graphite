/**
 * Graphite API conventions (C11, also usable from C++ and Objective-C).
 *
 * Units: logical points, radians; origin top-left, y down; straight-alpha sRGB.
 * Scene/layer/link operations run on their owning thread without thread checks.
 * Immutable resource ref/unref and reads may cross threads after synchronized
 * publication. Builders and canvases require exclusive access.
 *
 * Opaque handles have stable addresses until final destruction. new returns one
 * owned reference; ref acquires one and unref releases one. A valid owned or
 * borrowed reference is required for every call; reference counting does not
 * validate arbitrary pointers. ref(NULL) returns NULL; unref(NULL) is harmless.
 * Other NULL arguments require explicit permission in the function's contract.
 *
 * Unless documented otherwise, inputs are borrowed for the call. Constructors
 * copy arrays and descriptors; setters copy values and ref stored objects before
 * releasing old ones. Getters return borrowed objects. Immutable array views
 * remain valid while their owner is alive; layer borrows expire on replacement
 * or Scene teardown. Empty arrays allow NULL data, nonempty arrays require data.
 *
 * Allocation failure aborts. Invalid constructor arguments return NULL; other
 * recoverable errors use gr_result. Failed result-returning operations preserve
 * state and ownership. There is no global last-error slot.
 * Unknown enum values and nonfinite numbers are invalid. Sizes, radii, widths
 * and blur sigma are nonnegative; backing/image scales are positive. Opacity
 * and stroke fractions clamp to [0,1]; color components must be in [0,1].
 * Inactive union members and absent optional values are ignored.
 *
 * Never throw/unwind across C callbacks. Cleanup callbacks may release owned
 * objects but must not resurrect an object being destroyed or use expired
 * borrowed argument storage. Scene reentry follows the callback-specific rules.
 */
#ifndef GRAPHITE_TYPES_H
#define GRAPHITE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Exported symbols. Shared libraries hide everything else; GRAPHITE_STATIC is
 * defined for static builds (and their consumers through the CMake package).
 * Each library defines its GRAPHITE_*_BUILDING macro while compiling itself.
 */
#if defined(_WIN32)
#define GR_EXPORT __declspec(dllexport)
#define GR_IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
#define GR_EXPORT __attribute__((visibility("default")))
#define GR_IMPORT __attribute__((visibility("default")))
#else
#define GR_EXPORT
#define GR_IMPORT
#endif

#if defined(GRAPHITE_STATIC)
#define GR_API
#elif defined(GRAPHITE_BUILDING)
#define GR_API GR_EXPORT
#else
#define GR_API GR_IMPORT
#endif

#ifdef __cplusplus
extern "C" {
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
