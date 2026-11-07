#include "test.h"

#include <graphite/backend.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STR(literal) ((gr_string){literal, sizeof(literal) - 1})

static gr_attributed_text *plain(gr_string utf8)
{
    gr_text_style base = gr_text_style_default();
    return gr_attributed_text_new(utf8, &base, (gr_text_span_list){NULL, 0});
}

static gr_attributed_text *with_spans(gr_string utf8, const gr_text_span *spans, size_t count)
{
    gr_text_style base = gr_text_style_default();
    return gr_attributed_text_new(utf8, &base, (gr_text_span_list){spans, count});
}

static bool family_is(gr_string family, const char *expected)
{
    return family.count == strlen(expected) && !memcmp(family.data, expected, family.count);
}

static void attributed_text_validation(void)
{
    const char *invalid[] = {"\x80",     "\xC0\x80", "\xE0\x80\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80",
                             "\xE2\x82", "\xFF"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        CHECK(!plain((gr_string){invalid[i], strlen(invalid[i])}));
    CHECK(!plain((gr_string){NULL, 1}));

    gr_attributed_text *t = plain(STR("a\0b\xF0\x9F\x98\x80"));
    CHECK(t);
    CHECK(gr_attributed_text_get_utf8(t).count == 7);
    gr_attributed_text *empty = plain((gr_string){NULL, 0});
    CHECK(empty && gr_attributed_text_get_spans(empty).count == 0);
    CHECK(gr_attributed_text_get_id(t) != gr_attributed_text_get_id(empty));
    gr_attributed_text_unref(empty);
    gr_attributed_text_unref(t);

    gr_text_style base = gr_text_style_default();
    CHECK(!gr_attributed_text_new(STR("a"), NULL, (gr_text_span_list){NULL, 0}));
    base.size = 0;
    CHECK(!gr_attributed_text_new(STR("a"), &base, (gr_text_span_list){NULL, 0}));
    base = gr_text_style_default();
    base.weight = 1001;
    CHECK(!gr_attributed_text_new(STR("a"), &base, (gr_text_span_list){NULL, 0}));
    base = gr_text_style_default();
    base.decorations = 4;
    CHECK(!gr_attributed_text_new(STR("a"), &base, (gr_text_span_list){NULL, 0}));
    base = gr_text_style_default();
    base.color.r = NAN; /* The base color is ignored. */
    t = gr_attributed_text_new(STR("a"), &base, (gr_text_span_list){NULL, 0});
    CHECK(t && gr_attributed_text_get_base(t).color.r == 0);
    gr_attributed_text_unref(t);

    gr_text_span span = {{0, 1}, GR_TEXT_STYLE_SIZE, gr_text_style_default()};
    span.style.color.r = NAN; /* Unselected fields are ignored. */
    t = with_spans(STR("ab"), &span, 1);
    CHECK(t);
    gr_attributed_text_unref(t);
    span.fields = GR_TEXT_STYLE_COLOR;
    CHECK(!with_spans(STR("ab"), &span, 1));
    span.fields = 128;
    CHECK(!with_spans(STR("ab"), &span, 1));
    span = (gr_text_span){{2, 1}, GR_TEXT_STYLE_SIZE, gr_text_style_default()};
    CHECK(!with_spans(STR("ab"), &span, 1));
    span.range = (gr_text_range){0, 3};
    CHECK(!with_spans(STR("ab"), &span, 1));
    span.range = (gr_text_range){1, 2};
    CHECK(!with_spans(STR("\xC3\xA9"), &span, 1));
    span.range = (gr_text_range){1, 1};
    CHECK(!with_spans(STR("\xC3\xA9"), &span, 1));
    span.range = (gr_text_range){2, 2};
    span.style.size = NAN; /* Empty spans are ignored. */
    t = with_spans(STR("\xC3\xA9"), &span, 1);
    CHECK(t && gr_attributed_text_get_spans(t).count == 0);
    gr_attributed_text_unref(t);
    CHECK(!gr_attributed_text_new(STR("a"), &base, (gr_text_span_list){NULL, 1}));
}

static void attributed_text_spans(void)
{
    char family[] = "Serif Family";
    gr_text_span spans[4] = {
        {{0, 6}, GR_TEXT_STYLE_SIZE | GR_TEXT_STYLE_FAMILY, gr_text_style_default()},
        {{2, 8}, GR_TEXT_STYLE_COLOR, gr_text_style_default()},
        {{4, 4}, GR_TEXT_STYLE_SIZE, gr_text_style_default()},
        {{4, 6}, 0, gr_text_style_default()},
    };
    spans[0].style.size = 20;
    spans[0].style.family = (gr_string){family, strlen(family)};
    spans[1].style.color = (gr_color){1, 0, 0, 1};
    spans[1].style.size = NAN;       /* Unselected: replaced by the base value. */
    spans[1].style.family.count = 3; /* Unselected: never read. */
    gr_text_style base = gr_text_style_default();
    base.family = (gr_string){NULL, 0};
    base.weight = 700;
    gr_attributed_text *t = gr_attributed_text_new(STR("0123456789"), &base, (gr_text_span_list){spans, 4});
    CHECK(t);
    family[0] = 'X';

    gr_text_style b = gr_attributed_text_get_base(t);
    CHECK(family_is(b.family, "system-ui") && b.weight == 700 && b.size == 14);
    gr_text_span_list copies = gr_attributed_text_get_spans(t);
    CHECK(copies.count == 2); /* Empty and fieldless spans are dropped; order is kept. */
    CHECK(copies.data[0].range.start == 0 && copies.data[0].range.end == 6);
    CHECK(copies.data[0].fields == (GR_TEXT_STYLE_SIZE | GR_TEXT_STYLE_FAMILY));
    CHECK(family_is(copies.data[0].style.family, "Serif Family") && copies.data[0].style.size == 20);
    CHECK(copies.data[0].style.weight == 700);
    CHECK(copies.data[1].fields == GR_TEXT_STYLE_COLOR && copies.data[1].style.color.r == 1);
    CHECK(copies.data[1].style.size == 14 && family_is(copies.data[1].style.family, "system-ui"));
    gr_attributed_text_unref(t);
}

static void attributed_text_utf16(void)
{
    /* a (1 byte), é (2 bytes), € (3 bytes), 😀 (4 bytes, surrogate pair). */
    gr_attributed_text *t = plain(STR("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"));
    gr_utf16 units = gr_attributed_text_get_utf16(t);
    uint16_t expected[] = {'a', 0xE9, 0x20AC, 0xD83D, 0xDE00};
    CHECK(units.count == 5 && !memcmp(units.data, expected, sizeof(expected)));
    CHECK(gr_attributed_text_get_utf16(t).data == units.data);
    size_t to16[][2] = {{0, 0}, {1, 1}, {2, 1}, {3, 2}, {4, 2}, {5, 2}, {6, 3}, {9, 3}, {10, 5}, {99, 5}};
    for (size_t i = 0; i < sizeof(to16) / sizeof(to16[0]); ++i)
        CHECK(gr_attributed_text_utf16_index(t, to16[i][0]) == to16[i][1]);
    size_t to8[][2] = {{0, 0}, {1, 1}, {2, 3}, {3, 6}, {4, 6}, {5, 10}, {99, 10}};
    for (size_t i = 0; i < sizeof(to8) / sizeof(to8[0]); ++i)
        CHECK(gr_attributed_text_utf8_index(t, to8[i][0]) == to8[i][1]);
    gr_attributed_text_unref(t);

    t = plain(STR("ascii"));
    CHECK(gr_attributed_text_utf16_index(t, 3) == 3 && gr_attributed_text_utf8_index(t, 4) == 4);
    CHECK(gr_attributed_text_get_utf16(t).data[4] == 'i');
    gr_attributed_text_unref(t);
    t = plain((gr_string){NULL, 0});
    CHECK(gr_attributed_text_get_utf16(t).count == 0);
    gr_attributed_text_unref(t);
}

/* A fake text system: one line per "\n"-separated paragraph, 7 pt per byte, 10 pt lines. Caret stops are code
 * point boundaries not followed by a combining acute accent (U+0301, "\xCC\x81"). */

typedef struct
{
    gr_text_system *system;
    int layouts, destroyed;
} fake_system;

typedef struct
{
    fake_system *owner;
    gr_string utf8;
    size_t lines, visible_end;
    bool truncated;
    int metrics_calls;
    size_t next_input, previous_input;
    gr_text_position caret_input;
    gr_text_range range_input;
} fake_layout;

static bool fake_stop(gr_string s, size_t i)
{
    if (i == 0 || i >= s.count)
        return true;
    unsigned char c = (unsigned char)s.data[i];
    if ((c & 0xC0) == 0x80)
        return false;
    return !(c == 0xCC && i + 1 < s.count && (unsigned char)s.data[i + 1] == 0x81);
}

static gr_text_layout_metrics fake_metrics(void *data)
{
    fake_layout *f = data;
    ++f->metrics_calls;
    return (gr_text_layout_metrics){.size = {f->visible_end * 7.0, f->lines * 10.0},
                                    .line_count = f->lines,
                                    .first_baseline = 8,
                                    .last_baseline = 8 + 10.0 * (double)(f->lines - 1),
                                    .visible = {0, f->visible_end},
                                    .truncated = f->truncated};
}

static void fake_line(void *data, size_t index, gr_text_line *out)
{
    fake_layout *f = data;
    size_t start = 0, line = 0;
    while (line < index)
        if (f->utf8.data[start++] == '\n')
            ++line;
    size_t end = start;
    while (end < f->visible_end && f->utf8.data[end] != '\n')
        ++end;
    if (end < f->visible_end)
        ++end;
    *out = (gr_text_line){{start, end}, {{0, 10.0 * (double)index}, {(end - start) * 7.0, 10}}, 8 + 10.0 * index, 8, 2};
}

static size_t fake_next(void *data, size_t index)
{
    fake_layout *f = data;
    CHECK(index < f->visible_end);
    f->next_input = index;
    size_t i = index + 1;
    while (i < f->visible_end && !fake_stop(f->utf8, i))
        ++i;
    return i;
}

static size_t fake_previous(void *data, size_t index)
{
    fake_layout *f = data;
    CHECK(index > 0 && index <= f->visible_end);
    f->previous_input = index;
    size_t i = index - 1;
    while (i > 0 && !fake_stop(f->utf8, i))
        --i;
    return i;
}

static gr_text_position fake_hit(void *data, gr_point point)
{
    (void)data;
    CHECK(isfinite(point.x) && isfinite(point.y));
    return (gr_text_position){1, GR_TEXT_AFFINITY_UPSTREAM};
}

static gr_rect fake_caret(void *data, gr_text_position position)
{
    fake_layout *f = data;
    CHECK(fake_stop(f->utf8, position.index) && position.index <= f->visible_end);
    f->caret_input = position;
    return (gr_rect){{position.index * 7.0, 0}, {0, 10}};
}

static size_t fake_range_rects(void *data, gr_text_range range, gr_rect *rects, size_t capacity)
{
    fake_layout *f = data;
    CHECK(range.start < range.end && range.end <= f->visible_end);
    CHECK(fake_stop(f->utf8, range.start) && fake_stop(f->utf8, range.end));
    CHECK(capacity ? rects != NULL : rects == NULL);
    f->range_input = range;
    if (capacity)
        rects[0] = (gr_rect){{range.start * 7.0, 0}, {(range.end - range.start) * 7.0, 10}};
    return 1;
}

static void fake_layout_destroy(void *data)
{
    fake_layout *f = data;
    ++f->owner->destroyed;
    free(f);
}

static const gr_text_layout_vtable fake_layout_vtable = {
    fake_metrics, fake_line, fake_next, fake_previous, fake_hit, fake_caret, fake_range_rects, fake_layout_destroy,
};

static gr_text_layout *fake_layout_text(void *data, gr_attributed_text *text, const gr_text_layout_options *options)
{
    fake_system *s = data;
    fake_layout *f = calloc(1, sizeof(*f));
    CHECK(f);
    f->owner = s;
    f->utf8 = gr_attributed_text_get_utf8(text);
    f->lines = 1;
    f->visible_end = f->utf8.count;
    for (size_t i = 0; i < f->utf8.count; ++i)
        if (f->utf8.data[i] == '\n' && i + 1 < f->utf8.count)
        {
            if (options->max_lines && f->lines == options->max_lines)
            {
                f->visible_end = i + 1;
                f->truncated = true;
                break;
            }
            ++f->lines;
        }
    ++s->layouts;
    gr_text_layout *layout = gr_text_layout_new(s->system, text, options, &fake_layout_vtable, f);
    CHECK(layout);
    return layout;
}

static void fake_system_destroy(void *data)
{
    ++((fake_system *)data)->destroyed;
}

static gr_text_system *fake_system_new(fake_system *state)
{
    *state = (fake_system){0};
    gr_text_system_vtable vtable = {fake_layout_text, fake_system_destroy};
    state->system = gr_text_system_new(&vtable, state);
    return state->system;
}

static void text_system_lifetime(void)
{
    gr_text_system_vtable missing = {fake_layout_text, NULL};
    CHECK(!gr_text_system_new(&missing, NULL));
    CHECK(!gr_text_system_new(NULL, NULL));

    fake_system state;
    gr_text_system *system = fake_system_new(&state);
    gr_attributed_text *text = plain(STR("one\ntwo\nthree"));
    gr_text_layout_options options = gr_text_layout_options_default();
    CHECK(!gr_text_system_layout(system, NULL, &options));
    CHECK(!gr_text_system_layout(system, text, NULL));
    options.has_max_width = true;
    options.max_width = -1;
    CHECK(!gr_text_system_layout(system, text, &options));
    options.max_width = INFINITY;
    CHECK(!gr_text_system_layout(system, text, &options));
    options = gr_text_layout_options_default();
    options.align = (gr_text_align)7;
    CHECK(!gr_text_system_layout(system, text, &options));
    CHECK(state.layouts == 0);

    options = gr_text_layout_options_default();
    options.max_lines = 2;
    gr_text_layout *layout = gr_text_system_layout(system, text, &options);
    CHECK(layout && state.layouts == 1);
    fake_layout *f = gr_text_layout_get_data(layout);
    CHECK(f->metrics_calls == 0); /* Creating a layout performs no queries. */
    CHECK(gr_text_layout_get_vtable(layout) == &fake_layout_vtable);
    CHECK(gr_text_layout_get_system(layout) == system && gr_text_layout_get_text(layout) == text);
    CHECK(gr_text_layout_get_options(layout).max_lines == 2);
    CHECK(gr_text_layout_get_id(layout) != 0);

    gr_text_layout_vtable incomplete = fake_layout_vtable;
    incomplete.hit_test = NULL;
    CHECK(!gr_text_layout_new(system, text, &options, &incomplete, NULL));
    CHECK(!gr_text_layout_new(system, NULL, &options, &fake_layout_vtable, NULL));

    /* Layouts keep their text and system alive. */
    gr_attributed_text_unref(text);
    gr_text_system_unref(system);
    CHECK(state.destroyed == 0);
    gr_text_layout_metrics m = gr_text_layout_get_metrics(layout);
    CHECK(m.line_count == 2 && m.truncated && m.visible.end == 8);
    gr_text_layout_unref(layout);
    CHECK(state.destroyed == 2);
}

static void text_layout_queries(void)
{
    fake_system state;
    gr_text_system *system = fake_system_new(&state);
    /* "e" + U+0301 forms one cluster at [1, 4); "é" is precomposed at [5, 7). */
    gr_attributed_text *text = plain(STR("xe\xCC\x81y\xC3\xA9\nz"));
    gr_text_layout_options options = gr_text_layout_options_default();
    gr_text_layout *layout = gr_text_system_layout(system, text, &options);
    fake_layout *f = gr_text_layout_get_data(layout);

    gr_text_line line;
    OK(gr_text_layout_get_line(layout, 1, &line));
    CHECK(line.range.start == 8 && line.range.end == 9 && line.baseline == 18);
    line.baseline = 99;
    CHECK(gr_text_layout_get_line(layout, 2, &line) == GR_ERROR_ARGUMENT && line.baseline == 0);
    CHECK(gr_text_layout_get_line(layout, 0, NULL) == GR_ERROR_ARGUMENT);

    CHECK(gr_text_layout_next_caret_stop(layout, 0) == 1);
    CHECK(gr_text_layout_next_caret_stop(layout, 1) == 4);
    CHECK(gr_text_layout_next_caret_stop(layout, 2) == 4 && f->next_input == 2);
    CHECK(gr_text_layout_next_caret_stop(layout, 9) == 9);
    CHECK(gr_text_layout_next_caret_stop(layout, 99) == 9);
    CHECK(gr_text_layout_previous_caret_stop(layout, 4) == 1);
    CHECK(gr_text_layout_previous_caret_stop(layout, 3) == 0); /* 3 floors to 1 first. */
    CHECK(gr_text_layout_previous_caret_stop(layout, 6) == 4); /* 6 floors to 5 first. */
    CHECK(gr_text_layout_previous_caret_stop(layout, 0) == 0);
    CHECK(gr_text_layout_previous_caret_stop(layout, 99) == 8);

    gr_text_position hit = gr_text_layout_hit_test(layout, (gr_point){NAN, 0});
    CHECK(hit.index == 0 && hit.affinity == GR_TEXT_AFFINITY_DOWNSTREAM);
    hit = gr_text_layout_hit_test(layout, (gr_point){3, 4});
    CHECK(hit.index == 1 && hit.affinity == GR_TEXT_AFFINITY_UPSTREAM);

    gr_rect caret;
    OK(gr_text_layout_get_caret_rect(layout, (gr_text_position){3, GR_TEXT_AFFINITY_UPSTREAM}, &caret));
    CHECK(f->caret_input.index == 1 && f->caret_input.affinity == GR_TEXT_AFFINITY_UPSTREAM && caret.origin.x == 7);
    OK(gr_text_layout_get_caret_rect(layout, (gr_text_position){99, GR_TEXT_AFFINITY_DOWNSTREAM}, &caret));
    CHECK(f->caret_input.index == 9);
    CHECK(gr_text_layout_get_caret_rect(layout, (gr_text_position){0, (gr_text_affinity)5}, &caret) ==
          GR_ERROR_ARGUMENT);
    CHECK(caret.size.height == 0);

    gr_rect rects[2];
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){2, 6}, rects, 2) == 1);
    CHECK(f->range_input.start == 1 && f->range_input.end == 7);
    CHECK(rects[0].origin.x == 7 && rects[0].size.width == 42);
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){4, 99}, NULL, 2) == 1);
    CHECK(f->range_input.start == 4 && f->range_input.end == 9);
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){2, 3}, NULL, 0) == 1); /* Widened to [1, 4). */
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){4, 4}, rects, 2) == 0);
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){5, 4}, rects, 2) == 0);
    gr_text_layout_unref(layout);

    /* Truncated content has no caret stops or rectangles. */
    options.max_lines = 1;
    layout = gr_text_system_layout(system, text, &options);
    CHECK(gr_text_layout_get_metrics(layout).visible.end == 8);
    CHECK(gr_text_layout_next_caret_stop(layout, 8) == 8);
    CHECK(gr_text_layout_get_range_rects(layout, (gr_text_range){8, 9}, rects, 2) == 0);
    gr_text_layout_unref(layout);

    gr_attributed_text_unref(text);
    gr_text_system_unref(system);
    CHECK(state.destroyed == 3);
}

