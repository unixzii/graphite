#include "conformance.h"

#include <graphite/macos.h>

#import <AppKit/AppKit.h>
#include <stdlib.h>

typedef struct
{
    gr_scene_host *host;
    double scale;
} mac_capture;

static void *attach(void *userdata, gr_scene *scene, double scale)
{
    (void)userdata;
    mac_capture *capture = calloc(1, sizeof(*capture));
    if (!capture)
        abort();
    capture->scale = scale;
    capture->host = gr_macos_offscreen_new(scene, (gr_size){100, 100}, scale);
    if (!capture->host)
    {
        free(capture);
        return NULL;
    }
    return capture;
}

static bool capture(void *userdata, gr_conf_pixels *out)
{
    mac_capture *capture = userdata;
    gr_macos_pixels pixels = {0};
    if (!gr_macos_host_render(capture->host, (gr_size){100, 100}, capture->scale, &pixels))
        return false;
    *out = (gr_conf_pixels){(uint32_t)pixels.width, (uint32_t)pixels.height, pixels.data};
    return true;
}

static void destroy(void *userdata)
{
    mac_capture *capture = userdata;
    gr_scene_host_unref(capture->host);
    free(capture);
}

static gr_text_system *text_system(void *userdata)
{
    (void)userdata;
    return gr_macos_text_system_new();
}

int main(int argc, char **argv)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        gr_conf_harness harness = {"macos", NULL, attach, capture, destroy, text_system};
        return gr_conf_run_main(&harness, 1, argc, argv);
    }
}
