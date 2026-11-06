#include <graphite/backend.h>
#include "test.h"
#include <stdlib.h>

typedef struct
{
    gr_scene_link *scene;
    bool scheduled;
} example_backend;

static void request_commit(void *userdata)
{
    example_backend *backend = userdata;
    backend->scheduled = true;
}

static void apply(void *userdata, const gr_commit_batch *batch)
{
    (void)userdata;
    for (size_t i = 0; i < batch->updates.count; ++i)
    {
        const gr_layer_update *update = &batch->updates.data[i];
        if (update->resolved.content.kind == GR_CONTENT_DRAWN)
        {
            gr_draw_info info = gr_resolved_get_draw_info(&update->resolved, batch->backing_scale);
            gr_display_list *list = NULL;
            gr_result result = gr_drawer_provider_draw(batch->drawers, update->layer,
                                                       update->resolved.content.value.display_version, &info, &list);
            if (result == GR_OK)
            {
                gr_draw_op_list ops = gr_display_list_get_ops(list);
                /* A renderer consumes ops before releasing their owner. */
                (void)ops;
                gr_display_list_unref(list);
            }
        }
    }
}

static void destroy_backend(void *userdata)
{
    example_backend *backend = userdata;
    gr_scene_link_unref(backend->scene);
    free(backend);
}

static void draw(void *userdata, gr_canvas *canvas, const gr_draw_info *info)
{
    gr_result result = gr_canvas_fill_rect(canvas, info->bounds, userdata);
    CHECK(result == GR_OK);
    (void)result;
}

static void destroy_paint(void *userdata)
{
    gr_paint_unref(userdata);
}

void graphite_lifecycle_example(void)
{
    gr_scene *scene = gr_scene_new((gr_size){400, 300});
    CHECK(scene != NULL);
    example_backend *backend = calloc(1, sizeof(*backend));
    if (backend == NULL)
        abort();
    backend->scene = gr_scene_get_link(scene);
    gr_backend_vtable vtable = {request_commit, apply, destroy_backend};
    gr_result result = gr_scene_set_backend(scene, &vtable, backend);
    if (result != GR_OK)
    {
        destroy_backend(backend); /* Failed installation does not transfer ownership. */
        gr_scene_unref(scene);
        return;
    }
    gr_layer *layer = gr_layer_new(scene, GR_LAYER_CONTENT);
    gr_rect bounds = {{0, 0}, {100, 20}};
    result = gr_layer_set_bounds(layer, bounds);
    CHECK(result == GR_OK);
    gr_paint_desc desc = {0};
    desc.kind = GR_PAINT_SOLID;
    desc.value.solid = (gr_color){0, 0, 0, 1};
    gr_paint *paint = gr_paint_new(&desc);
    gr_canvas *canvas = gr_canvas_new();
    result = gr_canvas_fill_rect(canvas, bounds, paint);
    CHECK(result == GR_OK);
    gr_display_list *list = gr_canvas_finish(canvas);
    gr_canvas_unref(canvas);
    gr_paint_unref(paint); /* Recording owns an independent reference. */
    gr_display_list_unref(list);

    paint = gr_paint_new(&desc);
    gr_drawer drawer = {paint, draw, destroy_paint};
    result = gr_content_layer_set_drawer(layer, &drawer);
    if (result != GR_OK)
        gr_paint_unref(paint);
    result = gr_layer_add_child(gr_scene_get_root(scene), layer);
    CHECK(result == GR_OK);
    gr_layer_unref(layer); /* Parent keeps it alive. */
    layer = gr_layer_ref(gr_layer_get_child(gr_scene_get_root(scene), 0));

    /* Scheduled work owns a weak link independently of backend lifetime. */
    gr_scene_link *task = gr_scene_link_ref(backend->scene);
    gr_scene *locked = gr_scene_link_lock(task);
    if (locked != NULL)
    {
        result = gr_scene_commit(locked);
        CHECK(result == GR_OK);
        gr_scene_unref(locked);
    }
    gr_scene_unref(scene);
    CHECK(!gr_layer_is_alive(layer));
    locked = gr_scene_link_lock(task);
    CHECK(locked == NULL);
    gr_scene_unref(locked);
    gr_scene_link_unref(task);
    gr_layer_unref(layer);
    (void)result;
}
