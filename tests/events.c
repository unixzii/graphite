#include "test.h"

#include <graphite/backend.h>

#include <math.h>
#include <string.h>

/* A recording interactive host and handler. */

typedef struct
{
    int frame_requests, focus_requests, cursor_sets, text_input_sets;
    gr_cursor cursor;
    const gr_text_input *text_input;
    char log[64];
} platform;

static void note(char *log, char c)
{
    size_t n = strlen(log);
    CHECK(n + 1 < 64);
    log[n] = c;
    log[n + 1] = 0;
}

static void platform_destroy(void *data)
{
    note(((platform *)data)->log, 'P');
}

static void platform_request_frame(void *data)
{
    ++((platform *)data)->frame_requests;
}

static double platform_now(void *data)
{
    (void)data;
    return 42.5;
}

static void platform_set_cursor(void *data, gr_cursor cursor)
{
    platform *p = data;
    ++p->cursor_sets;
    p->cursor = cursor;
}

static void platform_set_text_input(void *data, const gr_text_input *input)
{
    platform *p = data;
    ++p->text_input_sets;
    p->text_input = input;
}

static void platform_request_focus(void *data)
{
    ++((platform *)data)->focus_requests;
}

static const gr_scene_host_vtable interactive = {platform_destroy,    platform_request_frame,  platform_now,
                                                 platform_set_cursor, platform_set_text_input, platform_request_focus};
static const gr_scene_host_vtable passive = {.destroy = platform_destroy};

typedef struct recorder recorder;

struct recorder
{
    char name; /* Logged on delivery (lowercase) and destruction (uppercase). */
    char *log; /* Shared with the platform to check ordering. */
    gr_scene_host *host;
    bool result;
    gr_event_type types[16];
    char texts[16][16];
    size_t count;
    void (*react)(recorder *r, const gr_event *event); /* Optional. */
    int destroyed;
};

static bool record(void *userdata, const gr_event *event)
{
    recorder *r = userdata;
    CHECK(r->count < 16);
    r->types[r->count] = event->type;
    if (event->type == GR_EVENT_TEXT_EDIT)
    {
        gr_string text = event->value.edit.text;
        CHECK(text.count < 16);
        if (text.count)
            memcpy(r->texts[r->count], text.data, text.count);
        r->texts[r->count][text.count] = 0;
    }
    ++r->count;
    note(r->log, r->name);
    if (r->react)
        r->react(r, event);
    return r->result;
}

static void record_destroy(void *userdata)
{
    recorder *r = userdata;
    ++r->destroyed;
    note(r->log, (char)(r->name - 'a' + 'A'));
}

static gr_event_handler handler(recorder *r)
{
    return (gr_event_handler){r, record, record_destroy};
}

static gr_event press(void)
{
    return (gr_event){.type = GR_EVENT_KEY_DOWN,
                      .time = 1,
                      .value.key = {.code = GR_KEY_CODE_A, .key = GR_KEY_CHARACTER, .character = 0x61}};
}

static gr_event frame(void)
{
    return (gr_event){.type = GR_EVENT_FRAME, .time = 2, .value.frame = {.target_time = 2.016, .interval = 0.016}};
}

/* An edit inserting text at the start of any state, leaving the caret after it. */
static gr_event insert_text(const char *text)
{
    size_t n = strlen(text);
    gr_event e = {.type = GR_EVENT_TEXT_EDIT, .time = 3};
    e.value.edit = (gr_text_edit_event){{0, 0}, {text, n}, {n, n}, {0, 0}};
    return e;
}

/* Cases */

