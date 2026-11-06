#include "containers.h"
#include "test.h"
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

static gr_paint *solid(void)
{
    gr_paint_desc d = {.kind = GR_PAINT_SOLID, .value.solid = {1, 0, 0, 1}};
    return gr_paint_new(&d);
}

static void resources(void)
{
    gr_gradient_stop stops[] = {{0, {0, 0, 0, 1}}, {1, {1, 1, 1, 1}}};
    gr_paint_desc d = {.kind = GR_PAINT_LINEAR};
    d.value.linear.end = (gr_point){1, 0};
    d.value.linear.stops = (gr_gradient_stop_list){stops, 2};
    gr_paint *paint = gr_paint_new(&d);
    CHECK(paint);
    stops[1].offset = 0.5f;
    CHECK(gr_paint_get_desc(paint).value.linear.stops.data[1].offset == 1);
    double dash[] = {2, 3};
    gr_stroke_style style = gr_stroke_style_default();
    style.dash = (gr_double_list){dash, 2};
    gr_stroke *stroke = gr_stroke_new(paint, &style);
    CHECK(stroke);
    dash[0] = 9;
    gr_paint_unref(paint);
    CHECK(gr_stroke_get_style(stroke).dash.data[0] == 2);
    CHECK(gr_paint_get_desc(gr_stroke_get_paint(stroke)).kind == GR_PAINT_LINEAR);
    gr_stroke_unref(stroke);
    stops[1].offset = -1;
    CHECK(!gr_paint_new(&d));
    d.kind = (gr_paint_kind)99;
    CHECK(!gr_paint_new(&d));
    paint = solid();
    dash[0] = dash[1] = 0;
    CHECK(!gr_stroke_new(paint, &style));
    style.dash = (gr_double_list){NULL, 0};
    style.width = NAN;
    CHECK(!gr_stroke_new(paint, &style));
    gr_paint_unref(paint);

    uint8_t pixels[] = {20, 30, 40, 255};
    gr_image *image = gr_image_new(1, 1, 2, (gr_bytes){pixels, 4});
    CHECK(image);
    pixels[0] = 99;
    CHECK(gr_image_get_pixels(image).data[0] == 20);
    CHECK(gr_image_get_width(image) == 1 && gr_image_get_height(image) == 1);
    CHECK(gr_image_get_scale(image) == 2);
    CHECK(!gr_image_new(UINT32_MAX, UINT32_MAX, 1, (gr_bytes){NULL, 0}));
    CHECK(!gr_image_new(1, 1, INFINITY, (gr_bytes){pixels, 4}));
    CHECK(!gr_image_new(1, 1, 1, (gr_bytes){NULL, 4}));
    CHECK(!gr_image_new(1, 1, 1, (gr_bytes){pixels, 3}));
    gr_image_unref(image);
}

static void paths(void)
{
    gr_path_builder *b = gr_path_builder_new();
    gr_rect box = {{0, 0}, {20, 10}};
    CHECK(gr_path_builder_add_rounded_rect(b, box, 100, GR_CORNERS_ALL) == GR_OK);
    gr_path *p = gr_path_builder_finish(b);
    gr_path_element_list e = gr_path_get_elements(p);
    CHECK(e.count == 10 && e.data[0].p[0].x == 5);
    CHECK(e.data[9].verb == GR_PATH_CLOSE);
    CHECK(gr_path_builder_add_rect(b, box) == GR_OK);
    gr_path_element invalid[] = {{GR_PATH_MOVE, {{1, 2}}}, {(gr_path_verb)99, {{0, 0}}}};
    CHECK(gr_path_builder_append(b, (gr_path_element_list){invalid, 2}) == GR_ERROR_ARGUMENT);
    CHECK(gr_path_builder_add_ellipse(b, (gr_rect){{0, 0}, {-1, 2}}) == GR_ERROR_ARGUMENT);
    gr_path *rect = gr_path_builder_finish(b);
    CHECK(gr_path_get_elements(rect).count == 5);
    CHECK(gr_path_get_id(rect) != gr_path_get_id(p));
    CHECK(e.count == 10 && e.data[0].p[0].x == 5);
    for (unsigned corners = 0; corners <= GR_CORNERS_ALL; ++corners)
    {
        CHECK(gr_path_builder_add_rounded_rect(b, box, 3, corners) == GR_OK);
        gr_path *r = gr_path_builder_finish(b);
        size_t curves = 0;
        e = gr_path_get_elements(r);
        for (size_t i = 0; i < e.count; ++i)
            curves += e.data[i].verb == GR_PATH_CUBIC;
        unsigned bits = 0;
        for (unsigned c = corners; c; c >>= 1)
            bits += c & 1;
        CHECK(curves == bits);
        gr_path_unref(r);
    }
    CHECK(gr_path_builder_add_ellipse(b, box) == GR_OK);
    gr_path *ellipse = gr_path_builder_finish(b);
    CHECK(gr_path_get_elements(ellipse).count == 6);
    gr_path_builder_unref(b);
    gr_path_unref(ellipse);
    gr_path_unref(rect);
    gr_path_unref(p);
}

