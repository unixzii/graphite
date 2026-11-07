/** Graphite resource API. */
#ifndef GRAPHITE_RESOURCE_H
#define GRAPHITE_RESOURCE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Paints and strokes */

typedef struct
{
    float offset;
    gr_color color;
} gr_gradient_stop;

GR_DECLARE_LIST(gr_gradient_stop_list, gr_gradient_stop);

typedef enum
{
    GR_PAINT_SOLID,
    GR_PAINT_LINEAR,
    GR_PAINT_RADIAL
} gr_paint_kind;

typedef struct
{
    gr_paint_kind kind;

    union
    {
        gr_color solid;

        struct
        {
            gr_point start, end;
            gr_gradient_stop_list stops;
        } linear;

        struct
        {
            gr_point center;
            double radius;
            gr_gradient_stop_list stops;
        } radial;
    } value;
} gr_paint_desc;

typedef struct
{
    double width;
    gr_line_cap cap;
    gr_line_join join;
    double miter_limit;
    gr_double_list dash;
    double dash_phase;
} gr_stroke_style;

/** Returns width 1, butt caps, miter joins/limit 10, and an empty dash pattern. */
gr_stroke_style gr_stroke_style_default(void);

/** Constructors copy arrays. NULL arrays are valid only with count == 0.
 * Gradient stops: >= 2, finite, nondecreasing offsets within [0,1].
 * Dash entries: finite, nonnegative, and not all zero for a nonempty pattern. */
gr_paint *gr_paint_new(const gr_paint_desc *desc);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_paint *gr_paint_ref(gr_paint *paint);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
void gr_paint_unref(gr_paint *paint);

/** Returns the paint description; stop views borrow storage until the paint is finally unref'd. */
gr_paint_desc gr_paint_get_desc(const gr_paint *paint);

/** Creates an owned stroke, refs paint and copies dash values; returns NULL for invalid style. */
gr_stroke *gr_stroke_new(gr_paint *paint, const gr_stroke_style *style);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_stroke *gr_stroke_ref(gr_stroke *stroke);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
void gr_stroke_unref(gr_stroke *stroke);

/** Returns the borrowed paint, valid while the stroke remains alive. */
gr_paint *gr_stroke_get_paint(const gr_stroke *stroke);

/** Returns the stroke style; the dash view borrows storage from the stroke. */
gr_stroke_style gr_stroke_get_style(const gr_stroke *stroke);

/* Paths and path construction */

typedef enum
{
    GR_PATH_MOVE,
    GR_PATH_LINE,
    GR_PATH_QUAD,
    GR_PATH_CUBIC,
    GR_PATH_CLOSE
} gr_path_verb;

/** MOVE/LINE use p[0]; QUAD uses control p[0], end p[1]; CUBIC uses all three. */
typedef struct
{
    gr_path_verb verb;
    gr_point p[3];
} gr_path_element;

GR_DECLARE_LIST(gr_path_element_list, gr_path_element);

/** Creates an owned empty path builder for exclusive access. */
gr_path_builder *gr_path_builder_new(void);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_path_builder *gr_path_builder_ref(gr_path_builder *builder);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
void gr_path_builder_unref(gr_path_builder *builder);

/** Copies path elements into the builder; invalid verbs/coordinates leave it unchanged. */
gr_result gr_path_builder_append(gr_path_builder *builder, gr_path_element_list elements);

/** Appends a closed rectangle; invalid geometry leaves the builder unchanged. */
gr_result gr_path_builder_add_rect(gr_path_builder *builder, gr_rect rect);

/** Appends a closed ellipse inscribed in rect; invalid geometry leaves the builder unchanged. */
gr_result gr_path_builder_add_ellipse(gr_path_builder *builder, gr_rect rect);

/** Appends a rounded rectangle; radius is clamped to half the shorter side. */
gr_result gr_path_builder_add_rounded_rect(gr_path_builder *builder, gr_rect rect, double radius, gr_corners corners);

/** Returns an owned immutable snapshot, resets builder to empty; does not unref it. */
gr_path *gr_path_builder_finish(gr_path_builder *builder);

/** Creates an owned immutable copy of elements; an empty view creates an empty path. */
gr_path *gr_path_new(gr_path_element_list elements);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_path *gr_path_ref(gr_path *path);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
void gr_path_unref(gr_path *path);

/** Returns a borrowed element view valid until the path is finally unref'd. */
gr_path_element_list gr_path_get_elements(const gr_path *path);

/** Returns the immutable resource identity, unique for the process lifetime and never reused. */
uint64_t gr_path_get_id(const gr_path *path);

/* Images */

/** Copies tightly packed premultiplied RGBA8, pixels.count == width*height*4;
 * overflow checked; scale finite and > 0. */
gr_image *gr_image_new(uint32_t width, uint32_t height, double scale, gr_bytes pixels);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_image *gr_image_ref(gr_image *image);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
void gr_image_unref(gr_image *image);

/** Returns the image width in pixels. */
uint32_t gr_image_get_width(const gr_image *image);

/** Returns the image height in pixels. */
uint32_t gr_image_get_height(const gr_image *image);

/** Returns pixels per logical point. */
double gr_image_get_scale(const gr_image *image);

/** Returns borrowed tightly packed RGBA8 bytes, valid while the image remains alive. */
gr_bytes gr_image_get_pixels(const gr_image *image);

/** Returns the immutable resource identity, unique for the process lifetime and never reused. */
uint64_t gr_image_get_id(const gr_image *image);

#ifdef __cplusplus
}
#endif

#endif
