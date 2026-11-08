#include "internal.h"

#include <emscripten/emscripten.h>
#include <stdlib.h>

struct gr_web_host
{
    int32_t element;     /**< The runtime host handle, owned by the backend. */
    gr_scene_host *host; /**< Borrowed; the core host this data belongs to. */
};

static bool dispatch(gr_web_host *h, gr_event event)
{
    return gr_scene_host_dispatch_event(h->host, &event);
}

/* Exports for runtime.js, which calls them from its event listeners while the host is alive. They return whether
 * the handler consumed the event. */

EMSCRIPTEN_KEEPALIVE void gr_web_on_viewport(gr_web_host *h, double width, double height, double scale)
{
    gr_scene *scene = gr_scene_host_get_scene(h->host);
    if (scene)
    {
        if (gr_scene_get_backing_scale(scene) != scale)
            gr_scene_set_backing_scale(scene, scale);
        gr_scene_unref(scene);
    }
    dispatch(
        h, (gr_event){.type = GR_EVENT_VIEWPORT, .time = gr_web_js_now(), .value.viewport = {{width, height}, scale}});
}

EMSCRIPTEN_KEEPALIVE void gr_web_on_focus(gr_web_host *h, bool focused, double time)
{
    dispatch(h, (gr_event){.type = focused ? GR_EVENT_FOCUS_IN : GR_EVENT_FOCUS_OUT, .time = time});
}

EMSCRIPTEN_KEEPALIVE bool gr_web_on_pointer(gr_web_host *h, int type, double time, uint32_t modifiers, uint32_t id,
                                            int kind, double x, double y, int button, uint32_t buttons, uint32_t clicks,
                                            double pressure)
{
    gr_event e = {.type = (gr_event_type)type, .time = time, .modifiers = modifiers};
    e.value.pointer = (gr_pointer_event){
        .id = id,
        .kind = (gr_pointer_kind)kind,
        .position = {x, y},
        .button = (gr_button)button,
        .buttons = buttons,
        .click_count = clicks,
        .pressure = (float)pressure,
    };
    return dispatch(h, e);
}

EMSCRIPTEN_KEEPALIVE bool gr_web_on_scroll(gr_web_host *h, double time, uint32_t modifiers, double x, double y,
                                           double dx, double dy, int unit)
{
    gr_event e = {.type = GR_EVENT_SCROLL, .time = time, .modifiers = modifiers};
    e.value.scroll = (gr_scroll_event){.position = {x, y}, .delta = {dx, dy}, .unit = (gr_scroll_unit)unit};
    return dispatch(h, e);
}

EMSCRIPTEN_KEEPALIVE bool gr_web_on_key(gr_web_host *h, bool down, double time, uint32_t modifiers, int code, int key,
                                        uint32_t character, bool repeat, bool composing, uint32_t native_code)
{
    gr_event e = {.type = down ? GR_EVENT_KEY_DOWN : GR_EVENT_KEY_UP, .time = time, .modifiers = modifiers};
    e.value.key = (gr_key_event){
        .code = (gr_key_code)code,
        .key = (gr_key)key,
        .character = character,
        .repeat = repeat,
        .composing = composing,
        .native_code = native_code,
    };
    return dispatch(h, e);
}

/* Delivers a requested frame; the backend commits right after it in the same animation frame. */
void gr_web_host_frame(gr_web_host *h, double time, double interval)
{
    if (!gr_scene_host_frame_requested(h->host))
        return;
    dispatch(h, (gr_event){.type = GR_EVENT_FRAME,
                           .time = time,
                           .value.frame = {.target_time = time + interval, .interval = interval}});
}

static void destroy_host(void *data)
{
    gr_web_host *h = data;
    gr_web_js_host_events(h->element, NULL);
    free(h);
}

static void request_frame(void *data)
{
    gr_web_js_request_tick(((gr_web_host *)data)->element);
}

static double now(void *data)
{
    (void)data;
    return gr_web_js_now();
}

static void set_cursor(void *data, gr_cursor cursor)
{
    gr_web_js_set_cursor(((gr_web_host *)data)->element, cursor);
}

/* TODO: text input (IME). */
static void set_text_input(void *data, const gr_text_input *input)
{
    (void)data;
    (void)input;
}

static void request_focus(void *data)
{
    gr_web_js_request_focus(((gr_web_host *)data)->element);
}

static const gr_scene_host_vtable host_vtable = {destroy_host, request_frame,  now,
                                                 set_cursor,   set_text_input, request_focus};

gr_scene_host *gr_web_attach_to_element(gr_scene *scene, const char *target)
{
    if (!scene || !target)
        return NULL;
    gr_web_host *data = calloc(1, sizeof(*data));
    if (!data)
        return NULL;
    data->element = gr_web_js_host_new(target);
    if (!data->element || !gr_web_install(scene, data->element))
    {
        free(data);
        return NULL;
    }
    gr_scene_set_backing_scale(scene, gr_web_js_device_pixel_ratio());
    data->host = gr_scene_host_new(scene, &host_vtable, data);
    if (!data->host)
    {
        free(data);
        return NULL;
    }
    /* Listeners start now; the first viewport report is synchronous, so it is known before the first frame. */
    gr_web_js_host_events(data->element, data);
    return data->host;
}
