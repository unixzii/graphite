#include "internal.h"

/* An event dispatched while a handler runs, copied with its text. */
typedef struct
{
    gr_event event;
    char *text;
} queued_event;

struct gr_scene_host
{
    size_t refs;
    gr_scene_link *scene;
    const gr_scene_host_vtable *identity;
    gr_scene_host_vtable vtable;
    void *data;

    gr_event_handler handler;
    bool has_handler, frame_requested, dispatching, focused, has_viewport;
    gr_viewport_event viewport;
    queued_event *queue;
    size_t queue_count, queue_capacity;
    gr_event_handler *retired; /* Handlers replaced during delivery, destroyed when it ends. */
    size_t retired_count, retired_capacity;
};

static bool interactive_vtable(const gr_scene_host_vtable *v)
{
    return v->request_frame && v->now && v->set_cursor && v->set_text_input && v->request_focus;
}

static bool passive_vtable(const gr_scene_host_vtable *v)
{
    return !v->request_frame && !v->now && !v->set_cursor && !v->set_text_input && !v->request_focus;
}

gr_scene_host *gr_scene_host_new(gr_scene *scene, const gr_scene_host_vtable *vtable, void *data)
{
    if (!scene || !vtable || !vtable->destroy || !(interactive_vtable(vtable) || passive_vtable(vtable)))
        return NULL;
    gr_scene_host *host = gr_alloc(1, sizeof(*host));
    *host = (gr_scene_host){
        .refs = 1, .scene = gr_scene_get_link(scene), .identity = vtable, .vtable = *vtable, .data = data};
    return host;
}

gr_scene_host *gr_scene_host_ref(gr_scene_host *host)
{
    if (host)
        gr_local_ref(&host->refs);
    return host;
}

static void destroy_handler(gr_event_handler handler)
{
    if (handler.destroy)
        handler.destroy(handler.userdata);
}

void gr_scene_host_unref(gr_scene_host *host)
{
    if (!host || !gr_local_unref(&host->refs))
        return;
    /* Delivery holds a reference, so nothing is queued or retired here. */
    if (host->has_handler)
    {
        host->has_handler = false;
        destroy_handler(host->handler);
    }
    host->vtable.destroy(host->data);
    gr_scene_link_unref(host->scene);
    free(host->queue);
    free(host->retired);
    free(host);
}

gr_scene *gr_scene_host_get_scene(const gr_scene_host *host)
{
    return gr_scene_link_lock(host->scene);
}

const gr_scene_host_vtable *gr_scene_host_get_vtable(const gr_scene_host *host)
{
    return host->identity;
}

void *gr_scene_host_get_data(const gr_scene_host *host)
{
    return host->data;
}

/* Event API */

bool gr_scene_host_is_interactive(const gr_scene_host *host)
{
    return host->vtable.request_frame != NULL;
}

gr_result gr_scene_host_set_event_handler(gr_scene_host *host, const gr_event_handler *handler)
{
    if (!gr_scene_host_is_interactive(host))
        return GR_ERROR_UNSUPPORTED;
    if (handler && !handler->handle)
        return GR_ERROR_ARGUMENT;
    gr_event_handler old = host->handler;
    bool had = host->has_handler;
    host->has_handler = handler != NULL;
    host->handler = handler ? *handler : (gr_event_handler){0};
    if (!had)
        return GR_OK;
    if (host->dispatching)
    {
        gr_reserve((void **)&host->retired, &host->retired_capacity, host->retired_count + 1, sizeof(*host->retired));
        host->retired[host->retired_count++] = old;
    }
    else
        destroy_handler(old);
    return GR_OK;
}

gr_result gr_scene_host_request_frame(gr_scene_host *host)
{
    if (!gr_scene_host_is_interactive(host))
        return GR_ERROR_UNSUPPORTED;
    if (!host->frame_requested)
    {
        host->frame_requested = true;
        host->vtable.request_frame(host->data);
    }
    return GR_OK;
}

bool gr_scene_host_frame_requested(const gr_scene_host *host)
{
    return host->frame_requested;
}

bool gr_scene_host_get_viewport(const gr_scene_host *host, gr_viewport_event *out)
{
    *out = host->has_viewport ? host->viewport : (gr_viewport_event){{0, 0}, 0};
    return host->has_viewport;
}

