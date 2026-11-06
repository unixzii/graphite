#include "conformance.h"
#include <graphite/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    gr_gtk_host *host;
    gr_gtk_renderer *renderer;
    double scale;
} gtk_capture;

static void *attach(void *userdata, gr_scene *scene, double scale)
{
    gtk_capture *capture = g_new0(gtk_capture, 1);
    capture->host = gr_gtk_attach_offscreen(scene);
    if (!capture->host)
    {
        g_free(capture);
        return NULL;
    }
    capture->renderer = gr_gtk_renderer_ref(userdata);
    capture->scale = scale;
    if (gr_scene_set_backing_scale(scene, scale) != GR_OK)
        abort();
    return capture;
}

static bool capture(void *userdata, gr_conf_pixels *out)
{
    gtk_capture *c = userdata;
    gr_gtk_pixels pixels = {0};
    if (!gr_gtk_renderer_render(c->renderer, c->host, c->scale, &pixels))
        return false;
    *out = (gr_conf_pixels){pixels.width, pixels.height, pixels.data};
    return true;
}

static void destroy(void *userdata)
{
    gtk_capture *c = userdata;
    gr_gtk_host_unref(c->host);
    gr_gtk_renderer_unref(c->renderer);
    g_free(c);
}

int main(int argc, char **argv)
{
    if (!gtk_init_check())
    {
        fprintf(stderr, "GTK could not be initialized: run inside a graphical session\n");
        return 1;
    }
    const char *selection = g_getenv("GRAPHITE_GSK_RENDERERS");
    if (!selection)
        selection = "gl,vulkan";
    char **names = g_strsplit(selection, ",", -1);
    size_t count = g_strv_length(names);
    if (count == 0)
    {
        fprintf(stderr, "no GSK renderer selected\n");
        g_strfreev(names);
        return 2;
    }
    gr_conf_harness *harnesses = g_new0(gr_conf_harness, count);
    int status = 0;
    size_t ready = 0;
    for (size_t i = 0; i < count; ++i)
    {
        char *name = g_strstrip(names[i]);
        gr_gtk_renderer_kind kind;
        if (strcmp(name, "gl") == 0)
            kind = GR_GTK_GL;
        else if (strcmp(name, "vulkan") == 0)
            kind = GR_GTK_VULKAN;
        else if (strcmp(name, "cairo") == 0)
            kind = GR_GTK_CAIRO;
        else
        {
            fprintf(stderr, "unknown GSK renderer '%s'\n", name);
            status = 2;
            break;
        }
        GError *error = NULL;
        gr_gtk_renderer *renderer = gr_gtk_renderer_new(kind, &error);
        if (!renderer)
        {
            fprintf(stderr, "cannot realize %s: %s\n", name, error ? error->message : "no display");
            g_clear_error(&error);
            status = 1;
            break;
        }
        harnesses[i] = (gr_conf_harness){.name = g_strdup_printf("gtk-%s", name),
                                         .userdata = renderer,
                                         .attach = attach,
                                         .capture = capture,
                                         .destroy = destroy};
        ++ready;
    }
    if (!status)
        status = gr_conf_run_main(harnesses, count, argc, argv);
    for (size_t i = 0; i < ready; ++i)
    {
        gr_gtk_renderer_unref(harnesses[i].userdata);
        g_free((char *)harnesses[i].name);
    }
    g_free(harnesses);
    g_strfreev(names);
    return status;
}
