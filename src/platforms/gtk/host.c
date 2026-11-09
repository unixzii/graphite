#include "internal.h"

static void request_commit(void *userdata)
{
    gr_gtk_shared *s = userdata;
    s->pending = true;
    GtkWidget *widget = g_weak_ref_get(&s->widget);
    if (widget)
    {
        gr_gtk_widget_request(widget);
        g_object_unref(widget);
    }
}

static void apply(void *userdata, const gr_commit_batch *batch)
{
    gr_gtk_shared *s = userdata;
    gr_size old, next;
    bool had = gr_gtk_tree_size(s, &old);
    gr_gtk_tree_apply(s, batch);
    bool has = gr_gtk_tree_size(s, &next);
    GtkWidget *widget = g_weak_ref_get(&s->widget);
    if (widget)
    {
        if (had != has || old.width != next.width || old.height != next.height)
            gtk_widget_queue_resize(widget);
        if (batch->realized.count || batch->children.count || batch->updates.count || batch->released.count)
            gtk_widget_queue_draw(widget);
        g_object_unref(widget);
    }
}

static void destroy(void *userdata)
{
    gr_gtk_shared *s = userdata;
    gr_gtk_tree_clear(s);
    GtkWidget *widget = g_weak_ref_get(&s->widget);
    if (widget)
    {
        gtk_widget_queue_resize(widget);
        gtk_widget_queue_draw(widget);
        g_object_unref(widget);
    }
    gr_gtk_shared_unref(s);
}

static void destroy_host(void *data)
{
    gr_gtk_host *host = data;
    if (host->widget)
        gr_gtk_widget_set_host(host->widget, NULL);
    g_clear_object(&host->widget);
    gr_gtk_shared_unref(host->shared);
    g_free(host);
}

static const gr_scene_host_vtable host_vtable = {.destroy = destroy_host};

/* Widget hosts are interactive. */

static void request_frame(void *data)
{
    gr_gtk_widget_request_frame(((gr_gtk_host *)data)->widget);
}

static double now(void *data)
{
    (void)data;
    return (double)g_get_monotonic_time() / G_TIME_SPAN_SECOND;
}

static void set_cursor(void *data, gr_cursor cursor)
{
    gr_gtk_widget_set_cursor(((gr_gtk_host *)data)->widget, cursor);
}

static void set_text_input(void *data, const gr_text_input *input)
{
    gr_gtk_widget_set_text_input(((gr_gtk_host *)data)->widget, input);
}

static void request_focus(void *data)
{
    gtk_widget_grab_focus(((gr_gtk_host *)data)->widget);
}

static const gr_scene_host_vtable widget_host_vtable = {
    destroy_host, request_frame, now, set_cursor, set_text_input, request_focus,
};

gr_gtk_host *gr_gtk_host_data(const gr_scene_host *host)
{
    if (!host ||
        (gr_scene_host_get_vtable(host) != &host_vtable && gr_scene_host_get_vtable(host) != &widget_host_vtable))
        return NULL;
    return gr_scene_host_get_data(host);
}

static gr_scene_host *install(gr_scene *scene, bool widget)
{
    if (!scene || !gtk_is_initialized())
        return NULL;
    gr_gtk_host *host = g_new0(gr_gtk_host, 1);
    host->shared = gr_gtk_shared_new(scene);
    if (widget)
    {
        host->widget = g_object_ref_sink(gr_gtk_widget_new(host->shared));
        g_weak_ref_set(&host->shared->widget, host->widget);
    }
    gr_backend_vtable vtable = {request_commit, apply, destroy};
    gr_gtk_shared *backend = gr_gtk_shared_ref(host->shared);
    if (gr_scene_set_backend(scene, &vtable, backend) != GR_OK)
    {
        gr_gtk_shared_unref(backend);
        destroy_host(host);
        return NULL;
    }
    gr_scene_host *result = gr_scene_host_new(scene, widget ? &widget_host_vtable : &host_vtable, host);
    if (widget)
        gr_gtk_widget_set_host(host->widget, result);
    return result;
}

gr_scene_host *gr_gtk_attach(gr_scene *scene)
{
    return install(scene, true);
}

gr_scene_host *gr_gtk_attach_offscreen(gr_scene *scene)
{
    return install(scene, false);
}

GtkWidget *gr_gtk_host_get_widget(const gr_scene_host *host)
{
    gr_gtk_host *data = gr_gtk_host_data(host);
    return data ? data->widget : NULL;
}

GskRenderNode *gr_gtk_host_get_root_node(const gr_scene_host *host)
{
    gr_gtk_host *data = gr_gtk_host_data(host);
    return data ? gr_gtk_tree_root(data->shared) : NULL;
}

bool gr_gtk_host_get_size(const gr_scene_host *host, gr_size *out)
{
    gr_gtk_host *data = gr_gtk_host_data(host);
    if (!data)
    {
        *out = (gr_size){0};
        return false;
    }
    return gr_gtk_tree_size(data->shared, out);
}
