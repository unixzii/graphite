#include "internal.h"
#include "test.h"

#define STR(literal) ((gr_string){literal, sizeof(literal) - 1})

static const char pangram[] = "The quick brown fox jumps over the lazy dog";

static gr_text_layout *layout_with(gr_text_system *system, gr_string utf8, gr_text_span_list spans,
                                   gr_text_layout_options options)
{
    gr_text_style base = gr_text_style_default();
    gr_attributed_text *text = gr_attributed_text_new(utf8, &base, spans);
    CHECK(text);
    gr_text_layout *layout = gr_text_system_layout(system, text, &options);
    CHECK(layout);
    gr_attributed_text_unref(text);
    return layout;
}

static gr_text_layout *layout_of(gr_text_system *system, gr_string utf8, gr_text_layout_options options)
{
    return layout_with(system, utf8, (gr_text_span_list){NULL, 0}, options);
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
    gr_text_line result;
    OK(gr_text_layout_get_line(layout, index, &result));
    return result;
}

static void metrics(void)
{
    gr_text_system *system = gr_macos_text_system_new();
    gr_text_layout *layout = layout_of(system, STR("Hello"), gr_text_layout_options_default());
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && !m.truncated && m.visible.end == 5);
    CHECK(m.size.width > 20 && m.size.width < 60);
    CHECK(m.size.height > 14 && m.size.height < 25);
    CHECK(m.first_baseline > 10 && m.first_baseline == m.last_baseline);
    gr_text_line l = line(layout, 0);
    CHECK(l.range.start == 0 && l.range.end == 5);
    CHECK_NEAR(l.bounds.size.width, m.size.width);
    CHECK_NEAR(l.baseline - l.ascent, 0);
    gr_text_layout_unref(layout);

    /* Larger and bolder text is wider. */
    gr_text_span span = {{0, 5}, GR_TEXT_STYLE_SIZE | GR_TEXT_STYLE_WEIGHT, gr_text_style_default()};
    span.style.size = 28;
    span.style.weight = 700;
    layout = layout_with(system, STR("Hello"), (gr_text_span_list){&span, 1}, gr_text_layout_options_default());
    CHECK(gr_text_layout_get_metrics(layout).size.width > 2 * m.size.width);
    gr_text_layout_unref(layout);
    gr_text_system_unref(system);
}

static void empty_and_trailing_lines(void)
{
    gr_text_system *system = gr_macos_text_system_new();
    gr_text_layout *layout = layout_of(system, (gr_string){NULL, 0}, gr_text_layout_options_default());
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && m.size.width == 0 && m.size.height > 10);
    gr_rect caret;
    OK(gr_text_layout_get_caret_rect(layout, (gr_text_position){0, GR_TEXT_AFFINITY_DOWNSTREAM}, &caret));
    CHECK(caret.size.height > 10);
    gr_text_layout_unref(layout);

    layout = layout_of(system, STR("a\n"), gr_text_layout_options_default());
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 2 && m.last_baseline > m.first_baseline);
    CHECK(line(layout, 0).range.end == 2 && line(layout, 1).range.start == 2 && line(layout, 1).range.end == 2);
    gr_text_layout_unref(layout);

    layout = layout_of(system,
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
    gr_text_system_unref(system);
}

static void wrapping(void)
{
    gr_text_system *system = gr_macos_text_system_new();
    gr_string text = {pangram, sizeof(pangram) - 1};
    gr_text_layout *layout = layout_of(system, text, width(80));
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
    CHECK(end == text.count);

    /* Soft wraps distinguish the two carets at a line boundary. */
    size_t boundary = line(layout, 0).range.end;
    gr_rect up, down;
    OK(gr_text_layout_get_caret_rect(layout, (gr_text_position){boundary, GR_TEXT_AFFINITY_UPSTREAM}, &up));
    OK(gr_text_layout_get_caret_rect(layout, (gr_text_position){boundary, GR_TEXT_AFFINITY_DOWNSTREAM}, &down));
    CHECK(up.origin.y == 0 && down.origin.y > 0 && up.origin.x > down.origin.x);
    gr_text_position hit = gr_text_layout_hit_test(layout, (gr_point){79, line(layout, 0).baseline});
    CHECK(hit.index == boundary && hit.affinity == GR_TEXT_AFFINITY_UPSTREAM);
    size_t rects = gr_text_layout_get_range_rects(layout, (gr_text_range){0, text.count}, NULL, 0);
    CHECK(rects == m.line_count);
    gr_text_layout_unref(layout);

    gr_text_layout_options chars = width(80);
    chars.wrap = GR_TEXT_WRAP_CHAR;
    layout = layout_of(system, text, chars);
    CHECK(gr_text_layout_get_metrics(layout).line_count >= m.line_count);
    gr_text_layout_unref(layout);

    gr_text_layout_options none = width(80);
    none.wrap = GR_TEXT_WRAP_NONE;
    layout = layout_of(system, text, none);
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && !m.truncated && line(layout, 0).bounds.size.width > 80);
    gr_text_layout_unref(layout);
    gr_text_system_unref(system);
}

