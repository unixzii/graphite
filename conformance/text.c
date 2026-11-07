#include "internal.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

/* Text layout and rendering through the harness's native text system. Fonts differ per platform, so these check
 * relations and invariants rather than exact metrics. Each case skips when the harness has no text system. */

#define STR(literal) ((gr_string){literal, sizeof(literal) - 1})
#define CHECK(expression) conf_check(c, (expression), "%s:%d: %s", __FILE__, __LINE__, #expression)
#define OK(expression) CHECK((expression) == GR_OK)
#define CLOSE(measured, expected) expect_near(c, #measured, (measured), (expected), 1e-6)
#define SYSTEM_OR_SKIP(c)                                                                                              \
    gr_text_system *system = text_system(c);                                                                           \
    if (!system)                                                                                                       \
    return

static const char pangram[] = "The quick brown fox jumps over the lazy dog";
#define PANGRAM ((gr_string){pangram, sizeof(pangram) - 1})

static gr_text_layout *layout_with(ctx *c, gr_string utf8, gr_text_span_list spans, gr_text_layout_options options)
{
    gr_text_style base = gr_text_style_default();
    gr_attributed_text *text = gr_attributed_text_new(utf8, &base, spans);
    if (!text)
        abort();
    gr_text_layout *layout = gr_text_system_layout(c->text, text, &options);
    if (!layout)
        abort();
    gr_attributed_text_unref(text);
    return layout;
}

static gr_text_layout *layout_of(ctx *c, gr_string utf8, gr_text_layout_options options)
{
    return layout_with(c, utf8, (gr_text_span_list){NULL, 0}, options);
}

static gr_text_layout_options width(double max_width)
{
    gr_text_layout_options o = gr_text_layout_options_default();
    o.has_max_width = true;
    o.max_width = max_width;
    return o;
}

static gr_text_line line(gr_text_layout *layout, size_t index)
{
    gr_text_line result = {0};
    gr_text_layout_get_line(layout, index, &result);
    return result;
}

static gr_rect caret(gr_text_layout *layout, size_t index, gr_text_affinity affinity)
{
    gr_rect result = {{0, 0}, {0, 0}};
    gr_text_layout_get_caret_rect(layout, (gr_text_position){index, affinity}, &result);
    return result;
}

void text_metrics_describe_lines(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    gr_text_layout *layout = layout_of(c, STR("Hello"), gr_text_layout_options_default());
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && !m.truncated && m.visible.start == 0 && m.visible.end == 5);
    CHECK(m.size.width > 20 && m.size.width < 60);
    CHECK(m.size.height > 14 && m.size.height < 25);
    CHECK(m.first_baseline > 10 && m.first_baseline == m.last_baseline);
    gr_text_line l = line(layout, 0);
    CHECK(l.range.start == 0 && l.range.end == 5);
    CLOSE(l.bounds.size.width, m.size.width);
    CLOSE(l.baseline - l.ascent, l.bounds.origin.y);
    CLOSE(l.bounds.size.height, m.size.height);
    gr_text_layout_unref(layout);

    /* Larger and bolder text is wider. */
    gr_text_span span = {{0, 5}, GR_TEXT_STYLE_SIZE | GR_TEXT_STYLE_WEIGHT, gr_text_style_default()};
    span.style.size = 28;
    span.style.weight = 700;
    layout = layout_with(c, STR("Hello"), (gr_text_span_list){&span, 1}, gr_text_layout_options_default());
    CHECK(gr_text_layout_get_metrics(layout).size.width > 2 * m.size.width);
    gr_text_layout_unref(layout);
}

void text_keeps_empty_and_trailing_lines(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout *layout = layout_of(c, (gr_string){NULL, 0}, gr_text_layout_options_default());
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && m.size.width == 0 && m.size.height > 10);
    CHECK(caret(layout, 0, GR_TEXT_AFFINITY_DOWNSTREAM).size.height > 10);
    gr_text_layout_unref(layout);

    layout = layout_of(c, STR("a\n"), gr_text_layout_options_default());
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 2 && m.last_baseline > m.first_baseline);
    CHECK(line(layout, 0).range.end == 2 && line(layout, 1).range.start == 2 && line(layout, 1).range.end == 2);
    gr_text_layout_unref(layout);

    /* CRLF and U+2029 separate paragraphs; line ranges include their separators. */
    layout = layout_of(c,
                       STR("a\r\nb\xE2\x80\xA9"
                           "c"),
                       gr_text_layout_options_default());
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 3);
    CHECK(line(layout, 0).range.end == 3 && line(layout, 1).range.end == 7 && line(layout, 2).range.end == 8);
    /* Carets stop before hard breaks. */
    gr_text_position hit = gr_text_layout_hit_test(layout, (gr_point){500, line(layout, 0).baseline});
    CHECK(hit.index == 1 && hit.affinity == GR_TEXT_AFFINITY_DOWNSTREAM);
    gr_text_layout_unref(layout);
}

