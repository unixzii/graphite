#ifndef GRAPHITE_DEMO_H
#define GRAPHITE_DEMO_H
#include <graphite/graphite.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define D_PI 3.14159265358979323846
#define D_TAU (2.0 * D_PI)
#define D_BLUE 0x0A84FF
#define D_INDIGO 0x5E5CE6
#define D_PURPLE 0xBF5AF2
#define D_PINK 0xFF375F
#define D_ORANGE 0xFF9F0A
#define D_YELLOW 0xFFD60A
#define D_GREEN 0x30D158
#define D_TEAL 0x64D2FF
#define D_INK 0x1C1C1E
#define D_RECT(x, y, w, h) ((gr_rect){{x, y}, {w, h}})
#define D_POINT(x, y) ((gr_point){x, y})
#define D_WHITE ((gr_color){1, 1, 1, 1})
#define D_BLACK ((gr_color){0, 0, 0, 1})
#define D_OK(call)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((call) != GR_OK)                                                                                           \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #call);                                          \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

typedef enum
{
    DEMO_SHOWCASE,
    DEMO_UI
} demo_kind;
typedef struct demo demo;

struct demo
{
    gr_scene *scene;
    demo_kind kind;
    double time;
    gr_layer **layers;
    size_t layer_count, layer_capacity;
    gr_layer *hour, *minute, *second, *orbit, *planets[6], *moon_orbit;
    gr_layer *drawing, *rings[3], *blob, *star, *compound;
    gr_layer *spotlight, *badge, *badge_mask;
    gr_layer *phone, *indicator, *morph, *leaf, *bar;
    gr_layer *fan, *photos[4], *group[2], *single[2], *dot;
    size_t front;
    gr_layer *switch_track, *switch_knob, *slider_fill, *slider_knob, *caret, *spinner, *progress;
    gr_image *drawing_picture;
};

#ifdef __cplusplus
extern "C"
{
#endif

    /** Creates the selected scene and poses it at time zero. Owns all animation state. */
    demo *demo_new(demo_kind kind);
    /** Updates the scene to time in seconds; called on the Scene thread. */
    void demo_update(demo *d, double time);
    /** Destroys Scene before releasing callback state and external layer handles. */
    void demo_free(demo *d);
    /** Returns the scene's logical window size. */
    gr_size demo_size(demo_kind kind);
    /** Runs the native window/event loop, optionally testing 60 ticks or reporting FPS. */
    int demo_platform_run(demo *d, bool selftest, bool fps);
    /** Renders at 1x and writes raw premultiplied RGBA8 PNG pixels. */
    bool demo_platform_snapshot(demo *d, const char *path);

    /* Scene construction helpers return borrowed layers held by demo. */
    gr_color hex(uint32_t rgb);
    gr_color alpha(gr_color color, float value);
    gr_layer *new_layer(demo *d, gr_layer_kind kind);
    gr_layer *layer(demo *d, gr_layer *parent, gr_rect rect);
    gr_layer *pill(demo *d, gr_layer *parent, gr_rect rect, gr_color color, double radius);
    gr_layer *shape(demo *d, gr_layer *parent, gr_rect rect);
    void background(gr_layer *l, gr_color color);
    void border(gr_layer *l, double width, gr_color color);
    void demo_shadow(gr_layer *l, gr_color color, double x, double y, double sigma, gr_path *path);
    void fill(gr_layer *l, gr_color color);
    void set_path(gr_layer *l, gr_path *owned);
    void set_image(gr_layer *l, gr_image *borrowed);
    void set_gradient(gr_layer *l, gr_paint *owned);
    gr_paint *paint(gr_color color);
    gr_paint *gradient(bool radial, gr_point start, gr_point end, double radius, gr_gradient_stop_list stops);
    gr_stroke *stroke(gr_paint *owned, double width, gr_line_cap cap, gr_line_join join, gr_double_list dash,
                      double phase);
    void shape_stroke(gr_layer *l, gr_stroke *owned);
    void draw_fill(gr_canvas *c, gr_path *owned, gr_fill_rule rule, gr_paint *owned_paint);
    void draw_stroke(gr_canvas *c, gr_path *owned, gr_stroke *owned_stroke);
    void draw_rect(gr_canvas *c, gr_rect rect, gr_color color);
    gr_path *circle(gr_point center, double radius);
    gr_path *rounded(gr_rect rect, double radius, gr_corners corners);
    gr_path *star_path(gr_point center, double outer, double inner, size_t points, double rotation);
    gr_path *star_polygon(gr_point center, double radius, size_t points, size_t skip, double rotation);
    void move_to(gr_path_builder *b, gr_point p);
    void line_to(gr_path_builder *b, gr_point p);
    void curve_to(gr_path_builder *b, gr_point a, gr_point c, gr_point p);
    void close_path(gr_path_builder *b);
    gr_path *finish_path(gr_path_builder *owned);
    gr_affine rotation(double angle);
    gr_affine scaling(double value);
    gr_affine translation(gr_point p);
    double smooth(double x);
    double wave(double t, double period, double offset);
    gr_image *landscape(uint32_t width, uint32_t height, double scale);
    gr_image *checker(uint32_t a, uint32_t b);
    void showcase_build(demo *d);
    void showcase_update(demo *d, double t);
    void ui_build(demo *d);
    void ui_update(demo *d, double t);
    void clock_build(demo *d, gr_layer *card);
    void clock_update(demo *d, double t);
    void drawing_build(demo *d, gr_layer *card);
    void shapes_build(demo *d, gr_layer *card);
    void shapes_update(demo *d, double t);
    void masks_build(demo *d, gr_layer *card);
    void masks_update(demo *d, double t);
    void clipping_build(demo *d, gr_layer *card);
    void clipping_update(demo *d, double t);
    void compositing_build(demo *d, gr_layer *card);
    void compositing_update(demo *d, double t);
#define CARD_WIDTH 288.0
#define CARD_HEIGHT 284.0
#ifdef __cplusplus
}
#endif
#endif
