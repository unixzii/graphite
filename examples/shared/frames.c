#include "platform.h"

static void report_fps(example_frames *f, double time)
{
    ++f->fps_ticks;
    double elapsed = time - f->report;
    if (elapsed < 2)
        return;
    printf("fps: %.1f\n", f->fps_ticks / elapsed);
    fflush(stdout);
    f->report = time;
    f->fps_ticks = 0;
}

static bool on_frame(example_frames *f, const gr_event *event)
{
    if (f->done)
        return true;
    if (f->frame == 0)
        f->start = f->report = event->time;
    ++f->frame;
    /* The host commits at its platform commit point, which comes before the next frame. A focus change between
     * frames may land in the same frame-clock cycle ahead of its commit, so skip the check after one. */
    bool focus_changed = f->focus_changed;
    f->focus_changed = false;
    if (f->selftest && f->frame > 1 && !focus_changed && gr_scene_needs_commit(f->content->scene))
    {
        if (f->strict)
            example_frames_fail(f);
        else
            ++f->coalesced;
    }
    /* Advance by frame time, so animation speed is independent of refresh rate. */
    example_update(f->content, event->time - f->start);
    if (f->fps)
        report_fps(f, event->time);
    if (f->selftest && f->frame == 60)
    {
        f->done = true;
        f->finished(f);
        return true;
    }
    gr_scene_host_request_frame(f->host);
    return true;
}

static bool handle(void *userdata, const gr_event *event)
{
    example_frames *f = userdata;
    switch (event->type)
    {
        case GR_EVENT_FRAME:
            return on_frame(f, event);
        case GR_EVENT_FOCUS_IN:
        case GR_EVENT_FOCUS_OUT:
            f->focus_changed = true;
            example_focus(f->content, event->type == GR_EVENT_FOCUS_IN);
            return true;
        default:
            return false;
    }
}

bool example_frames_start(example_frames *f)
{
    gr_event_handler handler = {f, handle, NULL};
    if (gr_scene_host_set_event_handler(f->host, &handler) != GR_OK || gr_scene_host_request_frame(f->host) != GR_OK)
    {
        fprintf(stderr, "the host does not deliver frames\n");
        return false;
    }
    gr_scene_host_request_focus(f->host);
    /* Scenes start focused; catch up if the host has no focus (yet). */
    if (!gr_scene_host_has_focus(f->host))
        example_focus(f->content, false);
    return true;
}

void example_frames_fail(example_frames *f)
{
    if (f->uncommitted_count < sizeof(f->uncommitted) / sizeof(f->uncommitted[0]))
        f->uncommitted[f->uncommitted_count++] = f->frame;
}

int example_frames_report(const example_frames *f, const char *details, bool platform_ok)
{
    gr_viewport_event viewport;
    bool has_viewport = gr_scene_host_get_viewport(f->host, &viewport);
    printf("selftest: frames=%u uncommitted=[", f->frame);
    for (unsigned i = 0; i < f->uncommitted_count; ++i)
        printf("%s%u", i ? ", " : "", f->uncommitted[i]);
    printf("] coalesced=%u viewport=%gx%g@%g focused=%s %sbacking_scale=%g\n", f->coalesced, viewport.size.width,
           viewport.size.height, viewport.scale, gr_scene_host_has_focus(f->host) ? "yes" : "no",
           details ? details : "", gr_scene_get_backing_scale(f->content->scene));
    fflush(stdout);
    return f->uncommitted_count || !has_viewport || !platform_ok ? 1 : 0;
}
