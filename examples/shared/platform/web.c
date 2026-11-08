#include "platform.h"

#include <graphite/web.h>

#include <emscripten/emscripten.h>

/* platform/web.js: titles the page and makes sure a #graphite element exists (the example page has one filling the
 * window; other pages get one of the example's size). */
void example_web_prepare(const char *title, double width, double height);

static void finish_selftest(example_frames *frames)
{
    emscripten_force_exit(example_frames_report(frames, NULL, true));
}

gr_text_system *example_platform_text_system(void)
{
    return gr_web_text_system_new();
}

int example_platform_run(example *content, bool selftest, bool fps)
{
    char title[128];
    snprintf(title, sizeof(title), "graphite %s (Web)", content->desc->name);
    example_web_prepare(title, content->desc->size.width, content->desc->size.height);
    gr_scene_host *host = gr_web_attach_to_element(content->scene, "#graphite");
    if (!host)
    {
        fprintf(stderr, "cannot attach the Web backend\n");
        return 1;
    }
    /* Frames outlive this call: the page runs until it closes or the selftest exits. */
    example_frames *frames = calloc(1, sizeof(*frames));
    if (!frames)
        abort();
    *frames = (example_frames){.content = content,
                               .host = host,
                               .selftest = selftest,
                               .fps = fps,
                               /* Each animation frame delivers the frame event and then commits. */
                               .strict = true,
                               .finished = finish_selftest};
    if (!example_frames_start(frames))
        return 1;
    /* Hand the thread back to the browser, which drives frames and input from now on; main never returns. */
    emscripten_exit_with_live_runtime();
    return 0;
}

bool example_platform_snapshot(example *content, const char *path)
{
    (void)content;
    fprintf(stderr, "cannot write %s: snapshots need offscreen rendering, which the Web backend does not have\n", path);
    return false;
}