static void canvas_text(void)
{
    fake_system state;
    gr_text_system *system = fake_system_new(&state);
    gr_attributed_text *text = plain(STR("hello"));
    gr_text_layout_options options = gr_text_layout_options_default();
    gr_text_layout *layout = gr_text_system_layout(system, text, &options);
    gr_attributed_text_unref(text);
    gr_text_system_unref(system);

    gr_canvas *canvas = gr_canvas_new();
    gr_color red = {1, 0, 0, 1};
    CHECK(gr_canvas_draw_text_layout(canvas, NULL, (gr_point){0, 0}, red) == GR_ERROR_ARGUMENT);
    CHECK(gr_canvas_draw_text_layout(canvas, layout, (gr_point){INFINITY, 0}, red) == GR_ERROR_ARGUMENT);
    CHECK(gr_canvas_draw_text_layout(canvas, layout, (gr_point){0, 0}, (gr_color){2, 0, 0, 1}) == GR_ERROR_ARGUMENT);
    OK(gr_canvas_draw_text_layout(canvas, layout, (gr_point){3, 4}, red));
    gr_text_layout_unref(layout);
    CHECK(state.destroyed == 0); /* The recording owns a reference. */
    gr_display_list *list = gr_canvas_finish(canvas);
    gr_draw_op_list ops = gr_display_list_get_ops(list);
    CHECK(ops.count == 1 && ops.data[0].kind == GR_DRAW_TEXT_LAYOUT);
    CHECK(ops.data[0].value.text_layout.layout == layout && ops.data[0].value.text_layout.origin.y == 4);
    CHECK(ops.data[0].value.text_layout.color.r == 1);
    gr_display_list_unref(list);
    CHECK(state.destroyed == 2);

    /* Unfinished recordings release their layouts too. */
    system = fake_system_new(&state);
    text = plain(STR("x"));
    layout = gr_text_system_layout(system, text, &options);
    OK(gr_canvas_draw_text_layout(canvas, layout, (gr_point){0, 0}, red));
    gr_text_layout_unref(layout);
    gr_attributed_text_unref(text);
    gr_text_system_unref(system);
    gr_canvas_unref(canvas);
    CHECK(state.destroyed == 2);
}

int main(int argc, char **argv)
{
    const gr_test_case cases[] = {
        GR_TEST_CASE(attributed_text_validation), GR_TEST_CASE(attributed_text_spans),
        GR_TEST_CASE(attributed_text_utf16),      GR_TEST_CASE(text_system_lifetime),
        GR_TEST_CASE(text_layout_queries),        GR_TEST_CASE(canvas_text),
    };
    return gr_test_run("text", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