static void truncation(void)
{
    gr_text_system *system = gr_macos_text_system_new();
    gr_string text = {pangram, sizeof(pangram) - 1};
    gr_text_layout_options o = width(80);
    o.max_lines = 2;
    gr_text_layout *layout = layout_of(system, text, o);
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 2 && m.truncated && m.visible.end == line(layout, 1).range.end && m.visible.end < text.count);
    CHECK(gr_text_layout_next_caret_stop(layout, m.visible.end) == m.visible.end);
    gr_text_layout_unref(layout);

    o.truncation = GR_TEXT_TRUNCATE_END;
    layout = layout_of(system, text, o);
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 2 && m.truncated && m.visible.end == text.count);
    gr_text_line last = line(layout, 1);
    CHECK(last.range.end == text.count && last.bounds.size.width <= 80 + 1e-6);
    /* Elided text collapses onto the caret before the ellipsis. */
    gr_text_position hit = gr_text_layout_hit_test(layout, (gr_point){79, last.baseline});
    CHECK(hit.index > last.range.start && hit.index < text.count);
    gr_rect at_hit, at_end;
    OK(gr_text_layout_get_caret_rect(layout, hit, &at_hit));
    OK(gr_text_layout_get_caret_rect(layout, (gr_text_position){text.count, GR_TEXT_AFFINITY_DOWNSTREAM}, &at_end));
    CHECK_NEAR(at_hit.origin.x, at_end.origin.x);
    gr_text_layout_unref(layout);

    gr_text_layout_options single = width(80);
    single.wrap = GR_TEXT_WRAP_NONE;
    single.truncation = GR_TEXT_TRUNCATE_END;
    layout = layout_of(system, text, single);
    m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 1 && m.truncated && m.visible.end == text.count);
    CHECK(line(layout, 0).bounds.size.width <= 80 + 1e-6);
    gr_text_layout_unref(layout);
    gr_text_system_unref(system);
}

static void alignment(void)
{
    gr_text_system *system = gr_macos_text_system_new();
    gr_text_layout_options o = width(200);
    gr_text_layout *layout = layout_of(system, STR("Hi"), o);
    gr_text_line start = line(layout, 0);
    CHECK(start.bounds.origin.x == 0);
    gr_text_layout_unref(layout);
    o.align = GR_TEXT_ALIGN_CENTER;
    layout = layout_of(system, STR("Hi"), o);
    CHECK_NEAR(line(layout, 0).bounds.origin.x, (200 - start.bounds.size.width) / 2);
    gr_text_layout_unref(layout);
    o.align = GR_TEXT_ALIGN_END;
    layout = layout_of(system, STR("Hi"), o);
    CHECK_NEAR(line(layout, 0).bounds.origin.x, 200 - start.bounds.size.width);
    gr_text_layout_unref(layout);

    /* Start alignment follows the paragraph direction. */
    o.align = GR_TEXT_ALIGN_START;
    layout = layout_of(system, STR("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"), o);
    gr_text_line rtl = line(layout, 0);
    CHECK_NEAR(rtl.bounds.origin.x + rtl.bounds.size.width, 200);
    gr_text_layout_unref(layout);
    o.direction = GR_TEXT_DIRECTION_LTR;
    layout = layout_of(system, STR("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"), o);
    CHECK(line(layout, 0).bounds.origin.x == 0);
    gr_text_layout_unref(layout);
    gr_text_system_unref(system);
}

static void carets(void)
{
    gr_text_system *system = gr_macos_text_system_new();
    gr_text_layout *layout = layout_of(system, STR("Hello world"), gr_text_layout_options_default());
    double previous = -1, baseline = line(layout, 0).baseline;
    for (size_t i = 0; i <= 11; ++i)
    {
        gr_rect caret;
        OK(gr_text_layout_get_caret_rect(layout, (gr_text_position){i, GR_TEXT_AFFINITY_DOWNSTREAM}, &caret));
        CHECK(caret.origin.x > previous && caret.size.width == 0);
        previous = caret.origin.x;
        CHECK(gr_text_layout_hit_test(layout, (gr_point){caret.origin.x + 0.5, baseline}).index == i);
    }
    gr_rect rect;
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){0, 11}, &rect, 1) == 1);
    CHECK_NEAR(rect.size.width, line(layout, 0).bounds.size.width);
    gr_text_layout_unref(layout);

    /* Grapheme clusters: e + U+0301, then a flag made of two regional indicators. */
    layout = layout_of(system, STR("e\xCC\x81x\xF0\x9F\x87\xAF\xF0\x9F\x87\xB5"), gr_text_layout_options_default());
    CHECK(gr_text_layout_next_caret_stop(layout, 0) == 3);
    CHECK(gr_text_layout_next_caret_stop(layout, 1) == 3);
    CHECK(gr_text_layout_next_caret_stop(layout, 3) == 4);
    CHECK(gr_text_layout_next_caret_stop(layout, 4) == 12);
    CHECK(gr_text_layout_previous_caret_stop(layout, 12) == 4);
    CHECK(gr_text_layout_previous_caret_stop(layout, 6) == 3);
    CHECK(gr_text_layout_previous_caret_stop(layout, 3) == 0);
    gr_text_layout_unref(layout);

    /* Selecting across a direction change gives one rectangle per visual piece. */
    layout = layout_of(system, STR("abc \xD7\x90\xD7\x91\xD7\x92 def"), gr_text_layout_options_default());
    gr_rect pieces[4];
    size_t count = gr_text_layout_get_range_rects(layout, (gr_text_range){2, 6}, pieces, 4);
    CHECK(count == 2);
    CHECK(pieces[0].origin.x + pieces[0].size.width < pieces[1].origin.x);
    gr_text_layout_unref(layout);
    gr_text_system_unref(system);
}