void text_wraps_inside_max_width(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout *layout = layout_of(c, PANGRAM, width(80));
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count >= 3 && !m.truncated && m.size.width == 80);
    size_t end = 0;
    for (size_t i = 0; i < m.line_count; ++i)
    {
        gr_text_line l = line(layout, i);
        CHECK(l.range.start == end);
        end = l.range.end;
        CHECK(l.bounds.size.width <= 80 + 1e-6);
        if (i + 1 < m.line_count)
            CHECK(pangram[l.range.end - 1] == ' ');
    }
    CHECK(end == PANGRAM.count);

    /* Soft wraps distinguish the two carets at a line boundary. */
    size_t boundary = line(layout, 0).range.end;
    gr_rect up = caret(layout, boundary, GR_TEXT_AFFINITY_UPSTREAM);
    gr_rect down = caret(layout, boundary, GR_TEXT_AFFINITY_DOWNSTREAM);
    CHECK(up.origin.y == 0 && down.origin.y > 0 && up.origin.x > down.origin.x);
    gr_text_position hit = gr_text_layout_hit_test(layout, (gr_point){79, line(layout, 0).baseline});
    CHECK(hit.index == boundary && hit.affinity == GR_TEXT_AFFINITY_UPSTREAM);
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){0, PANGRAM.count}, NULL, 0) == m.line_count);
    gr_text_layout_unref(layout);

    gr_text_layout_options chars = width(80);
    chars.wrap = GR_TEXT_WRAP_CHAR;
    layout = layout_of(c, PANGRAM, chars);
    CHECK(gr_text_layout_get_metrics(layout).line_count >= m.line_count);
    gr_text_layout_unref(layout);

    gr_text_layout_options none = width(80);
    none.wrap = GR_TEXT_WRAP_NONE;
    layout = layout_of(c, PANGRAM, none);
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && !m.truncated && line(layout, 0).bounds.size.width > 80);
    gr_text_layout_unref(layout);
}

void text_truncates_to_max_lines(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout_options o = width(80);
    o.max_lines = 2;
    gr_text_layout *layout = layout_of(c, PANGRAM, o);
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 2 && m.truncated && m.visible.end == line(layout, 1).range.end &&
          m.visible.end < PANGRAM.count);
    CHECK(gr_text_layout_next_caret_stop(layout, m.visible.end) == m.visible.end);
    gr_text_layout_unref(layout);

    /* The ellipsized last line owns the rest of its paragraph. */
    o.truncation = GR_TEXT_TRUNCATE_END;
    layout = layout_of(c, PANGRAM, o);
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 2 && m.truncated && m.visible.end == PANGRAM.count);
    gr_text_line last = line(layout, 1);
    CHECK(last.range.end == PANGRAM.count && last.bounds.size.width <= 80 + 1e-6);
    /* A point on the ellipsis hits where the elided text starts, and elided positions share its caret. */
    gr_text_position hit = gr_text_layout_hit_test(layout, (gr_point){79, last.baseline});
    CHECK(hit.index > last.range.start && hit.index < PANGRAM.count);
    gr_rect at_hit = caret(layout, hit.index, hit.affinity);
    gr_rect at_end = caret(layout, PANGRAM.count, GR_TEXT_AFFINITY_DOWNSTREAM);
    CLOSE(at_hit.origin.x, at_end.origin.x);
    CHECK(at_end.origin.x > last.bounds.origin.x && at_end.origin.x < 80);
    gr_text_layout_unref(layout);

    gr_text_layout_options single = width(80);
    single.wrap = GR_TEXT_WRAP_NONE;
    single.truncation = GR_TEXT_TRUNCATE_END;
    layout = layout_of(c, PANGRAM, single);
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && m.truncated && m.visible.end == PANGRAM.count);
    CHECK(line(layout, 0).bounds.size.width <= 80 + 1e-6);
    gr_text_layout_unref(layout);
}

void text_aligns_by_paragraph_direction(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout_options o = width(200);
    gr_text_layout *layout = layout_of(c, STR("Hi"), o);
    gr_text_line start = line(layout, 0);
    CHECK(start.bounds.origin.x == 0);
    gr_text_layout_unref(layout);
    o.align = GR_TEXT_ALIGN_CENTER;
    layout = layout_of(c, STR("Hi"), o);
    CLOSE(line(layout, 0).bounds.origin.x, (200 - start.bounds.size.width) / 2);
    gr_text_layout_unref(layout);
    o.align = GR_TEXT_ALIGN_END;
    layout = layout_of(c, STR("Hi"), o);
    CLOSE(line(layout, 0).bounds.origin.x, 200 - start.bounds.size.width);
    gr_text_layout_unref(layout);

    /* Start alignment follows the paragraph direction, unless the direction is explicit. */
    o.align = GR_TEXT_ALIGN_START;
    layout = layout_of(c, STR("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"), o);
    gr_text_line rtl = line(layout, 0);
    CLOSE(rtl.bounds.origin.x + rtl.bounds.size.width, 200);
    gr_text_layout_unref(layout);
    o.direction = GR_TEXT_DIRECTION_LTR;
    layout = layout_of(c, STR("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"), o);
    CHECK(line(layout, 0).bounds.origin.x == 0);
    gr_text_layout_unref(layout);
}