static gr_display_list *record(void)
{
    gr_path_builder *b = gr_path_builder_new();
    gr_rect r = {{1, 2}, {10, 20}};
    CHECK(gr_path_builder_add_rect(b, r) == GR_OK);
    gr_path *path = gr_path_builder_finish(b);
    gr_path_builder_unref(b);
    gr_paint *paint = solid();
    gr_stroke_style style = gr_stroke_style_default();
    gr_stroke *stroke = gr_stroke_new(paint, &style);
    uint8_t pixel[] = {0, 0, 0, 255};
    gr_image *image = gr_image_new(1, 1, 1, (gr_bytes){pixel, 4});
    gr_canvas *c = gr_canvas_new();
    gr_canvas_restore(c);
    gr_canvas_save(c);
    CHECK(gr_canvas_concat_transform(c, (gr_affine){1, 0, 0, 1, 0, 0}) == GR_OK);
    CHECK(gr_canvas_concat_transform(c, (gr_affine){1, 0, 0, 1, 3, 4}) == GR_OK);
    CHECK(gr_canvas_set_alpha(c, 2) == GR_OK);
    CHECK(gr_canvas_set_alpha(c, NAN) == GR_ERROR_ARGUMENT);
    CHECK(gr_canvas_clip_rect(c, r) == GR_OK);
    CHECK(gr_canvas_clip_path(c, path, GR_FILL_NONZERO) == GR_OK);
    CHECK(gr_canvas_fill_path(c, path, GR_FILL_EVEN_ODD, paint) == GR_OK);
    CHECK(gr_canvas_fill_path(c, path, (gr_fill_rule)99, paint) == GR_ERROR_ARGUMENT);
    CHECK(gr_canvas_stroke_path(c, path, stroke) == GR_OK);
    CHECK(gr_canvas_draw_image(c, image, r) == GR_OK);
    CHECK(gr_canvas_fill_rect(c, r, paint) == GR_OK);
    gr_path_unref(path);
    gr_stroke_unref(stroke);
    gr_paint_unref(paint);
    gr_image_unref(image);
    gr_display_list *list = gr_canvas_finish(c);
    gr_display_list *empty = gr_canvas_finish(c);
    CHECK(gr_display_list_is_empty(empty));
    CHECK(gr_display_list_get_id(empty) != gr_display_list_get_id(list));
    gr_display_list_unref(empty);
    /* Dropping an unfinished recording must release its resources as well. */
    paint = solid();
    CHECK(gr_canvas_fill_rect(c, r, paint) == GR_OK);
    gr_paint_unref(paint);
    gr_canvas_unref(c);
    gr_draw_op_list ops = gr_display_list_get_ops(list);
    CHECK(ops.count == 10);
    CHECK(ops.data[0].kind == GR_DRAW_SAVE && ops.data[9].kind == GR_DRAW_RESTORE);
    CHECK(ops.data[2].value.alpha == 1);
    CHECK(gr_path_get_elements(ops.data[5].value.fill_path.path).count == 5);
    CHECK(gr_paint_get_desc(ops.data[5].value.fill_path.paint).value.solid.r == 1);
    CHECK(gr_image_get_pixels(ops.data[7].value.image.image).data[3] == 255);
    return list;
}

#if defined(_WIN32)
static DWORD WINAPI worker(void *arg)
#else
static void *worker(void *arg)
#endif
{
    gr_display_list *list = arg;
    for (int i = 0; i < 10000; ++i)
    {
        gr_display_list *copy = gr_display_list_ref(list);
        gr_draw_op_list ops = gr_display_list_get_ops(copy);
        gr_path *path = gr_path_ref(ops.data[5].value.fill_path.path);
        CHECK(gr_path_get_elements(path).count == 5);
        gr_path_unref(path);
        gr_paint_unref(gr_paint_ref(ops.data[5].value.fill_path.paint));
        gr_stroke_unref(gr_stroke_ref(ops.data[6].value.stroke_path.stroke));
        gr_image_unref(gr_image_ref(ops.data[7].value.image.image));
        gr_display_list_unref(copy);
    }
    gr_display_list_unref(list);
    return 0;
}

static void shared_resources(void)
{
    gr_display_list *list = record();
#if defined(_WIN32)
    HANDLE threads[4];
    for (size_t i = 0; i < 4; ++i)
    {
        threads[i] = CreateThread(NULL, 0, worker, gr_display_list_ref(list), 0, NULL);
        CHECK(threads[i] != NULL);
    }
    gr_display_list_unref(list);
    for (size_t i = 0; i < 4; ++i)
    {
        CHECK(WaitForSingleObject(threads[i], INFINITE) == WAIT_OBJECT_0);
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[4];
    for (size_t i = 0; i < 4; ++i)
        CHECK(pthread_create(&threads[i], NULL, worker, gr_display_list_ref(list)) == 0);
    gr_display_list_unref(list);
    for (size_t i = 0; i < 4; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
#endif
}

static void containers(void)
{
    struct entry
    {
        unsigned key;
        UT_hash_handle hh;
    } *map = NULL, *found, *e, *tmp;

    for (unsigned i = 0; i < 1000; ++i)
    {
        e = gr_alloc(1, sizeof(*e));
        e->key = i;
        HASH_ADD(hh, map, key, sizeof(e->key), e);
    }
    unsigned key = 731;
    HASH_FIND(hh, map, &key, sizeof(key), found);
    CHECK(found && found->key == key);
    HASH_ITER(hh, map, e, tmp)
    {
        HASH_DEL(map, e);
        free(e);
    }
    CHECK(map == NULL);
}

static void null_references(void)
{
    gr_path_unref(NULL);
    gr_image_unref(NULL);
    gr_paint_unref(NULL);
    gr_stroke_unref(NULL);
    gr_canvas_unref(NULL);
    gr_path_builder_unref(NULL);
    gr_display_list_unref(NULL);
    CHECK(gr_path_ref(NULL) == NULL && gr_canvas_ref(NULL) == NULL);
}

int main(int argc, char **argv)
{
    const gr_test_case cases[] = {
        GR_TEST_CASE(resources),  GR_TEST_CASE(paths),           GR_TEST_CASE(shared_resources),
        GR_TEST_CASE(containers), GR_TEST_CASE(null_references),
    };
    return gr_test_run("resources", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
