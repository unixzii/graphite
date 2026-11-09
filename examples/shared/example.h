#ifndef GRAPHITE_EXAMPLE_H
#define GRAPHITE_EXAMPLE_H

#include <graphite/graphite.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EX_PI 3.14159265358979323846
#define EX_TAU (2.0 * EX_PI)
#define EX_BLUE 0x0A84FF
#define EX_INDIGO 0x5E5CE6
#define EX_PURPLE 0xBF5AF2
#define EX_PINK 0xFF375F
#define EX_ORANGE 0xFF9F0A
#define EX_YELLOW 0xFFD60A
#define EX_GREEN 0x30D158
#define EX_TEAL 0x64D2FF
#define EX_INK 0x1C1C1E
#define EX_RECT(x, y, w, h) ((gr_rect){{x, y}, {w, h}})
#define EX_POINT(x, y) ((gr_point){x, y})
#define EX_WHITE ((gr_color){1, 1, 1, 1})
#define EX_BLACK ((gr_color){0, 0, 0, 1})
#define EX_OK(call)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if ((call) != GR_OK)                                                                                           \
        {                                                                                                              \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #call);                                          \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

typedef struct example example;

/** Describes one example scene; each example executable passes its own to example_main. */
typedef struct
{
    const char *name; /**< Short name, used in the window title and usage text. */
    gr_size size;     /**< Logical window size. */
    /** Builds layers under the Scene root and returns scene state (may be NULL). */
    void *(*build)(example *ex);
    /** Poses the scene at time in seconds; called on the Scene thread. */
    void (*update)(void *state, double time);
    /** Releases scene state after the Scene and every tracked layer have been released. May be NULL. */
    void (*free)(void *state);
    /** Reflects whether the window has keyboard focus; called on the Scene thread. Scenes start focused, which is
     * also how snapshots render. May be NULL. */
    void (*focus)(void *state, bool focused);
    /** Handles pointer, scroll and key input; returns whether it was consumed (false lets the platform have it).
     * Called on the Scene thread; ex->host can set the cursor. May be NULL. */
    bool (*input)(void *state, const gr_event *event);
    /** Reports the window's size in logical points once it is known and whenever it changes; called on the Scene
     * thread. The root keeps the size it was built with unless the scene resizes it. May be NULL. */
    void (*viewport)(void *state, gr_size size);
    /** Returns whether the scene is animating and needs another frame. Windows request frames only while it does,
     * and ask again after input, focus, viewport and timer events. NULL animates continuously. */
    bool (*animating)(void *state);
} example_desc;

struct example
{
    const example_desc *desc;
    gr_scene *scene;
    gr_text_system *text; /**< NULL where the platform has no text system yet. */
    gr_scene_host *host;  /**< Borrowed interactive host while a window runs; NULL for snapshots. */
    void *state;
    gr_layer **layers;
    size_t layer_count, layer_capacity;
};

#ifdef __cplusplus
extern "C" {
#endif

/** Parses the shared command line, then runs the window, selftest or snapshot for desc. */
int example_main(int argc, char **argv, const example_desc *desc);

/** Creates the Scene, builds it and poses it at time zero. Refs text, which may be NULL; scenes then draw
 * placeholder bars instead of text. */
example *example_new(const example_desc *desc, gr_text_system *text);
/** Updates the scene to time in seconds; called on the Scene thread. */
void example_update(example *ex, double time);
/** Tells the scene whether the window has keyboard focus; called on the Scene thread. */
void example_focus(example *ex, bool focused);
/** Passes an input event to the scene; returns whether it was consumed. */
bool example_input(example *ex, const gr_event *event);
/** Returns whether the scene needs another frame. */
bool example_animating(example *ex);
/** Tells the scene the window's size in logical points. */
void example_viewport(example *ex, gr_size size);
/** Destroys the Scene before releasing tracked layers and scene state. */
void example_free(example *ex);

/* Construction helpers return borrowed layers held by ex until example_free. */
gr_layer *new_layer(example *ex, gr_layer_kind kind);
gr_layer *layer(example *ex, gr_layer *parent, gr_rect rect);
gr_layer *pill(example *ex, gr_layer *parent, gr_rect rect, gr_color color, double radius);
gr_layer *shape(example *ex, gr_layer *parent, gr_rect rect);

gr_color hex(uint32_t rgb);
gr_color alpha(gr_color color, float value);
void background(gr_layer *l, gr_color color);
void border(gr_layer *l, double width, gr_color color);
void drop_shadow(gr_layer *l, gr_color color, double x, double y, double sigma, gr_path *path);
void fill(gr_layer *l, gr_color color);
void set_path(gr_layer *l, gr_path *owned);
void set_image(gr_layer *l, gr_image *borrowed);
void set_gradient(gr_layer *l, gr_paint *owned);
gr_paint *paint(gr_color color);
gr_paint *gradient(bool radial, gr_point start, gr_point end, double radius, gr_gradient_stop_list stops);
gr_stroke *stroke(gr_paint *owned, double width, gr_line_cap cap, gr_line_join join, gr_double_list dash, double phase);
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

#ifdef __cplusplus
}
#endif

#endif
