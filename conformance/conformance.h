#ifndef GRAPHITE_CONFORMANCE_H
#define GRAPHITE_CONFORMANCE_H

#include <graphite/graphite.h>

/** Owned premultiplied RGBA8 pixels, top row first; release with free. */
typedef struct
{
    uint32_t width, height;
    uint8_t *data;
} gr_conf_pixels;

/** Features a backend may not support yet; cases that need one it lacks are reported as skipped. */
enum
{
    GR_CONF_MASKS = 1u,  /**< gr_layer_set_mask */
    GR_CONF_THREADS = 2u /**< Threads, for text systems used from several at once */
};

/** Backend adapter. Attach once per Scene; capture reads committed state.
 * The context keeps Scene alive until after destroy. Name is borrowed for the run.
 * text_system is optional and returns an owned text system whose layouts the backend draws, or NULL; text cases
 * are skipped without one. unsupported lists GR_CONF_* features whose cases are skipped. */
typedef struct
{
    const char *name;
    void *userdata;
    void *(*attach)(void *userdata, gr_scene *scene, double scale);
    bool (*capture)(void *host, gr_conf_pixels *pixels);
    void (*destroy)(void *host);
    gr_text_system *(*text_system)(void *userdata);
    uint32_t unsupported;
} gr_conf_harness;

/** Runs all cases at 1, 1.5 and 2x. Positional arguments filter full case names
 * by substring, flags are ignored. Returns process status.
 * GRAPHITE_CONFORMANCE_OUT overrides the compile-time output directory. */
int gr_conf_run_main(const gr_conf_harness *harnesses, size_t count, int argc, char **argv);

#endif