static void passive_hosts(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host_vtable partial = interactive;
    partial.request_focus = NULL;
    CHECK(!gr_scene_host_new(scene, &partial, &p));
    partial = passive;
    partial.now = platform_now;
    CHECK(!gr_scene_host_new(scene, &partial, &p));

    gr_scene_host *host = gr_scene_host_new(scene, &passive, &p);
    CHECK(host && !gr_scene_host_is_interactive(host));
    recorder r = {.name = 'a', .log = p.log, .result = true};
    gr_event_handler h = handler(&r);
    CHECK(gr_scene_host_set_event_handler(host, &h) == GR_ERROR_UNSUPPORTED);
    CHECK(gr_scene_host_set_event_handler(host, NULL) == GR_ERROR_UNSUPPORTED);
    CHECK(gr_scene_host_request_frame(host) == GR_ERROR_UNSUPPORTED && !gr_scene_host_frame_requested(host));
    CHECK(isnan(gr_scene_host_get_time(host)));
    CHECK(gr_scene_host_set_cursor(host, GR_CURSOR_TEXT) == GR_ERROR_UNSUPPORTED);
    CHECK(gr_scene_host_request_focus(host) == GR_ERROR_UNSUPPORTED);
    CHECK(gr_scene_host_set_text_input(host, NULL) == GR_ERROR_UNSUPPORTED);
    gr_event e = press();
    CHECK(!gr_scene_host_dispatch_event(host, &e));
    gr_scene_host_unref(host);
    CHECK(r.destroyed == 0 && strcmp(p.log, "P") == 0);
    gr_scene_unref(scene);
}

static void host_commands(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host *host = gr_scene_host_new(scene, &interactive, &p);
    CHECK(host && gr_scene_host_is_interactive(host));
    CHECK(gr_scene_host_get_time(host) == 42.5);

    OK(gr_scene_host_set_cursor(host, GR_CURSOR_GRABBING));
    CHECK(gr_scene_host_set_cursor(host, (gr_cursor)(GR_CURSOR_NONE + 1)) == GR_ERROR_ARGUMENT);
    CHECK(p.cursor_sets == 1 && p.cursor == GR_CURSOR_GRABBING);

    OK(gr_scene_host_request_focus(host));
    CHECK(p.focus_requests == 1);

    gr_text_input input = {.kind = GR_TEXT_INPUT_TEXT, .text = {"hi", 2}, .selection = {2, 2}};
    OK(gr_scene_host_set_text_input(host, &input)); /* Passed through as is. */
    CHECK(p.text_input == &input && p.text_input_sets == 1);
    OK(gr_scene_host_set_text_input(host, NULL));
    CHECK(!p.text_input && p.text_input_sets == 2);

    gr_scene_host_unref(host);
    gr_scene_unref(scene);
}

static void handler_ownership(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host *host = gr_scene_host_new(scene, &interactive, &p);
    gr_event e = press();
    CHECK(!gr_scene_host_dispatch_event(host, &e)); /* No handler. */

    recorder a = {.name = 'a', .log = p.log, .result = true}, b = {.name = 'b', .log = p.log};
    gr_event_handler h = handler(&a);
    h.handle = NULL;
    CHECK(gr_scene_host_set_event_handler(host, &h) == GR_ERROR_ARGUMENT);
    CHECK(a.destroyed == 0);
    h = handler(&a);
    OK(gr_scene_host_set_event_handler(host, &h));
    CHECK(gr_scene_host_dispatch_event(host, &e));
    h = handler(&b);
    OK(gr_scene_host_set_event_handler(host, &h));
    CHECK(a.destroyed == 1);
    CHECK(!gr_scene_host_dispatch_event(host, &e)); /* b reports unhandled. */
    OK(gr_scene_host_set_event_handler(host, NULL));
    CHECK(b.destroyed == 1);
    CHECK(!gr_scene_host_dispatch_event(host, &e));
    OK(gr_scene_host_set_event_handler(host, NULL));

    /* A handler without destroy is fine; final release destroys the handler before the platform host. */
    gr_event_handler plain = {&a, record, NULL};
    OK(gr_scene_host_set_event_handler(host, &plain));
    h = handler(&b);
    OK(gr_scene_host_set_event_handler(host, &h));
    CHECK(a.destroyed == 1);
    gr_scene_host_unref(host);
    CHECK(strcmp(p.log, "aAbBBP") == 0);
    gr_scene_unref(scene);
}

static void request_next_frame(recorder *r, const gr_event *event)
{
    if (event->type == GR_EVENT_FRAME)
    {
        CHECK(!gr_scene_host_frame_requested(r->host));
        CHECK(event->value.frame.interval == 0.016);
        OK(gr_scene_host_request_frame(r->host));
    }
}

