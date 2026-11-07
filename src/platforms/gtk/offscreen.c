#include "internal.h"

struct gr_gtk_renderer
{
    size_t refs;
    GskRenderer *renderer;
};

gr_gtk_renderer *gr_gtk_renderer_new(gr_gtk_renderer_kind kind, GError **error)
{
    if (!gtk_is_initialized() || !gdk_display_get_default())
        return NULL;
    GskRenderer *native = NULL;
    switch (kind)
    {
        case GR_GTK_GL:
            native = gsk_gl_renderer_new();
            break;
        case GR_GTK_VULKAN:
            native = gsk_vulkan_renderer_new();
            break;
        case GR_GTK_CAIRO:
            native = gsk_cairo_renderer_new();
            break;
        default:
            return NULL;
    }
    if (!gsk_renderer_realize_for_display(native, gdk_display_get_default(), error))
    {
        g_object_unref(native);
        return NULL;
    }
    gr_gtk_renderer *r = g_new0(gr_gtk_renderer, 1);
    r->refs = 1;
    r->renderer = native;
    return r;
}

gr_gtk_renderer *gr_gtk_renderer_ref(gr_gtk_renderer *r)
{
    if (r)
    {
        if (r->refs == SIZE_MAX)
            abort();
        ++r->refs;
    }
    return r;
}

void gr_gtk_renderer_unref(gr_gtk_renderer *r)
{
    if (!r || --r->refs)
        return;
    gsk_renderer_unrealize(r->renderer);
    g_object_unref(r->renderer);
    g_free(r);
}

bool gr_gtk_renderer_render(gr_gtk_renderer *renderer, const gr_scene_host *host, double scale, gr_gtk_pixels *out)
{
    if (!out)
        return false;
    *out = (gr_gtk_pixels){0};
    gr_size size;
    if (!renderer || !host || !isfinite(scale) || scale <= 0 || !gr_gtk_host_get_size(host, &size))
        return false;
    double w = round(size.width * scale), h = round(size.height * scale);
    if (!isfinite(w) || !isfinite(h) || w < 1 || h < 1 || w > G_MAXINT || h > G_MAXINT)
        return false;
    uint32_t width = (uint32_t)w, height = (uint32_t)h;
    size_t row = (size_t)width * 4;
    if (row / 4 != width || height > SIZE_MAX / row)
        return false;
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gtk_snapshot_scale(snapshot, (float)scale, (float)scale);
    GskRenderNode *root = gr_gtk_host_get_root_node(host);
    if (root)
    {
        gtk_snapshot_append_node(snapshot, root);
        gsk_render_node_unref(root);
    }
    graphene_rect_t viewport;
    graphene_rect_init(&viewport, 0, 0, (float)width, (float)height);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    if (!node)
    {
        GdkRGBA clear = {0, 0, 0, 0};
        node = gsk_color_node_new(&clear, &viewport);
    }
    GdkTexture *texture = gsk_renderer_render_texture(renderer->renderer, node, &viewport);
    gsk_render_node_unref(node);
    if (!texture)
        return false;
    GdkTextureDownloader *downloader = gdk_texture_downloader_new(texture);
    gdk_texture_downloader_set_format(downloader, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED);
    gsize stride;
    GBytes *bytes = gdk_texture_downloader_download_bytes(downloader, &stride);
    gsize length;
    const uint8_t *data = g_bytes_get_data(bytes, &length);
    bool valid = stride >= row && stride <= SIZE_MAX / height && length >= stride * (height - 1) + row;
    if (valid)
    {
        out->data = g_malloc(row * height);
        out->width = width;
        out->height = height;
        for (size_t y = 0; y < height; ++y)
            memcpy(out->data + y * row, data + y * stride, row);
    }
    g_bytes_unref(bytes);
    gdk_texture_downloader_free(downloader);
    g_object_unref(texture);
    return valid;
}

void gr_gtk_pixels_clear(gr_gtk_pixels *pixels)
{
    if (pixels)
    {
        g_free(pixels->data);
        *pixels = (gr_gtk_pixels){0};
    }
}
