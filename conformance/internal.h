#ifndef GRAPHITE_CONFORMANCE_INTERNAL_H
#define GRAPHITE_CONFORMANCE_INTERNAL_H

#include "conformance.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint8_t v[4];
} rgba8;

typedef struct
{
    gr_conf_pixels pixels;
    double scale;
} frame;

typedef struct
{
    double distance, channels[4];
} sample;

typedef struct
{
    sample *data;
    size_t count;
} profile;

typedef struct
{
    gr_scene *scene;
    void *capture;
} host;

typedef struct
{
    const gr_conf_harness *harness;
    const char *name;
    double scale;
    host *hosts;
    size_t host_count, host_capacity;
    gr_scene **scenes;
    size_t scene_count, scene_capacity;
    gr_layer **layers;
    size_t layer_count, layer_capacity;
    frame **frames;
    size_t frame_count, frame_capacity;
    gr_text_system *text;
    size_t failures;
    bool skipped;
} ctx;

typedef struct
{
    const char *name, *tier;
    void (*run)(ctx *);
} conf_case;

extern const conf_case gr_conf_cases[];
extern const size_t gr_conf_case_count;

#define RED ((rgba8){{255, 0, 0, 255}})
#define BLUE ((rgba8){{0, 0, 255, 255}})
#define WHITE ((rgba8){{255, 255, 255, 255}})
#define BLACK ((rgba8){{0, 0, 0, 255}})
#define CLEAR ((rgba8){{0, 0, 0, 0}})
#define POINT(x, y) ((gr_point){(x), (y)})
#define RECT(x, y, w, h) ((gr_rect){{(x), (y)}, {(w), (h)}})
#define API(call)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((call) != GR_OK)                                                                                           \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #call);                                          \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

void context_clear(ctx *c);
void conf_check(ctx *c, bool ok, const char *format, ...);
void expect_color(ctx *c, const frame *f, const char *label, gr_point point, rgba8 expected, unsigned tolerance);
void expect_near(ctx *c, const char *label, double measured, double expected, double tolerance);
bool frame_at(const frame *f, gr_point point, rgba8 *out);
profile frame_profile(const frame *f, gr_point from, gr_point to);
double profile_crossing(profile p, size_t channel, double threshold);
double profile_coverage(profile p, size_t channel, double scale);
gr_scene *new_scene(ctx *c);
/** Returns the borrowed text system of the harness, or NULL after marking the case skipped. */
gr_text_system *text_system(ctx *c);
/** Returns whether the harness supports a GR_CONF_* feature; otherwise marks the case skipped and returns false. */
bool supports(ctx *c, uint32_t feature);
gr_layer *new_layer(ctx *c, gr_scene *s, gr_layer_kind kind);
gr_color color(rgba8 value);
gr_layer *solid(ctx *c, gr_scene *s, gr_rect rect, rgba8 value);
frame *new_frame(ctx *c);
frame *render(ctx *c, gr_scene *s);
gr_path *rectangle_path(gr_rect rect);
gr_path *line_path(gr_point from, gr_point to);
void set_fill(gr_layer *layer, rgba8 value);
void set_stroke(gr_layer *layer, rgba8 value, double width);
void canvas_fill(gr_canvas *canvas, gr_rect rect, rgba8 value);
void set_gradient(gr_layer *layer, bool radial, gr_point start, gr_point end, double radius);
void set_shadow(gr_layer *layer, double x, double y, double sigma);

#endif