static void frame_requests(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host *host = gr_scene_host_new(scene, &interactive, &p);
    recorder r = {.name = 'a', .log = p.log, .host = host, .result = true};
    gr_event_handler h = handler(&r);
    OK(gr_scene_host_set_event_handler(host, &h));

    gr_event f = frame();
    CHECK(!gr_scene_host_dispatch_event(host, &f)); /* Not requested: dropped. */
    CHECK(r.count == 0);

    OK(gr_scene_host_request_frame(host));
    OK(gr_scene_host_request_frame(host));
    CHECK(p.frame_requests == 1 && gr_scene_host_frame_requested(host));
    CHECK(gr_scene_host_dispatch_event(host, &f));
    CHECK(r.count == 1 && !gr_scene_host_frame_requested(host));
    CHECK(!gr_scene_host_dispatch_event(host, &f));
    CHECK(r.count == 1);

    /* Requesting from the FRAME handler schedules exactly one more. */
    r.react = request_next_frame;
    OK(gr_scene_host_request_frame(host));
    CHECK(p.frame_requests == 2);
    CHECK(gr_scene_host_dispatch_event(host, &f));
    CHECK(p.frame_requests == 3 && gr_scene_host_frame_requested(host));
    CHECK(gr_scene_host_dispatch_event(host, &f));
    CHECK(p.frame_requests == 4 && r.count == 3);

    /* Without a handler a requested frame is consumed. */
    OK(gr_scene_host_set_event_handler(host, NULL));
    CHECK(!gr_scene_host_dispatch_event(host, &f));
    CHECK(!gr_scene_host_frame_requested(host));
    gr_scene_host_unref(host);
    gr_scene_unref(scene);
}

static void dispatch_nested(recorder *r, const gr_event *event)
{
    if (event->type != GR_EVENT_KEY_DOWN)
        return;
    char text[] = "ab";
    gr_event nested = insert_text(text);
    CHECK(!gr_scene_host_dispatch_event(r->host, &nested));
    memcpy(text, "xy", 2); /* The queued copy must not alias caller storage. */
    gr_event out = {.type = GR_EVENT_KEY_UP, .time = 4};
    CHECK(!gr_scene_host_dispatch_event(r->host, &out));
    CHECK(r->count == 1); /* Nothing delivered yet. */
}

static void dispatch_from_queue(recorder *r, const gr_event *event)
{
    dispatch_nested(r, event);
    if (event->type == GR_EVENT_TEXT_EDIT && event->value.edit.text.data[0] == 'a')
    {
        gr_event more = insert_text("c");
        CHECK(!gr_scene_host_dispatch_event(r->host, &more));
    }
}

static void reentrant_dispatch(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host *host = gr_scene_host_new(scene, &interactive, &p);
    recorder r = {.name = 'a', .log = p.log, .host = host, .result = true, .react = dispatch_nested};
    gr_event_handler h = handler(&r);
    OK(gr_scene_host_set_event_handler(host, &h));
    gr_event e = press();
    CHECK(gr_scene_host_dispatch_event(host, &e));
    CHECK(r.count == 3 && r.types[1] == GR_EVENT_TEXT_EDIT && r.types[2] == GR_EVENT_KEY_UP);
    CHECK(strcmp(r.texts[1], "ab") == 0);

    /* Events queued while draining the queue are delivered too, in order. */
    r.count = 0;
    r.react = dispatch_from_queue;
    CHECK(gr_scene_host_dispatch_event(host, &e));
    CHECK(r.count == 4 && r.types[3] == GR_EVENT_TEXT_EDIT && strcmp(r.texts[3], "c") == 0);
    gr_scene_host_unref(host);
    gr_scene_unref(scene);
}

static recorder *replacement;

static void replace_self(recorder *r, const gr_event *event)
{
    (void)event;
    gr_event out = {.type = GR_EVENT_KEY_UP, .time = 5};
    CHECK(!gr_scene_host_dispatch_event(r->host, &out));
    gr_event_handler h = handler(replacement);
    OK(gr_scene_host_set_event_handler(r->host, &h));
    CHECK(r->destroyed == 0); /* Deferred until delivery ends. */
    r->react = NULL;
}

static void replacement_during_delivery(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host *host = gr_scene_host_new(scene, &interactive, &p);
    recorder a = {.name = 'a', .log = p.log, .host = host, .result = true, .react = replace_self};
    recorder b = {.name = 'b', .log = p.log, .host = host};
    replacement = &b;
    gr_event_handler h = handler(&a);
    OK(gr_scene_host_set_event_handler(host, &h));
    gr_event e = press();
    CHECK(gr_scene_host_dispatch_event(host, &e)); /* a's result, not b's. */
    CHECK(strcmp(p.log, "abA") == 0);              /* The queued event reaches b before a is destroyed. */
    CHECK(a.destroyed == 1 && b.destroyed == 0);
    gr_scene_host_unref(host);
    CHECK(strcmp(p.log, "abABP") == 0);
    gr_scene_unref(scene);
}

