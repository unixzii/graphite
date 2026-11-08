#ifndef GRAPHITE_WEB_INTERNAL_H
#define GRAPHITE_WEB_INTERNAL_H

#include <graphite/backend.h>
#include <graphite/web.h>

#include <stddef.h>
#include <stdint.h>

/* Layer identities and pixel pointers cross into JavaScript as numbers. */
_Static_assert(sizeof(void *) == 4, "the Web backend targets wasm32");

/* apply sends the runtime a stream of doubles: an opcode, then its operands. Layer identities are their pointers,
 * valid as keys until their RELEASE (see the identity lease in backend.h). Commands follow the commit contract's
 * order: ROOT, REALIZE, CHILDREN, the property commands, then RELEASE.
 *
 * Operand groups used below:
 *   color    r g b a (straight alpha)
 *   path     count, then per element: verb (gr_path_verb) and its points (MOVE/LINE 1, QUAD 2, CUBIC 3, CLOSE 0)
 *   paint    kind (0 none, 1 solid, 2 linear, 3 radial), then
 *            solid: color; linear: x1 y1 x2 y2 stops; radial: cx cy radius stops; stops: count, (offset color)...
 *   stroke   has; if set: width cap join miter_limit dash_count dashes... dash_phase paint
 *   image    identity width height pixels (a pointer to premultiplied RGBA8, valid during the call)
 */
enum
{
    GR_WEB_ROOT = 1,       /* layer */
    GR_WEB_REALIZE = 2,    /* layer kind */
    GR_WEB_CHILDREN = 3,   /* parent count children... */
    GR_WEB_RELEASE = 4,    /* layer */
    GR_WEB_GEOMETRY = 5,   /* layer width height a b c d tx ty bounds_offset_x bounds_offset_y */
    GR_WEB_APPEARANCE = 6, /* layer opacity hidden masks_to_bounds radius corners has_background color has_border
                            * width color blur */
    GR_WEB_SHADOW = 7,     /* layer has; if set: color offset_x offset_y sigma path (box space) */
    GR_WEB_CONTENT = 8,    /* layer kind (gr_content_kind), then
                            * IMAGE: image x y width height (box space)
                            * DRAWN: draw ops until GR_WEB_END (see backend.c)
                            * SHAPE: path? (has, path) paint fill_rule stroke stroke_start stroke_end
                            * GRADIENT: paint */
    GR_WEB_MASK = 9        /* layer kind (0 none, 1 path, 2 gradient), then
                            * path: even_odd path (box space); gradient: paint (unit space over the box) */
};

/* Text draw ops (in DRAWN content), for layouts of the Web text system:
 *   GR_DRAW_TEXT_LAYOUT: layout origin_x origin_y color
 *   GR_DRAW_TEXT_GLYPHS: layout origin_x origin_y color line glyph_start glyph_end */

#define GR_WEB_END (-1.0)

typedef struct gr_web_backend gr_web_backend;
typedef struct gr_web_host gr_web_host;

/* runtime.js. Host handles are positive; 0 means none. host_new resolves target as gr_web_attach_to_element
 * documents. The runtime keeps one animation-frame tick per host: request_tick schedules it, and it calls
 * gr_web_tick with the backend and the event host it was given. host_free cancels it. */
int32_t gr_web_js_host_new(const char *target);
void gr_web_js_host_free(int32_t host);
void gr_web_js_host_backend(int32_t host, gr_web_backend *backend);
/* Starts delivering events to events, or stops with NULL; starting reports the viewport synchronously. */
void gr_web_js_host_events(int32_t host, gr_web_host *events);
double gr_web_js_device_pixel_ratio(void);
double gr_web_js_now(void);
void gr_web_js_request_tick(int32_t host);
void gr_web_js_set_cursor(int32_t host, gr_cursor cursor);
void gr_web_js_request_focus(int32_t host);
void gr_web_js_apply(int32_t host, const double *commands, size_t count);

/* Exported to runtime.js: one animation frame. Delivers a requested frame to events (NULL without an event host),
 * then commits if the backend asked for it. */
void gr_web_tick(gr_web_backend *backend, gr_web_host *events, double time, double interval);
void gr_web_host_frame(gr_web_host *host, double time, double interval);

/* Exported to runtime.js: input and state reports for an event host; see host.c. */
void gr_web_on_viewport(gr_web_host *host, double width, double height, double scale);
void gr_web_on_focus(gr_web_host *host, bool focused, double time);
bool gr_web_on_pointer(gr_web_host *host, int type, double time, uint32_t modifiers, uint32_t id, int kind, double x,
                       double y, int button, uint32_t buttons, uint32_t clicks, double pressure);
bool gr_web_on_scroll(gr_web_host *host, double time, uint32_t modifiers, double x, double y, double dx, double dy,
                      int unit);
bool gr_web_on_key(gr_web_host *host, bool down, double time, uint32_t modifiers, int code, int key, uint32_t character,
                   bool repeat, bool composing, uint32_t native_code);

/* text.js: layouts made by the Web text system. text_new takes the UTF-8 text and the operands text.c encodes; the
 * queries write their results into out as numbers (see text.c for the layouts). */
int32_t gr_web_js_text_new(const char *utf8, size_t length, const double *operands, size_t count);
void gr_web_js_text_free(int32_t layout);
void gr_web_js_text_metrics(int32_t layout, double *out);
bool gr_web_js_text_truncated(int32_t layout);
void gr_web_js_text_line(int32_t layout, size_t index, double *out);
double gr_web_js_text_caret_stop(int32_t layout, size_t index, bool next);
void gr_web_js_text_hit_test(int32_t layout, double x, double y, double *out);
void gr_web_js_text_caret(int32_t layout, size_t index, int affinity, double *out);
double gr_web_js_text_range_rects(int32_t layout, size_t start, size_t end, double *out, size_t capacity);
double gr_web_js_text_glyphs(int32_t layout, size_t line, double *out, size_t capacity);

extern const gr_text_layout_vtable gr_web_layout_vtable;
/* Returns the runtime handle of a layout made by the Web text system, or 0 for other layouts. */
int32_t gr_web_layout_id(const gr_text_layout *layout);

/* Installs the backend on scene, taking ownership of host; on failure frees host and returns false. */
bool gr_web_install(gr_scene *scene, int32_t host);

#endif
