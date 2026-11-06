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

static gr_gtk_host *install(gr_scene *scene, bool widget)
{
    if (!scene || !gtk_is_initialized())
        return NULL;
    gr_gtk_host *host = g_new0(gr_gtk_host, 1);
    host->refs = 1;
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
        gr_gtk_host_unref(host);
        return NULL;
    }
    return host;
}

gr_gtk_host *gr_gtk_attach(gr_scene *scene)
{
    return install(scene, true);
}

gr_gtk_host *gr_gtk_attach_offscreen(gr_scene *scene)
{
    return install(scene, false);
}

gr_gtk_host *gr_gtk_host_ref(gr_gtk_host *host)
{
    if (host)
    {
        if (host->refs == SIZE_MAX)
            abort();
        ++host->refs;
    }
    return host;
}

void gr_gtk_host_unref(gr_gtk_host *host)
{
    if (!host || --host->refs)
        return;
    g_clear_object(&host->widget);
    gr_gtk_shared_unref(host->shared);
    g_free(host);
}

GtkWidget *gr_gtk_host_get_widget(const gr_gtk_host *host)
{
    return host->widget;
}

GskRenderNode *gr_gtk_host_get_root_node(gr_gtk_host *host)
{
    return gr_gtk_tree_root(host->shared);
}

bool gr_gtk_host_get_size(const gr_gtk_host *host, gr_size *out)
{
    return gr_gtk_tree_size(host->shared, out);
}