void text_carets_follow_grapheme_clusters(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout *layout = layout_of(c, STR("Hello world"), gr_text_layout_options_default());
    double previous = -1, baseline = line(layout, 0).baseline;
    for (size_t i = 0; i <= 11; ++i)
    {
        gr_rect r = caret(layout, i, GR_TEXT_AFFINITY_DOWNSTREAM);
        CHECK(r.origin.x > previous && r.size.width == 0);
        previous = r.origin.x;
        CHECK(gr_text_layout_hit_test(layout, (gr_point){r.origin.x + 0.5, baseline}).index == i);
    }
    gr_rect rect;
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){0, 11}, &rect, 1) == 1);
    CLOSE(rect.size.width, line(layout, 0).bounds.size.width);
    gr_text_layout_unref(layout);

    /* e + U+0301 is one cluster, and so is a flag made of two regional indicators. */
    layout = layout_of(c, STR("e\xCC\x81x\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5"), gr_text_layout_options_default());
    CHECK(gr_text_layout_next_caret_stop(layout, 0) == 3);
    CHECK(gr_text_layout_next_caret_stop(layout, 1) == 3);
    CHECK(gr_text_layout_next_caret_stop(layout, 3) == 4);
    CHECK(gr_text_layout_next_caret_stop(layout, 4) == 12);
    CHECK(gr_text_layout_previous_caret_stop(layout, 12) == 4);
    CHECK(gr_text_layout_previous_caret_stop(layout, 6) == 3);
    CHECK(gr_text_layout_previous_caret_stop(layout, 3) == 0);
    gr_text_layout_unref(layout);

    /* Selecting across a direction change gives one rectangle per visual piece. */
    layout = layout_of(c, STR("abc \xD7\x90\xD7\x91\xD7\x92 def"), gr_text_layout_options_default());
    gr_rect pieces[4];
    size_t count = gr_text_layout_get_range_rects(layout, (gr_text_range){2, 6}, pieces, 4);
    CHECK(count == 2);
    CHECK(count < 2 || pieces[0].origin.x + pieces[0].size.width < pieces[1].origin.x);
    gr_text_layout_unref(layout);
}

typedef struct
{
    ctx *c;
    gr_text_layout *shared;
    double height;
    size_t lines;
} worker;

/* Queries the shared layout and lays out text of its own, concurrently with the other workers. */
static void work(worker *w)
{
    w->height = gr_text_layout_get_metrics(w->shared).size.height;
    gr_text_layout *own = layout_of(w->c, PANGRAM, width(120));
    w->lines = gr_text_layout_get_metrics(own).line_count;
    gr_text_layout_unref(own);
}

#if defined(_WIN32)
static DWORD WINAPI thread_main(LPVOID data)
{
    work(data);
    return 0;
}
#else
static void *thread_main(void *data)
{
    work(data);
    return NULL;
}
#endif

