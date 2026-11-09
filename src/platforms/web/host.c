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

/* Text input edits arrive in UTF-8 offsets, already applied to the runtime's copy of the state. */
EMSCRIPTEN_KEEPALIVE bool gr_web_on_text_edit(gr_web_host *h, double time, size_t start, size_t end, const char *text,
                                              size_t length, size_t selection_start, size_t selection_end,
                                              size_t composition_start, size_t composition_end)
{
    gr_event e = {.type = GR_EVENT_TEXT_EDIT, .time = time};
    e.value.edit = (gr_text_edit_event){
        .replace = {start, end},
        .text = {text, length},
        .selection = {selection_start, selection_end},
        .composition = {composition_start, composition_end},
    };
    return dispatch(h, e);
}

EMSCRIPTEN_KEEPALIVE bool gr_web_on_text_command(gr_web_host *h, double time, uint32_t modifiers, int command,
                                                 bool extend)
{
    gr_event e = {.type = GR_EVENT_TEXT_COMMAND, .time = time, .modifiers = modifiers};
    e.value.command = (gr_text_command_event){(gr_text_command)command, extend};
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

/* The runtime keeps the state and places its hidden field at the caret: the composition's start while there is one,
 * else the selection's end, from the shared layout or else the shared caret. */
static void set_text_input(void *data, const gr_text_input *input)
{
    gr_web_host *h = data;
    if (!input)
    {
        gr_web_js_set_text_input(h->element, false, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        return;
    }
    gr_text_range s = input->selection, c = input->composition;
    gr_rect caret = input->caret;
    if (input->layout)
    {
        size_t at = c.start < c.end ? c.start : s.end;
        gr_text_layout_get_caret_rect(input->layout, (gr_text_position){at, GR_TEXT_AFFINITY_DOWNSTREAM}, &caret);
        caret.origin.x += input->origin.x;
        caret.origin.y += input->origin.y;
    }
    gr_web_js_set_text_input(h->element, true, input->kind, input->text.data, input->text.count, s.start, s.end,
                             c.start, c.end, caret.origin.x, caret.origin.y, caret.size.width, caret.size.height);
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