static void concurrent_queries(void)
{
    gr_text_system *system = gr_macos_text_system_new();
    gr_text_layout *layout = layout_of(system, (gr_string){pangram, sizeof(pangram) - 1}, width(120));
    double storage[8] = {0};
    double *heights = storage;
    dispatch_apply(8, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^(size_t i) {
      heights[i] = gr_text_layout_get_metrics(layout).size.height;
    });
    for (size_t i = 1; i < 8; ++i)
        CHECK(heights[i] == heights[0] && heights[0] > 0);
    gr_text_layout_unref(layout);
    gr_text_system_unref(system);
}

typedef struct
{
    gr_text_layout *layout;
    gr_point origin;
} text_drawing;

static void draw_text(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    (void)info;
    text_drawing *d = userdata;
    OK(gr_canvas_draw_text_layout(canvas, d->layout, d->origin, (gr_color){1, 0, 0, 1}));
}

static void rendering(void)
{
    @autoreleasepool
    {
        gr_text_system *system = gr_macos_text_system_new();
        /* A colored first word must not leak its color into the rest of the line. */
        gr_text_span blue = {{0, 5}, GR_TEXT_STYLE_COLOR | GR_TEXT_STYLE_DECORATIONS, gr_text_style_default()};
        blue.style.color = (gr_color){0, 0, 1, 1};
        blue.style.decorations = GR_TEXT_DECORATION_UNDERLINE;
        text_drawing d = {
            layout_with(system, STR("Hello world"), (gr_text_span_list){&blue, 1}, gr_text_layout_options_default()),
            {10, 10}};
        gr_text_layout_metrics m = gr_text_layout_get_metrics(d.layout);

        gr_scene *scene = gr_scene_new((gr_size){200, 60});
        gr_layer *layer = gr_layer_new(scene, GR_LAYER_CONTENT);
        OK(gr_layer_set_frame(layer, (gr_rect){{0, 0}, {200, 60}}));
        OK(gr_content_layer_set_drawer(layer, &(gr_drawer){&d, draw_text, NULL}));
        OK(gr_layer_add_child(gr_scene_get_root(scene), layer));
        gr_macos_host *host = gr_macos_offscreen_new(scene, (gr_size){200, 60}, 2);
        gr_macos_pixels pixels = {0};
        CHECK(gr_macos_host_render(host, (gr_size){200, 60}, 2, &pixels));

        size_t red = 0, blue_pixels = 0, outside = 0, underline = 0;
        double underline_y = 2 * (10 + m.first_baseline);
        for (size_t y = 0; y < pixels.height; ++y)
            for (size_t x = 0; x < pixels.width; ++x)
            {
                uint8_t *p = pixels.data + (y * pixels.width + x) * 4;
                if (p[3] < 128)
                    continue;
                double lx = x / 2.0 - 10, ly = y / 2.0 - 10;
                if (lx < -1 || ly < -1 || lx > m.size.width + 1 || ly > m.size.height + 1)
                    ++outside;
                if (p[0] > 200 && p[2] < 60)
                    ++red;
                if (p[2] > 200 && p[0] < 60)
                {
                    ++blue_pixels;
                    if (y > underline_y && y < underline_y + 8)
                        ++underline;
                }
            }
        CHECK(red > 50 && blue_pixels > 50 && outside == 0);
        CHECK(underline > 40); /* An underline spans the blue word below its baseline. */
        gr_macos_pixels_clear(&pixels);
        gr_layer_unref(layer);
        gr_scene_unref(scene);
        gr_macos_host_unref(host);
        gr_text_layout_unref(d.layout);
        gr_text_system_unref(system);
    }
}

int main(int argc, char **argv)
{
    @autoreleasepool
    {
        const gr_test_case cases[] = {
            GR_TEST_CASE(metrics),
            GR_TEST_CASE(empty_and_trailing_lines),
            GR_TEST_CASE(wrapping),
            GR_TEST_CASE(truncation),
            GR_TEST_CASE(alignment),
            GR_TEST_CASE(carets),
            GR_TEST_CASE(concurrent_queries),
            GR_TEST_CASE(rendering),
        };
        return gr_test_run("macos.text", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
    }
}