bool gr_scene_host_has_focus(const gr_scene_host *host)
{
    return host->focused;
}

double gr_scene_host_get_time(const gr_scene_host *host)
{
    return gr_scene_host_is_interactive(host) ? host->vtable.now(host->data) : NAN;
}

gr_result gr_scene_host_set_cursor(gr_scene_host *host, gr_cursor cursor)
{
    if (!gr_scene_host_is_interactive(host))
        return GR_ERROR_UNSUPPORTED;
    if ((unsigned)cursor > GR_CURSOR_NONE)
        return GR_ERROR_ARGUMENT;
    host->vtable.set_cursor(host->data, cursor);
    return GR_OK;
}

gr_result gr_scene_host_request_focus(gr_scene_host *host)
{
    if (!gr_scene_host_is_interactive(host))
        return GR_ERROR_UNSUPPORTED;
    host->vtable.request_focus(host->data);
    return GR_OK;
}

static bool finite_point(gr_point p)
{
    return isfinite(p.x) && isfinite(p.y);
}

gr_result gr_scene_host_set_text_input(gr_scene_host *host, const gr_text_input *input)
{
    if (!gr_scene_host_is_interactive(host))
        return GR_ERROR_UNSUPPORTED;
    if (input)
    {
        gr_rect c = input->caret;
        if (!finite_point(c.origin) || !isfinite(c.size.width) || !isfinite(c.size.height) || c.size.width < 0 ||
            c.size.height < 0)
            return GR_ERROR_ARGUMENT;
    }
    host->vtable.set_text_input(host->data, input);
    return GR_OK;
}

/* Delivery */

/* Records VIEWPORT and FOCUS state; returns false for an event that repeats it. */
static bool update_state(gr_scene_host *host, const gr_event *e)
{
    switch (e->type)
    {
        case GR_EVENT_VIEWPORT:
        {
            const gr_viewport_event *v = &e->value.viewport;
            if (host->has_viewport && host->viewport.size.width == v->size.width &&
                host->viewport.size.height == v->size.height && host->viewport.scale == v->scale)
                return false;
            host->has_viewport = true;
            host->viewport = *v;
            return true;
        }
        case GR_EVENT_FOCUS_IN:
        case GR_EVENT_FOCUS_OUT:
        {
            bool focused = e->type == GR_EVENT_FOCUS_IN;
            if (host->focused == focused)
                return false;
            host->focused = focused;
            return true;
        }
        default:
            return true;
    }
}

/* Events come from backends, which build them from native input; debug builds check that contract. */
#ifndef NDEBUG
static bool valid_pointer(gr_event_type type, const gr_pointer_event *p)
{
    const uint32_t buttons = (1u << GR_BUTTON_PRIMARY) | (1u << GR_BUTTON_SECONDARY) | (1u << GR_BUTTON_MIDDLE) |
                             (1u << GR_BUTTON_BACK) | (1u << GR_BUTTON_FORWARD);
    if ((unsigned)p->kind > GR_POINTER_TOUCH || (unsigned)p->button > GR_BUTTON_FORWARD || !finite_point(p->position) ||
        (p->buttons & ~buttons) || !isfinite(p->pressure) || p->pressure < 0 || p->pressure > 1)
        return false;
    bool press = type == GR_EVENT_POINTER_DOWN || type == GR_EVENT_POINTER_UP;
    return press ? p->button != GR_BUTTON_NONE : p->button == GR_BUTTON_NONE;
}

