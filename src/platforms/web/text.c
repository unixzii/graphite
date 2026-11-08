#include "internal.h"

#include <stdlib.h>

/* The Web text system lays text out with the page's own text stack in text.js; this file only converts between the
 * core's types and the runtime's number arrays. Every callback runs on the main thread (see web.h). */

typedef struct
{
    gr_text_system *handle; /**< Borrowed: the system owns this data. */
} web_system;

typedef struct
{
    int32_t id; /**< The runtime's layout handle. */
} web_layout;

static int32_t id_of(void *layout)
{
    return ((web_layout *)layout)->id;
}

static gr_text_layout_metrics get_metrics(void *layout)
{
    double v[6];
    gr_web_js_text_metrics(id_of(layout), v);
    return (gr_text_layout_metrics){
        .size = {v[0], v[1]},
        .line_count = (size_t)v[2],
        .first_baseline = v[3],
        .last_baseline = v[4],
        .visible = {0, (size_t)v[5]},
        .truncated = gr_web_js_text_truncated(id_of(layout)),
    };
}

static void get_line(void *layout, size_t index, gr_text_line *out)
{
    double v[9];
    gr_web_js_text_line(id_of(layout), index, v);
    *out = (gr_text_line){
        .range = {(size_t)v[0], (size_t)v[1]},
        .bounds = {{v[2], v[3]}, {v[4], v[5]}},
        .baseline = v[6],
        .ascent = v[7],
        .descent = v[8],
    };
}

static size_t next_caret_stop(void *layout, size_t index)
{
    return (size_t)gr_web_js_text_caret_stop(id_of(layout), index, true);
}

static size_t previous_caret_stop(void *layout, size_t index)
{
    return (size_t)gr_web_js_text_caret_stop(id_of(layout), index, false);
}

static gr_text_position hit_test(void *layout, gr_point point)
{
    double v[2];
    gr_web_js_text_hit_test(id_of(layout), point.x, point.y, v);
    return (gr_text_position){(size_t)v[0], (gr_text_affinity)v[1]};
}

static gr_rect get_caret_rect(void *layout, gr_text_position position)
{
    double v[4];
    gr_web_js_text_caret(id_of(layout), position.index, position.affinity, v);
    return (gr_rect){{v[0], v[1]}, {v[2], v[3]}};
}

static size_t get_range_rects(void *layout, gr_text_range range, gr_rect *rects, size_t capacity)
{
    double *v = capacity ? malloc(capacity * 4 * sizeof(double)) : NULL;
    if (capacity && !v)
        abort();
    size_t count = (size_t)gr_web_js_text_range_rects(id_of(layout), range.start, range.end, v, capacity);
    for (size_t i = 0; i < capacity && i < count; ++i)
        rects[i] = (gr_rect){{v[i * 4], v[i * 4 + 1]}, {v[i * 4 + 2], v[i * 4 + 3]}};
    free(v);
    return count;
}

enum
{
    GLYPH_WORDS = 13
};

static size_t get_glyphs(void *layout, size_t line, gr_text_glyph *glyphs, size_t capacity)
{
    double *v = capacity ? malloc(capacity * GLYPH_WORDS * sizeof(double)) : NULL;
    if (capacity && !v)
        abort();
    size_t count = (size_t)gr_web_js_text_glyphs(id_of(layout), line, v, capacity);
    for (size_t i = 0; i < capacity && i < count; ++i)
    {
        const double *g = v + i * GLYPH_WORDS;
        glyphs[i] = (gr_text_glyph){
            .position = {g[0], g[1]},
            .advance = g[2],
            .bounds = {{g[3], g[4]}, {g[5], g[6]}},
            .cluster = {(size_t)g[7], (size_t)g[8]},
            .flags = (gr_text_glyph_flags)g[9],
            .font = (uint64_t)g[10],
            .font_size = g[11],
            .glyph = (uint32_t)g[12],
        };
    }
    free(v);
    return count;
}

static void destroy_layout(void *layout)
{
    gr_web_js_text_free(id_of(layout));
    free(layout);
}

const gr_text_layout_vtable gr_web_layout_vtable = {
    get_metrics,    get_line,        next_caret_stop, previous_caret_stop, hit_test,
    get_caret_rect, get_range_rects, get_glyphs,      destroy_layout,
};

int32_t gr_web_layout_id(const gr_text_layout *layout)
{
    return gr_text_layout_get_vtable(layout) == &gr_web_layout_vtable ? id_of(gr_text_layout_get_data(layout)) : 0;
}

/* Style operands: size weight slant decorations letter_spacing family_pointer family_length, then for spans the
 * color r g b a. */
static void push_style(double *v, size_t *n, const gr_text_style *s)
{
    v[(*n)++] = s->size;
    v[(*n)++] = s->weight;
    v[(*n)++] = s->slant;
    v[(*n)++] = s->decorations;
    v[(*n)++] = s->letter_spacing;
    v[(*n)++] = (double)(uintptr_t)s->family.data;
    v[(*n)++] = (double)s->family.count;
}

static gr_text_layout *layout_text(void *data, gr_attributed_text *text, const gr_text_layout_options *o)
{
    web_system *system = data;
    gr_string utf8 = gr_attributed_text_get_utf8(text);
    gr_text_style base = gr_attributed_text_get_base(text);
    gr_text_span_list spans = gr_attributed_text_get_spans(text);
    /* options (7), base (7), span count, then per span: start end fields style (7) color (4). */
    size_t n = 0, capacity = 15 + spans.count * 14;
    double *v = malloc(capacity * sizeof(double));
    if (!v)
        abort();
    v[n++] = o->has_max_width;
    v[n++] = o->max_width;
    v[n++] = o->wrap;
    v[n++] = o->max_lines;
    v[n++] = o->truncation;
    v[n++] = o->align;
    v[n++] = o->direction;
    push_style(v, &n, &base);
    v[n++] = (double)spans.count;
    for (size_t i = 0; i < spans.count; ++i)
    {
        const gr_text_span *s = &spans.data[i];
        v[n++] = (double)s->range.start;
        v[n++] = (double)s->range.end;
        v[n++] = s->fields;
        push_style(v, &n, &s->style);
        v[n++] = s->style.color.r;
        v[n++] = s->style.color.g;
        v[n++] = s->style.color.b;
        v[n++] = s->style.color.a;
    }
    web_layout *layout = malloc(sizeof(*layout));
    if (!layout)
        abort();
    layout->id = gr_web_js_text_new(utf8.data, utf8.count, v, n);
    free(v);
    gr_text_layout *result = gr_text_layout_new(system->handle, text, o, &gr_web_layout_vtable, layout);
    if (!result)
        destroy_layout(layout);
    return result;
}

static void destroy_system(void *data)
{
    free(data);
}

gr_text_system *gr_web_text_system_new(void)
{
    web_system *system = calloc(1, sizeof(*system));
    if (!system)
        return NULL;
    gr_text_system_vtable vtable = {layout_text, destroy_system};
    system->handle = gr_text_system_new(&vtable, system);
    if (!system->handle)
        free(system);
    return system->handle;
}