void text_layouts_are_thread_safe(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout *layout = layout_of(c, PANGRAM, width(120));
    worker workers[8];
    for (size_t i = 0; i < 8; ++i)
        workers[i] = (worker){c, layout, 0, 0};
#if defined(_WIN32)
    HANDLE threads[8];
    for (size_t i = 0; i < 8; ++i)
        threads[i] = CreateThread(NULL, 0, thread_main, &workers[i], 0, NULL);
    for (size_t i = 0; i < 8; ++i)
    {
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[8];
    for (size_t i = 0; i < 8; ++i)
        if (pthread_create(&threads[i], NULL, thread_main, &workers[i]))
            abort();
    for (size_t i = 0; i < 8; ++i)
        pthread_join(threads[i], NULL);
#endif
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    for (size_t i = 0; i < 8; ++i)
        CHECK(workers[i].height == m.size.height && workers[i].lines == m.line_count && m.size.height > 0);
    gr_text_layout_unref(layout);
}

typedef struct
{
    gr_text_layout *layout;
    gr_point origin;
    gr_color color;
} text_drawing;

static void draw_text(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    const text_drawing *d = userdata;
    API(gr_canvas_draw_text_layout(canvas, d->layout, d->origin, d->color));
}

static void free_text(void *userdata)
{
    text_drawing *d = userdata;
    gr_text_layout_unref(d->layout);
    free(d);
}

void text_is_drawn_upright_in_its_box(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    /* A colored first word must not leak its color into the rest of the line. */
    gr_text_span blue = {{0, 5}, GR_TEXT_STYLE_COLOR | GR_TEXT_STYLE_DECORATIONS, gr_text_style_default()};
    blue.style.color = color(BLUE);
    blue.style.decorations = GR_TEXT_DECORATION_UNDERLINE;
    text_drawing *d = malloc(sizeof(*d));
    if (!d)
        abort();
    *d = (text_drawing){
        layout_with(c, STR("Hello world"), (gr_text_span_list){&blue, 1}, gr_text_layout_options_default()),
        {8, 10},
        color(RED)};
    gr_text_layout_metrics m = gr_text_layout_get_metrics(d->layout);
    CHECK(d->origin.x + m.size.width < 100);

    gr_scene *s = new_scene(c);
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    API(gr_layer_set_frame(l, RECT(0, 0, 100, 40)));
    gr_drawer drawer = {d, draw_text, free_text};
    API(gr_content_layer_set_drawer(l, &drawer));
    API(gr_layer_add_child(gr_scene_get_root(s), l));
    frame *f = render(c, s);

    size_t red = 0, blue_pixels = 0, outside = 0, underline = 0;
    const gr_conf_pixels *p = &f->pixels;
    double baseline = d->origin.y + m.first_baseline;
    for (uint32_t y = 0; y < p->height; ++y)
        for (uint32_t x = 0; x < p->width; ++x)
        {
            const uint8_t *v = p->data + ((size_t)y * p->width + x) * 4;
            /* A one-pixel underline at 1x is anti-aliased: count any blue-dominant pixel below the baseline. */
            double below = (y + 0.5) / f->scale - baseline;
            if (below > 0 && below < 4 && v[2] > 60 && v[0] * 3 < v[2])
                ++underline;
            if (v[3] < 128)
                continue;
            double lx = (x + 0.5) / f->scale - d->origin.x, ly = (y + 0.5) / f->scale - d->origin.y;
            if (lx < -1 || ly < -1 || lx > m.size.width + 1 || ly > m.size.height + 1)
                ++outside;
            if (v[0] > 200 && v[2] < 60)
                ++red;
            if (v[2] > 200 && v[0] < 60)
                ++blue_pixels;
        }
    double area = f->scale * f->scale;
    conf_check(c, red > 12 * area && blue_pixels > 12 * area, "red %zu and blue %zu pixels", red, blue_pixels);
    conf_check(c, outside == 0, "%zu pixels outside the layout box", outside);
    /* The underline runs below the blue word's baseline. */
    conf_check(c, underline > 10 * f->scale, "%zu underline pixels", underline);
}

/* Adds a full-scene content layer drawing layout at origin in color; the layer owns a layout reference. */
static void show(ctx *c, gr_scene *s, gr_text_layout *layout, gr_point origin, gr_color fill)
{
    text_drawing *d = malloc(sizeof(*d));
    if (!d)
        abort();
    *d = (text_drawing){gr_text_layout_ref(layout), origin, fill};
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    API(gr_layer_set_frame(l, RECT(0, 0, 100, 100)));
    gr_drawer drawer = {d, draw_text, free_text};
    API(gr_content_layer_set_drawer(l, &drawer));
    API(gr_layer_add_child(gr_scene_get_root(s), l));
}

static size_t inked(const frame *f, gr_rect r)
{
    size_t count = 0;
    const gr_conf_pixels *p = &f->pixels;
    for (uint32_t y = 0; y < p->height; ++y)
        for (uint32_t x = 0; x < p->width; ++x)
        {
            double lx = (x + 0.5) / f->scale, ly = (y + 0.5) / f->scale;
            if (lx >= r.origin.x && ly >= r.origin.y && lx < r.origin.x + r.size.width &&
                ly < r.origin.y + r.size.height && p->data[((size_t)y * p->width + x) * 4 + 3] > 64)
                ++count;
        }
    return count;
}

void text_color_is_paint_only(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    /* A color span leaves the layout unchanged. */
    gr_text_layout *plain = layout_of(c, STR("Paint only"), gr_text_layout_options_default());
    gr_text_span colored = {{0, 5}, GR_TEXT_STYLE_COLOR, gr_text_style_default()};
    colored.style.color = color(BLUE);
    gr_text_layout *styled =
        layout_with(c, STR("Paint only"), (gr_text_span_list){&colored, 1}, gr_text_layout_options_default());
    gr_text_layout_metrics a = gr_text_layout_get_metrics(plain), b = gr_text_layout_get_metrics(styled);
    CLOSE(b.size.width, a.size.width);
    CLOSE(b.size.height, a.size.height);
    CLOSE(line(styled, 0).bounds.origin.x, line(plain, 0).bounds.origin.x);
    for (size_t i = 0; i <= 10; ++i)
        CLOSE(caret(styled, i, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x,
              caret(plain, i, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x);
    gr_text_layout_unref(styled);

    /* The same layout drawn in two colors covers the same pixels. */
    gr_scene *s = new_scene(c);
    show(c, s, plain, POINT(4, 10), color(RED));
    show(c, s, plain, POINT(4, 50), color(BLUE));
    frame *f = render(c, s);
    size_t top = 0, bottom = 0, differ = 0;
    const gr_conf_pixels *p = &f->pixels;
    uint32_t shift = (uint32_t)lround(40 * f->scale);
    for (uint32_t y = 0; y < shift && y + shift < p->height; ++y)
        for (uint32_t x = 0; x < p->width; ++x)
        {
            const uint8_t *u = p->data + ((size_t)y * p->width + x) * 4;
            const uint8_t *v = p->data + ((size_t)(y + shift) * p->width + x) * 4;
            top += u[3] > 64;
            bottom += v[3] > 64;
            differ += abs((int)u[3] - (int)v[3]) > 8;
        }
    conf_check(c, top > 20 && differ <= top / 50, "coverage %zu vs %zu, %zu pixels differ", top, bottom, differ);
    gr_text_layout_unref(plain);
}

void text_letter_spacing_goes_between_graphemes(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout *plain = layout_of(c, STR("Hello"), gr_text_layout_options_default());
    gr_text_span spaced = {{0, 5}, GR_TEXT_STYLE_LETTER_SPACING, gr_text_style_default()};
    spaced.style.letter_spacing = 3;
    gr_text_layout *wide =
        layout_with(c, STR("Hello"), (gr_text_span_list){&spaced, 1}, gr_text_layout_options_default());
    expect_near(c, "width gained by 3 pt between five graphemes",
                gr_text_layout_get_metrics(wide).size.width - gr_text_layout_get_metrics(plain).size.width, 12, 0.5);
    /* Carets move by the gaps before them, give or take where they sit inside a gap. */
    for (size_t i = 0; i <= 5; ++i)
    {
        double shift = caret(wide, i, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x -
                       caret(plain, i, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x;
        double low = i ? 3.0 * ((double)i - 1) : 0, high = 3.0 * (double)(i < 5 ? i : 4);
        conf_check(c, shift >= low - 0.5 && shift <= high + 0.5, "caret %zu moved %.2f, expected %.0f to %.0f", i,
                   shift, low, high);
    }
    gr_text_layout_unref(wide);
    gr_text_layout_unref(plain);
}

void text_styles_and_decorations_render(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_span bold = {{0, 4}, GR_TEXT_STYLE_WEIGHT, gr_text_style_default()};
    bold.style.weight = 700;
    gr_text_layout *regular = layout_of(c, STR("Mmmm"), gr_text_layout_options_default());
    gr_text_layout *heavy =
        layout_with(c, STR("Mmmm"), (gr_text_span_list){&bold, 1}, gr_text_layout_options_default());
    CHECK(gr_text_layout_get_metrics(heavy).size.width > gr_text_layout_get_metrics(regular).size.width);
    gr_text_layout_unref(heavy);

    gr_text_span italic = {{0, 4}, GR_TEXT_STYLE_SLANT, gr_text_style_default()};
    italic.style.slant = GR_FONT_SLANT_ITALIC;
    gr_text_span strike = {{0, 4}, GR_TEXT_STYLE_DECORATIONS, gr_text_style_default()};
    strike.style.decorations = GR_TEXT_DECORATION_STRIKETHROUGH;
    gr_text_layout *slanted =
        layout_with(c, STR("Mmmm"), (gr_text_span_list){&italic, 1}, gr_text_layout_options_default());
    gr_text_layout *struck =
        layout_with(c, STR("Mmmm"), (gr_text_span_list){&strike, 1}, gr_text_layout_options_default());
    gr_scene *s = new_scene(c);
    show(c, s, regular, POINT(4, 4), color(RED));
    show(c, s, slanted, POINT(4, 34), color(RED));
    show(c, s, struck, POINT(4, 64), color(RED));
    frame *f = render(c, s);
    gr_text_layout_metrics m = gr_text_layout_get_metrics(regular);
    gr_text_line l = line(regular, 0);

    /* Italic glyphs lean: the top half of the line moves right relative to the bottom half. */
    double half = l.baseline / 2;
    gr_rect upper = RECT(4, 4, m.size.width + 4, half), lower = RECT(4, 4 + half, m.size.width + 4, half);
    size_t straight = inked(f, upper) + inked(f, lower);
    size_t changed = 0;
    const gr_conf_pixels *p = &f->pixels;
    uint32_t shift = (uint32_t)lround(30 * f->scale);
    for (uint32_t y = (uint32_t)(4 * f->scale); y < (uint32_t)((4 + m.size.height) * f->scale); ++y)
        for (uint32_t x = 0; x < p->width; ++x)
            changed += abs((int)p->data[((size_t)y * p->width + x) * 4 + 3] -
                           (int)p->data[((size_t)(y + shift) * p->width + x) * 4 + 3]) > 64;
    conf_check(c, straight > 0 && changed > straight / 10, "italic changed %zu of %zu inked pixels", changed, straight);

    /* Strikethrough adds ink between the glyphs at about half the x-height. */
    gr_rect band_plain = RECT(4, 4 + l.baseline - l.ascent * 0.45, m.size.width, l.ascent * 0.3);
    gr_rect band_struck = band_plain;
    band_struck.origin.y += 60;
    size_t base_ink = inked(f, band_plain), struck_ink = inked(f, band_struck);
    conf_check(c, struck_ink > base_ink + (size_t)(m.size.width * f->scale * 0.5),
               "strikethrough band %zu vs plain %zu", struck_ink, base_ink);
    gr_text_layout_unref(struck);
    gr_text_layout_unref(slanted);
    gr_text_layout_unref(regular);
}

void text_carets_follow_affinity_at_direction_changes(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    /* "abc " then three Hebrew letters (bytes 4 to 10), shown reversed, then " def". */
    gr_text_layout *layout = layout_of(c, STR("abc \xD7\x90\xD7\x91\xD7\x92 def"), gr_text_layout_options_default());
    gr_rect hebrew;
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){4, 10}, &hebrew, 1) == 1);
    double left = hebrew.origin.x, right = left + hebrew.size.width;
    /* Downstream belongs to the character at the index, upstream to the one before it. */
    expect_near(c, "downstream caret at the first Hebrew letter",
                caret(layout, 4, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x, right, 0.5);
    expect_near(c, "upstream caret after the space", caret(layout, 4, GR_TEXT_AFFINITY_UPSTREAM).origin.x, left, 0.5);
    expect_near(c, "downstream caret at the space after Hebrew",
                caret(layout, 10, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x, right, 0.5);
    expect_near(c, "upstream caret after the last Hebrew letter", caret(layout, 10, GR_TEXT_AFFINITY_UPSTREAM).origin.x,
                left, 0.5);
    /* Inside the run, carets move leftward as the index grows. */
    CHECK(caret(layout, 6, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x < right - 1 &&
          caret(layout, 8, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x <
              caret(layout, 6, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x);
    gr_text_layout_unref(layout);
}

void text_rtl_paragraphs_hit_test_from_the_right(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout *layout = layout_of(c, STR("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"), width(200));
    gr_text_line l = line(layout, 0);
    double left = l.bounds.origin.x, right = left + l.bounds.size.width;
    expect_near(c, "start caret at the right edge", caret(layout, 0, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x, right, 0.5);
    expect_near(c, "end caret at the left edge", caret(layout, 8, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x, left, 0.5);
    double previous = right + 1;
    for (size_t i = 0; i <= 8; i += 2)
    {
        double x = caret(layout, i, GR_TEXT_AFFINITY_DOWNSTREAM).origin.x;
        CHECK(x < previous);
        previous = x;
        /* Just left of a caret is the character after it. */
        CHECK(gr_text_layout_hit_test(layout, (gr_point){x - 0.5, l.baseline}).index == (i < 8 ? i : 8));
    }
    CHECK(gr_text_layout_hit_test(layout, (gr_point){right + 20, l.baseline}).index == 0);
    CHECK(gr_text_layout_hit_test(layout, (gr_point){left - 20, l.baseline}).index == 8);
    gr_text_layout_unref(layout);
}

void text_ellipsis_marks_a_cut_at_a_paragraph_break(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    /* The first paragraph fits; the cut falls exactly at its separator. */
    gr_text_layout *whole = layout_of(c, STR("First line"), gr_text_layout_options_default());
    double natural = gr_text_layout_get_metrics(whole).size.width;
    gr_text_layout_unref(whole);
    gr_text_layout_options o = width(200);
    o.max_lines = 1;
    o.truncation = GR_TEXT_TRUNCATE_END;
    gr_text_layout *layout = layout_of(c, STR("First line\nSecond line"), o);
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && m.truncated && m.visible.end == 11);
    gr_text_line l = line(layout, 0);
    CHECK(l.range.start == 0 && l.range.end == 11);
    conf_check(c, l.bounds.size.width > natural + 3, "line width %.2f, text alone %.2f: no ellipsis",
               l.bounds.size.width, natural);
    /* The ellipsis is drawn after the text, inside the box. */
    gr_scene *s = new_scene(c);
    show(c, s, layout, POINT(4, 10), color(RED));
    frame *f = render(c, s);
    conf_check(c, inked(f, RECT(4 + natural + 1, 10, l.bounds.size.width - natural, m.size.height)) > 0,
               "nothing drawn after the text");
    gr_text_layout_unref(layout);
}

static size_t glyphs_of(gr_text_layout *layout, size_t line, gr_text_glyph *glyphs, size_t capacity)
{
    size_t count = gr_text_layout_get_glyphs(layout, line, glyphs, capacity);
    return count < capacity ? count : capacity;
}

void text_glyphs_describe_lines(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_glyph g[64];
    gr_text_layout *layout = layout_of(c, STR("Hello world"), gr_text_layout_options_default());
    size_t n = glyphs_of(layout, 0, g, 64);
    CHECK(n == gr_text_layout_get_glyphs(layout, 0, NULL, 0) && n >= 10);
    CHECK(gr_text_layout_get_glyphs(layout, 1, g, 64) == 0);
    double advance = 0;
    bool covered[11] = {false};
    for (size_t i = 0; i < n; ++i)
    {
        CHECK(!i || g[i].position.x >= g[i - 1].position.x);
        CHECK(g[i].cluster.start < g[i].cluster.end && g[i].cluster.end <= 11 && !g[i].flags);
        for (size_t k = g[i].cluster.start; k < g[i].cluster.end && k < 11; ++k)
            covered[k] = true;
        advance += g[i].advance;
    }
    for (size_t k = 0; k < 11; ++k)
        CHECK(covered[k]);
    expect_near(c, "advances add up to the line", advance, line(layout, 0).bounds.size.width, 1);
    CHECK(g[0].bounds.size.width > 2 && g[0].bounds.size.height > 5 && g[0].position.y == line(layout, 0).baseline);
    for (size_t i = 0; i < n; ++i)
        if (g[i].cluster.start == 5) /* The space has no ink. */
            CHECK(g[i].bounds.size.width == 0 || g[i].bounds.size.height == 0);
    gr_text_layout_unref(layout);

    /* A ZWJ emoji sequence is one cluster, however many glyphs draw it. */
    layout = layout_of(c, STR("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"), gr_text_layout_options_default());
    n = glyphs_of(layout, 0, g, 64);
    CHECK(n >= 1);
    for (size_t i = 0; i < n; ++i)
        CHECK(g[i].cluster.start == 0 && g[i].cluster.end == 11);
    gr_text_layout_unref(layout);

    /* Right-to-left glyphs come in visual order: the logical first letter is rightmost. */
    layout = layout_of(c, STR("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"), gr_text_layout_options_default());
    n = glyphs_of(layout, 0, g, 64);
    CHECK(n == 4 && (g[0].flags & GR_TEXT_GLYPH_RTL) && g[0].cluster.start == 6 && g[3].cluster.start == 0);
    gr_text_layout_unref(layout);

    /* Separators have no glyphs. */
    layout = layout_of(c, STR("a\nb"), gr_text_layout_options_default());
    CHECK(gr_text_layout_get_glyphs(layout, 0, NULL, 0) == 1 && gr_text_layout_get_glyphs(layout, 1, NULL, 0) == 1);
    gr_text_layout_unref(layout);

    /* An ellipsis is listed where it is drawn, covering the elided text. */
    gr_text_layout_options o = width(80);
    o.max_lines = 2;
    o.truncation = GR_TEXT_TRUNCATE_END;
    layout = layout_of(c, PANGRAM, o);
    gr_text_line last = line(layout, 1);
    n = glyphs_of(layout, 1, g, 64);
    size_t elided = gr_text_layout_hit_test(layout, (gr_point){79, last.baseline}).index;
    CHECK(n >= 2 && (g[n - 1].flags & GR_TEXT_GLYPH_ELLIPSIS) && !(g[0].flags & GR_TEXT_GLYPH_ELLIPSIS));
    CHECK(g[n - 1].cluster.start == elided && g[n - 1].cluster.end == PANGRAM.count);
    for (size_t i = 0; i < n; ++i)
        CHECK((g[i].flags & GR_TEXT_GLYPH_ELLIPSIS) || g[i].cluster.end <= elided);
    gr_text_layout_unref(layout);
}

typedef struct
{
    gr_text_layout *layout;
    gr_point origin;
    gr_color color;
    bool single; /* Every glyph as its own range, else every line as one range. */
} glyph_drawing;

static void draw_glyphs(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    const glyph_drawing *d = userdata;
    size_t lines = gr_text_layout_get_metrics(d->layout).line_count;
    for (size_t l = 0; l < lines; ++l)
    {
        size_t n = gr_text_layout_get_glyphs(d->layout, l, NULL, 0);
        if (d->single)
            for (size_t i = 0; i < n; ++i)
                API(gr_canvas_draw_text_glyphs(canvas, d->layout, l, (gr_glyph_range){i, i + 1}, d->origin, d->color));
        else
            API(gr_canvas_draw_text_glyphs(canvas, d->layout, l, (gr_glyph_range){0, n}, d->origin, d->color));
    }
}

static void free_glyphs(void *userdata)
{
    glyph_drawing *d = userdata;
    gr_text_layout_unref(d->layout);
    free(d);
}

static void show_glyphs(ctx *c, gr_scene *s, gr_text_layout *layout, gr_point origin, bool single)
{
    glyph_drawing *d = malloc(sizeof(*d));
    if (!d)
        abort();
    *d = (glyph_drawing){gr_text_layout_ref(layout), origin, color(RED), single};
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    API(gr_layer_set_frame(l, RECT(0, 0, 100, 100)));
    gr_drawer drawer = {d, draw_glyphs, free_glyphs};
    API(gr_content_layer_set_drawer(l, &drawer));
    API(gr_layer_add_child(gr_scene_get_root(s), l));
}

/* Counts pixels whose channels differ by more than 48 between two equal-height bands shift apart. */
static size_t band_difference(const frame *f, double top, double height, double shift, size_t *inked_pixels)
{
    const gr_conf_pixels *p = &f->pixels;
    uint32_t y0 = (uint32_t)lround(top * f->scale), rows = (uint32_t)lround(height * f->scale);
    uint32_t offset = (uint32_t)lround(shift * f->scale);
    size_t differ = 0, inked_count = 0;
    for (uint32_t y = y0; y < y0 + rows && y + offset < p->height; ++y)
        for (uint32_t x = 0; x < p->width; ++x)
        {
            const uint8_t *u = p->data + ((size_t)y * p->width + x) * 4;
            const uint8_t *v = p->data + ((size_t)(y + offset) * p->width + x) * 4;
            inked_count += u[3] > 64;
            for (int k = 0; k < 4; ++k)
                if (abs((int)u[k] - (int)v[k]) > 48)
                {
                    ++differ;
                    break;
                }
        }
    *inked_pixels = inked_count;
    return differ;
}

void text_glyph_ranges_redraw_the_layout(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    /* Two lines with a colored span; no decorations, which glyph drawing leaves out. */
    gr_text_span spans[2] = {{{3, 8}, GR_TEXT_STYLE_COLOR, gr_text_style_default()},
                             {{0, 16}, GR_TEXT_STYLE_SIZE, gr_text_style_default()}};
    spans[0].style.color = color(BLUE);
    spans[1].style.size = 11;
    gr_text_layout *layout = layout_with(c, STR("Ag gyp\nfine \xF0\x9F\x98\x80"), (gr_text_span_list){spans, 2},
                                         gr_text_layout_options_default());
    /* Even offsets keep the three drawings on the same device pixel phase at 1.5x too. */
    double height = gr_text_layout_get_metrics(layout).size.height, step = 2 * ceil((height + 1) / 2);
    CHECK(2 + 3 * step < 100);
    gr_scene *s = new_scene(c);
    show(c, s, layout, POINT(4, 2), color(RED));
    show_glyphs(c, s, layout, POINT(4, 2 + step), false);
    show_glyphs(c, s, layout, POINT(4, 2 + 2 * step), true);
    frame *f = render(c, s);
    size_t ink = 0, ink2 = 0;
    size_t lines = band_difference(f, 2, step, step, &ink);
    size_t singles = band_difference(f, 2, step, 2 * step, &ink2);
    conf_check(c, ink > 50 && lines <= ink / 50, "line ranges differ in %zu of %zu inked pixels", lines, ink);
    conf_check(c, singles <= ink / 20, "single glyphs differ in %zu of %zu inked pixels", singles, ink);
    gr_text_layout_unref(layout);
}

static void draw_one_glyph(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    const text_drawing *d = userdata;
    API(gr_canvas_draw_text_glyphs(canvas, d->layout, 0, (gr_glyph_range){1, 2}, d->origin, d->color));
}

void text_glyphs_draw_where_their_origin_puts_them(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_layout *layout = layout_of(c, STR("Wg"), gr_text_layout_options_default());
    gr_text_glyph g[4];
    CHECK(glyphs_of(layout, 0, g, 4) == 2);
    /* Draw "g" alone so that its origin lands at (50, 60). */
    gr_point target = POINT(50, 60);
    gr_point origin = POINT(target.x - g[1].position.x, target.y - g[1].position.y);
    gr_rect ink = g[1].bounds;
    ink.origin.x += origin.x;
    ink.origin.y += origin.y;
    gr_scene *s = new_scene(c);
    text_drawing *d = malloc(sizeof(*d));
    if (!d)
        abort();
    *d = (text_drawing){gr_text_layout_ref(layout), origin, color(RED)};
    gr_layer *l = new_layer(c, s, GR_LAYER_CONTENT);
    API(gr_layer_set_frame(l, RECT(0, 0, 100, 100)));
    gr_drawer drawer = {d, draw_one_glyph, free_text};
    API(gr_content_layer_set_drawer(l, &drawer));
    API(gr_layer_add_child(gr_scene_get_root(s), l));
    frame *f = render(c, s);
    size_t inside = inked(f, RECT(ink.origin.x - 1, ink.origin.y - 1, ink.size.width + 2, ink.size.height + 2));
    size_t all = inked(f, RECT(0, 0, 100, 100));
    conf_check(c, inside > 10 && inside == all, "%zu of %zu inked pixels inside the glyph's bounds", inside, all);
    /* The descender reaches below the target baseline. */
    CHECK(inked(f, RECT(ink.origin.x, target.y + 1, ink.size.width, 10)) > 0);
    gr_text_layout_unref(layout);
}

void text_glyphs_identify_font_and_glyph(ctx *c)
{
    SYSTEM_OR_SKIP(c);
    (void)system;
    gr_text_glyph a[16], b[16], bold[16], emoji[16];
    gr_text_layout *hello = layout_of(c, STR("Hello"), gr_text_layout_options_default());
    gr_text_layout *help = layout_of(c, STR("Help"), gr_text_layout_options_default());
    gr_text_span heavy = {{0, 5}, GR_TEXT_STYLE_WEIGHT, gr_text_style_default()};
    heavy.style.weight = 700;
    gr_text_layout *strong =
        layout_with(c, STR("Hello"), (gr_text_span_list){&heavy, 1}, gr_text_layout_options_default());
    gr_text_layout *smile = layout_of(c, STR("H\xF0\x9F\x98\x80"), gr_text_layout_options_default());
    CHECK(glyphs_of(hello, 0, a, 16) == 5 && glyphs_of(help, 0, b, 16) == 4);
    CHECK(glyphs_of(strong, 0, bold, 16) == 5 && glyphs_of(smile, 0, emoji, 16) >= 2);

    /* The same letter in the same font and size is the same glyph across layouts. */
    CHECK(a[0].font && a[0].font == b[0].font && a[0].glyph == b[0].glyph && a[0].font_size == b[0].font_size);
    CHECK(a[2].glyph == b[2].glyph && a[2].glyph == a[3].glyph);
    expect_near(c, "font size", a[0].font_size, 14, 1e-6);
    /* Different letters are different glyphs of one font. */
    CHECK(a[0].font == a[1].font && a[0].glyph != a[1].glyph);
    /* Bold is another face, or another position on a weight axis. */
    CHECK(bold[0].font && bold[0].font != a[0].font);
    /* Emoji come from a fallback font. */
    CHECK(emoji[0].font == a[0].font && emoji[0].glyph == a[0].glyph);
    for (size_t i = 1; i < 2; ++i)
        CHECK(emoji[i].font && emoji[i].font != a[0].font);
    gr_text_layout_unref(smile);
    gr_text_layout_unref(strong);
    gr_text_layout_unref(help);
    gr_text_layout_unref(hello);
}