static void release_host(recorder *r, const gr_event *event)
{
    (void)event;
    gr_scene_host_unref(r->host);
    CHECK(!strchr(r->log, 'P')); /* Still alive until delivery ends. */
    OK(gr_scene_host_request_focus(r->host));
}

static void release_during_delivery(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host *host = gr_scene_host_new(scene, &interactive, &p);
    recorder r = {.name = 'a', .log = p.log, .host = host, .result = true, .react = release_host};
    gr_event_handler h = handler(&r);
    OK(gr_scene_host_set_event_handler(host, &h));
    gr_event e = press();
    CHECK(gr_scene_host_dispatch_event(host, &e));
    CHECK(strcmp(p.log, "aAP") == 0 && p.focus_requests == 1);
    gr_scene_unref(scene);
}

static void event_validation(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host *host = gr_scene_host_new(scene, &interactive, &p);
    recorder r = {.name = 'a', .log = p.log, .result = true};
    gr_event_handler h = handler(&r);
    OK(gr_scene_host_set_event_handler(host, &h));
    CHECK(!gr_scene_host_dispatch_event(host, NULL));

    gr_event down = {.type = GR_EVENT_POINTER_DOWN, .time = 1, .modifiers = GR_MODIFIER_SHIFT | GR_MODIFIER_SUPER};
    down.value.pointer = (gr_pointer_event){.kind = GR_POINTER_PEN,
                                            .position = {3, 4},
                                            .button = GR_BUTTON_PRIMARY,
                                            .buttons = 1u << GR_BUTTON_PRIMARY,
                                            .click_count = 2,
                                            .pressure = 0.25f};
    gr_event scroll = {.type = GR_EVENT_SCROLL, .time = 1};
    scroll.value.scroll = (gr_scroll_event){{1, 1}, {0, -3}, GR_SCROLL_UNIT_LINE, GR_SCROLL_PHASE_NONE, false};
    gr_event key = {.type = GR_EVENT_KEY_DOWN, .time = 1};
    key.value.key = (gr_key_event){GR_KEY_CODE_C, GR_KEY_CHARACTER, 'c', false, false, 8};
    gr_event compose = {.type = GR_EVENT_TEXT_EDIT, .time = 1};
    compose.value.edit = (gr_text_edit_event){{0, 0}, {"ni", 2}, {2, 2}, {0, 2}};
    gr_event command = {.type = GR_EVENT_TEXT_COMMAND, .time = 1};
    command.value.command = (gr_text_command_event){GR_TEXT_COMMAND_MOVE_LEFT, true};
    gr_event viewport = {.type = GR_EVENT_VIEWPORT, .time = 1, .value.viewport = {{800, 600}, 2}};
    const gr_event valid[] = {down, scroll, key, compose, command, viewport};
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
        CHECK(gr_scene_host_dispatch_event(host, &valid[i]));
    CHECK(r.count == 6);

#ifndef NDEBUG /* Release builds trust backends to send valid events. */
    gr_event bad[17];
    size_t n = 0;
    bad[n] = press(), bad[n++].type = (gr_event_type)(GR_EVENT_TEXT_COMMAND + 1);
    bad[n] = press(), bad[n++].time = NAN;
    bad[n] = press(), bad[n++].modifiers = 32;
    bad[n] = down, bad[n++].value.pointer.button = GR_BUTTON_NONE;
    bad[n] = down, bad[n++].value.pointer.pressure = 1.5f;
    bad[n] = down, bad[n++].value.pointer.buttons = 1u; /* Bit of GR_BUTTON_NONE. */
    bad[n] = down, bad[n++].value.pointer.position.y = INFINITY;
    bad[n] = down, bad[n++].value.pointer.kind = (gr_pointer_kind)7;
    bad[n] = down, bad[n].type = GR_EVENT_POINTER_MOVE, ++n; /* MOVE carries no changed button. */
    bad[n] = scroll, bad[n++].value.scroll.unit = (gr_scroll_unit)2;
    bad[n] = scroll, bad[n++].value.scroll.delta.x = NAN;
    bad[n] = key, bad[n++].value.key.character = 0xD800;
    bad[n] = key, bad[n++].value.key.code = (gr_key_code)1000;
    bad[n] = compose, bad[n++].value.edit.text.data = NULL;
    bad[n] = command, bad[n++].value.command.command = (gr_text_command)99;
    bad[n] = viewport, bad[n++].value.viewport.scale = 0;
    bad[n] = frame(), bad[n++].value.frame.interval = -1;
    CHECK(n == sizeof(bad) / sizeof(bad[0]));
    OK(gr_scene_host_request_frame(host));
    for (size_t i = 0; i < n; ++i)
        CHECK(!gr_scene_host_dispatch_event(host, &bad[i]));
    CHECK(r.count == 6);
    CHECK(gr_scene_host_frame_requested(host)); /* An invalid FRAME does not consume the request. */
#endif
    gr_scene_host_unref(host);
    gr_scene_unref(scene);
}

