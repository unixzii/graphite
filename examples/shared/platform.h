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

#ifdef __cplusplus
}
#endif

#endif
