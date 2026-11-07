#ifndef GRAPHITE_EXAMPLE_PLATFORM_H
#define GRAPHITE_EXAMPLE_PLATFORM_H

#include "example.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented once per native shell in platform/. */

/** Runs the native window/event loop, optionally testing 60 ticks or reporting FPS. */
int example_platform_run(example *ex, bool selftest, bool fps);
/** Renders at 1x and writes raw premultiplied RGBA8 PNG pixels. */
bool example_platform_snapshot(example *ex, const char *path);
/** Returns an owned native text system, or NULL where the platform has none yet. */
gr_text_system *example_platform_text_system(void);

/* Frame driver shared by the shells (frames.c): poses the scene from the host's frame events. */

typedef struct example_frames example_frames;

struct example_frames
{
    example *content;
    gr_scene_host *host; /**< Interactive host; its handler borrows this struct until the host is released. */
    bool selftest, fps;
    /** Whether a frame that finds the previous frame's changes uncommitted fails the selftest. Shells whose commit
     * point may merge frames delivered back to back (macOS run-loop idle) count them as coalesced instead and
     * check commits at their idle points with example_frames_fail. */
    bool strict;
    /** Called from the frame handler after the 60th selftest frame; stops the native loop. */
    void (*finished)(example_frames *frames);
    void *platform;

    unsigned frame, fps_ticks, uncommitted[60], uncommitted_count, coalesced;
    double start, report;
    bool done, changed_between_frames;
};

/** Installs the event handler, asks for keyboard focus and requests the first frame. The handler poses the scene
 * on frames and passes focus changes to example_focus and input to example_input. Sets content->host; clear it
 * before releasing the host. */
bool example_frames_start(example_frames *frames);
/** Records a selftest failure at the current frame. */
void example_frames_fail(example_frames *frames);
/** Prints the selftest summary with optional platform details and returns the exit status: nonzero for
 * uncommitted frames, a missing viewport report or !platform_ok. */
int example_frames_report(const example_frames *frames, const char *details, bool platform_ok);

#ifdef __cplusplus
}
#endif

#endif
