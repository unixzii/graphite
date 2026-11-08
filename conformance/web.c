#include "conformance.h"

#include <graphite/web.h>

#include <emscripten/eventloop.h>
#include <stdio.h>
#include <stdlib.h>

/* conformance/web.js. The async ones suspend through JSPI while the page runner works. */
int web_conf_set_scale(double scale);
int web_conf_container(void);
void web_conf_remove(int container);
int web_conf_capture(int container, gr_conf_pixels *out);
void web_conf_export(const char *directory);

/* Captures are written to the in-memory file system, then copied out by the runner. */
#define WEB_CONF_OUT "/conformance"

typedef struct
{
    gr_scene_host *host;
    int container;
} web_capture;

static void *attach(void *userdata, gr_scene *scene, double scale)
{
    (void)userdata;
    if (!web_conf_set_scale(scale))
        return NULL;
    web_capture *capture = calloc(1, sizeof(*capture));
    if (!capture)
        abort();
    capture->container = web_conf_container();
    char target[32];
    snprintf(target, sizeof(target), "!conf%d", capture->container);
    capture->host = gr_web_attach_to_element(scene, target);
    if (!capture->host || gr_scene_get_backing_scale(scene) != scale)
    {
        gr_scene_host_unref(capture->host);
        web_conf_remove(capture->container);
        free(capture);
        return NULL;
    }
    return capture;
}

static bool capture(void *userdata, gr_conf_pixels *out)
{
    web_capture *capture = userdata;
    return web_conf_capture(capture->container, out);
}

static void destroy(void *userdata)
{
    web_capture *capture = userdata;
    gr_scene_host_unref(capture->host);
    web_conf_remove(capture->container);
    free(capture);
}

static gr_text_system *text_system(void *userdata)
{
    (void)userdata;
    return gr_web_text_system_new();
}

int main(int argc, char **argv)
{
    /* main suspends while captures wait; keep the runtime alive through the animation frames that run meanwhile. */
    emscripten_runtime_keepalive_push();
    setenv("GRAPHITE_CONFORMANCE_OUT", WEB_CONF_OUT, 0);
    /* Masks are a known gap (docs/backends/web.md). The build is single-threaded, and the Web text system works on
     * the main thread only. */
    gr_conf_harness harness = {"web", NULL, attach, capture, destroy, text_system, GR_CONF_MASKS | GR_CONF_THREADS};
    int status = gr_conf_run_main(&harness, 1, argc, argv);
    web_conf_export(getenv("GRAPHITE_CONFORMANCE_OUT"));
    emscripten_runtime_keepalive_pop();
    return status;
}
