#include "internal.h"

struct gr_scene_host
{
    size_t refs;
    gr_scene_link *scene;
    const gr_scene_host_vtable *identity;
    gr_scene_host_vtable vtable;
    void *data;
};

gr_scene_host *gr_scene_host_new(gr_scene *scene, const gr_scene_host_vtable *vtable, void *data)
{
    if (!scene || !vtable || !vtable->destroy)
        return NULL;
    gr_scene_host *host = gr_alloc(1, sizeof(*host));
    host->refs = 1;
    host->scene = gr_scene_get_link(scene);
    host->identity = vtable;
    host->vtable = *vtable;
    host->data = data;
    return host;
}

gr_scene_host *gr_scene_host_ref(gr_scene_host *host)
{
    if (host)
        gr_local_ref(&host->refs);
    return host;
}

void gr_scene_host_unref(gr_scene_host *host)
{
    if (!host || !gr_local_unref(&host->refs))
        return;
    host->vtable.destroy(host->data);
    gr_scene_link_unref(host->scene);
    free(host);
}

gr_scene *gr_scene_host_get_scene(const gr_scene_host *host)
{
    return gr_scene_link_lock(host->scene);
}

const gr_scene_host_vtable *gr_scene_host_get_vtable(const gr_scene_host *host)
{
    return host->identity;
}

void *gr_scene_host_get_data(const gr_scene_host *host)
{
    return host->data;
}
