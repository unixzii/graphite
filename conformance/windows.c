#include "conformance.h"

#include <graphite/windows.h>

static void *attach(void *userdata, gr_scene *scene, double scale)
{
    (void)userdata;
    return gr_windows_offscreen_new(scene, scale);
}

static bool capture(void *host, gr_conf_pixels *out)
{
    gr_windows_pixels pixels = {0};
    if (!gr_windows_host_capture(host, &pixels))
        return false;
    *out = (gr_conf_pixels){pixels.width, pixels.height, pixels.data};
    return true;
}

static void destroy(void *host)
{
    gr_scene_host_unref(host);
}

static gr_text_system *text_system(void *userdata)
{
    (void)userdata;
    return gr_windows_text_system_new();
}

int main(int argc, char **argv)
{
    gr_conf_harness harness = {"windows", NULL, attach, capture, destroy, text_system};
    return gr_conf_run_main(&harness, 1, argc, argv);
}