static bool valid_event(const gr_event *e)
{
    const gr_modifiers modifiers =
        GR_MODIFIER_SHIFT | GR_MODIFIER_CONTROL | GR_MODIFIER_ALT | GR_MODIFIER_SUPER | GR_MODIFIER_CAPS_LOCK;
    if ((unsigned)e->type > GR_EVENT_TEXT_PREEDIT || !isfinite(e->time) || (e->modifiers & ~modifiers))
        return false;
    switch (e->type)
    {
        case GR_EVENT_FRAME:
        {
            const gr_frame_event *f = &e->value.frame;
            return isfinite(f->target_time) && isfinite(f->interval) && f->interval >= 0;
        }
        case GR_EVENT_VIEWPORT:
        {
            const gr_viewport_event *v = &e->value.viewport;
            return isfinite(v->size.width) && isfinite(v->size.height) && v->size.width >= 0 && v->size.height >= 0 &&
                   isfinite(v->scale) && v->scale > 0;
        }
        case GR_EVENT_FOCUS_IN:
        case GR_EVENT_FOCUS_OUT:
            return true;
        case GR_EVENT_POINTER_DOWN:
        case GR_EVENT_POINTER_UP:
        case GR_EVENT_POINTER_MOVE:
        case GR_EVENT_POINTER_ENTER:
        case GR_EVENT_POINTER_LEAVE:
        case GR_EVENT_POINTER_CANCEL:
            return valid_pointer(e->type, &e->value.pointer);
        case GR_EVENT_SCROLL:
        {
            const gr_scroll_event *s = &e->value.scroll;
            return finite_point(s->position) && finite_point(s->delta) && (unsigned)s->unit <= GR_SCROLL_UNIT_LINE &&
                   (unsigned)s->phase <= GR_SCROLL_PHASE_END;
        }
        case GR_EVENT_KEY_DOWN:
        case GR_EVENT_KEY_UP:
        {
            const gr_key_event *k = &e->value.key;
            return (unsigned)k->code <= GR_KEY_CODE_AUDIO_VOLUME_UP && (unsigned)k->key <= GR_KEY_F24 &&
                   k->character <= 0x10FFFF && !(k->character >= 0xD800 && k->character <= 0xDFFF);
        }
        case GR_EVENT_TEXT_COMMIT:
        case GR_EVENT_TEXT_PREEDIT:
        {
            const gr_text_event *t = &e->value.text;
            if (t->length && !t->text)
                return false;
            if (e->type == GR_EVENT_TEXT_COMMIT)
                return t->selection_start == 0 && t->selection_end == 0;
            return t->selection_start <= t->selection_end && t->selection_end <= t->length;
        }
    }
    return false;
}

#endif

static bool is_text(gr_event_type type)
{
    return type == GR_EVENT_TEXT_COMMIT || type == GR_EVENT_TEXT_PREEDIT;
}

static bool deliver(gr_scene_host *host, const gr_event *event)
{
    if (event->type == GR_EVENT_FRAME)
    {
        if (!host->frame_requested)
            return false;
        host->frame_requested = false;
    }
    if (!host->has_handler)
        return false;
    gr_event_handler handler = host->handler;
    return handler.handle(handler.userdata, event);
}

bool gr_scene_host_dispatch_event(gr_scene_host *host, const gr_event *event)
{
    if (!gr_scene_host_is_interactive(host) || !event)
        return false;
#ifndef NDEBUG
    if (!valid_event(event))
        return false;
#endif
    if (!update_state(host, event))
        return false;
    if (host->dispatching)
    {
        gr_reserve((void **)&host->queue, &host->queue_capacity, host->queue_count + 1, sizeof(*host->queue));
        queued_event *q = &host->queue[host->queue_count++];
        q->event = *event;
        q->text = NULL;
        if (is_text(event->type) && event->value.text.length)
        {
            q->text = gr_copy(event->value.text.text, event->value.text.length, 1);
            q->event.value.text.text = q->text;
        }
        return false;
    }

    gr_scene_host_ref(host);
    host->dispatching = true;
    bool handled = deliver(host, event);
    for (size_t i = 0; i < host->queue_count; ++i)
    {
        /* Copy out first: delivery may queue more events and move the array. */
        queued_event q = host->queue[i];
        deliver(host, &q.event);
        free(q.text);
    }
    host->queue_count = 0;
    host->dispatching = false;
    /* Detach the list first: destructors may use the host again, and replacements now destroy immediately. */
    gr_event_handler *retired = host->retired;
    size_t retired_count = host->retired_count;
    host->retired = NULL;
    host->retired_count = host->retired_capacity = 0;
    for (size_t i = 0; i < retired_count; ++i)
        destroy_handler(retired[i]);
    free(retired);
    gr_scene_host_unref(host);
    return handled;
}