static void state_events(void)
{
    platform p = {0};
    gr_scene *scene = gr_scene_new((gr_size){10, 10});
    gr_scene_host *passive_host = gr_scene_host_new(scene, &passive, &p);
    gr_viewport_event v = {{1, 1}, 1};
    CHECK(!gr_scene_host_get_viewport(passive_host, &v) && v.scale == 0 && v.size.width == 0);
    CHECK(!gr_scene_host_has_focus(passive_host));
    gr_scene_host_unref(passive_host);

    gr_scene_host *host = gr_scene_host_new(scene, &interactive, &p);
    CHECK(!gr_scene_host_get_viewport(host, &v));

    /* State is tracked without a handler, so the first report can arrive before one is installed. */
    gr_event viewport = {.type = GR_EVENT_VIEWPORT, .time = 1, .value.viewport = {{800, 600}, 2}};
    CHECK(!gr_scene_host_dispatch_event(host, &viewport));
    CHECK(gr_scene_host_get_viewport(host, &v) && v.size.width == 800 && v.size.height == 600 && v.scale == 2);
    gr_event in = {.type = GR_EVENT_FOCUS_IN, .time = 1}, out = {.type = GR_EVENT_FOCUS_OUT, .time = 2};
    CHECK(!gr_scene_host_dispatch_event(host, &in));
    CHECK(gr_scene_host_has_focus(host));

    recorder r = {.name = 'a', .log = p.log, .result = true};
    gr_event_handler h = handler(&r);
    OK(gr_scene_host_set_event_handler(host, &h));
    CHECK(!gr_scene_host_dispatch_event(host, &viewport)); /* Repeats are dropped. */
    CHECK(!gr_scene_host_dispatch_event(host, &in));
    CHECK(r.count == 0);
    viewport.value.viewport.scale = 1.5;
    CHECK(gr_scene_host_dispatch_event(host, &viewport));
    CHECK(gr_scene_host_dispatch_event(host, &out));
    CHECK(!gr_scene_host_dispatch_event(host, &out));
    CHECK(!gr_scene_host_has_focus(host));
    CHECK(gr_scene_host_get_viewport(host, &v) && v.scale == 1.5);
    CHECK(r.count == 2 && r.types[0] == GR_EVENT_VIEWPORT && r.types[1] == GR_EVENT_FOCUS_OUT);

#ifndef NDEBUG
    /* An invalid VIEWPORT leaves the state alone. */
    viewport.value.viewport.size.width = -1;
    CHECK(!gr_scene_host_dispatch_event(host, &viewport));
    CHECK(gr_scene_host_get_viewport(host, &v) && v.size.width == 800);
#endif
    gr_scene_host_unref(host);
    gr_scene_unref(scene);
}

int main(int argc, char **argv)
{
    const gr_test_case cases[] = {
        GR_TEST_CASE(passive_hosts),
        GR_TEST_CASE(host_commands),
        GR_TEST_CASE(handler_ownership),
        GR_TEST_CASE(frame_requests),
        GR_TEST_CASE(reentrant_dispatch),
        GR_TEST_CASE(replacement_during_delivery),
        GR_TEST_CASE(release_during_delivery),
        GR_TEST_CASE(event_validation),
        GR_TEST_CASE(state_events),
    };
    return gr_test_run("events", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
}
