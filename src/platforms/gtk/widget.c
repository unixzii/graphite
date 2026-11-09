#include "internal.h"

typedef struct
{
    GtkWidget parent_instance;
    gr_gtk_shared *shared;
    gr_scene_host *host; /* Borrowed; NULL once the host is destroyed. */
    GdkFrameClock *clock;
    GdkSurface *surface;
    GtkWindow *window;
    gulong layout_handler, update_handler, scale_handler, active_handler;
    guint timer; /* The timeout source of the host's timer, or 0. */
    /* Input state. */
    bool inside, scrolling;
    uint32_t buttons; /* Pressed buttons, 1u << gr_button. */
    uint32_t clicks;
    gr_button click_button;
    guint32 click_time;
    gr_point click_position;
    guint64 keys_down[4]; /* Bitset of pressed keycodes, to recognize auto-repeat. */
    gr_point pointer;     /* Last known pointer position; Wayland scroll events carry none. */
    /* Text input: the shared state with this widget's edits applied; text is owned, layout referenced. */
    GtkIMContext *im;
    bool text_on, im_focused, resetting;
    gr_text_input text_input;
    char *text;
} GrGraphiteWidget;

typedef struct
{
    GtkWidgetClass parent_class;
} GrGraphiteWidgetClass;
G_DEFINE_TYPE(GrGraphiteWidget, gr_graphite_widget, GTK_TYPE_WIDGET)

void gr_gtk_widget_request(GtkWidget *widget)
{
    GdkFrameClock *clock = gtk_widget_get_frame_clock(widget);
    if (clock)
        gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_LAYOUT);
}

static bool dispatch(GrGraphiteWidget *w, gr_event event)
{
    return w->host && gr_scene_host_dispatch_event(w->host, &event);
}

static double seconds(gint64 microseconds)
{
    return (double)microseconds / G_TIME_SPAN_SECOND;
}

static void clear_text_input(GrGraphiteWidget *w);

void gr_gtk_widget_set_host(GtkWidget *widget, gr_scene_host *host)
{
    GrGraphiteWidget *w = (GrGraphiteWidget *)widget;
    w->host = host;
    if (!host)
    {
        g_clear_handle_id(&w->timer, g_source_remove);
        clear_text_input(w);
    }
}

/* Timers: a timeout on the default main context, which counts on the monotonic clock the frame clock uses. */
static gboolean timer_fired(gpointer userdata)
{
    GrGraphiteWidget *w = userdata;
    w->timer = 0;
    dispatch(w, (gr_event){.type = GR_EVENT_TIMER, .time = seconds(g_get_monotonic_time())});
    return G_SOURCE_REMOVE;
}

void gr_gtk_widget_set_timer(GtkWidget *widget, double deadline)
{
    GrGraphiteWidget *w = (GrGraphiteWidget *)widget;
    g_clear_handle_id(&w->timer, g_source_remove);
    if (!w->host || deadline == INFINITY)
        return;
    /* Round up to whole milliseconds so the timeout does not fire early; core reschedules one that does. */
    double ms = ceil((deadline - seconds(g_get_monotonic_time())) * 1000);
    w->timer = g_timeout_add((guint)fmin(fmax(ms, 0), G_MAXUINT), timer_fired, w);
}

/* Frames run in the update phase, before the layout phase where the backend commits. */
void gr_gtk_widget_request_frame(GtkWidget *widget)
{
    GdkFrameClock *clock = gtk_widget_get_frame_clock(widget);
    if (clock)
        gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_UPDATE);
}

static void update_frame(GdkFrameClock *clock, gpointer userdata)
{
    GrGraphiteWidget *w = userdata;
    /* The update phase also runs for other widgets' tick callbacks. */
    if (!w->host || !gr_scene_host_frame_requested(w->host))
        return;
    gint64 time = gdk_frame_clock_get_frame_time(clock), interval = 0, presentation = 0;
    gdk_frame_clock_get_refresh_info(clock, time, &interval, &presentation);
    if (interval <= 0)
        interval = G_TIME_SPAN_SECOND / 60;
    if (presentation <= time)
        presentation = time + interval;
    dispatch(w, (gr_event){.type = GR_EVENT_FRAME,
                           .time = seconds(time),
                           .value.frame = {.target_time = seconds(presentation), .interval = seconds(interval)}});
}

static void report_viewport(GrGraphiteWidget *w)
{
    if (!w->surface)
        return;
    GtkWidget *widget = GTK_WIDGET(w);
    dispatch(w, (gr_event){.type = GR_EVENT_VIEWPORT,
                           .time = seconds(g_get_monotonic_time()),
                           .value.viewport = {{gtk_widget_get_width(widget), gtk_widget_get_height(widget)},
                                              gdk_surface_get_scale(w->surface)}});
}

/* The widget has keyboard focus while it is the focus widget of the active window. */
static bool has_focus(GrGraphiteWidget *w)
{
    return gtk_widget_has_focus(GTK_WIDGET(w)) && (!w->window || gtk_window_is_active(w->window));
}

static void update_im_focus(GrGraphiteWidget *w);

static void update_focus(GrGraphiteWidget *w)
{
    bool focused = has_focus(w);
    if (!focused)
        memset(w->keys_down, 0, sizeof(w->keys_down)); /* Releases may go elsewhere. */
    update_im_focus(w);
    dispatch(w, (gr_event){.type = focused ? GR_EVENT_FOCUS_IN : GR_EVENT_FOCUS_OUT,
                           .time = seconds(g_get_monotonic_time())});
}

static void focus_changed(GtkEventControllerFocus *controller, gpointer userdata)
{
    (void)controller;
    update_focus(userdata);
}

static void window_active_changed(GObject *object, GParamSpec *spec, gpointer userdata)
{
    (void)object;
    (void)spec;
    update_focus(userdata);
}

/* Input */

static gr_modifiers modifiers(GdkModifierType state)
{
    return (state & GDK_SHIFT_MASK ? GR_MODIFIER_SHIFT : 0) | (state & GDK_CONTROL_MASK ? GR_MODIFIER_CONTROL : 0) |
           (state & GDK_ALT_MASK ? GR_MODIFIER_ALT : 0) |
           (state & (GDK_SUPER_MASK | GDK_META_MASK | GDK_HYPER_MASK) ? GR_MODIFIER_SUPER : 0) |
           (state & GDK_LOCK_MASK ? GR_MODIFIER_CAPS_LOCK : 0);
}

static double event_seconds(GdkEvent *event)
{
    return gdk_event_get_time(event) / 1000.0;
}

/* Event positions are relative to the surface; convert them to the widget. */
static bool widget_point(GrGraphiteWidget *w, double x, double y, gr_point *out)
{
    GtkWidget *widget = GTK_WIDGET(w);
    GtkNative *native = gtk_widget_get_native(widget);
    if (!native)
        return false;
    double nx, ny;
    gtk_native_get_surface_transform(native, &nx, &ny);
    graphene_point_t p = GRAPHENE_POINT_INIT((float)(x - nx), (float)(y - ny)), r;
    if (!gtk_widget_compute_point(GTK_WIDGET(native), widget, &p, &r))
        return false;
    *out = (gr_point){r.x, r.y};
    return true;
}

static bool event_point(GrGraphiteWidget *w, GdkEvent *event, gr_point *out)
{
    double x, y;
    if (!gdk_event_get_position(event, &x, &y) || !widget_point(w, x, y, out))
        return false;
    w->pointer = *out;
    return true;
}

static bool contains(GrGraphiteWidget *w, gr_point p)
{
    GtkWidget *widget = GTK_WIDGET(w);
    return p.x >= 0 && p.y >= 0 && p.x < gtk_widget_get_width(widget) && p.y < gtk_widget_get_height(widget);
}

static gr_event pointer_event(GrGraphiteWidget *w, gr_event_type type, GdkEvent *event, gr_point position)
{
    GdkDeviceTool *tool = gdk_event_get_device_tool(event);
    GdkDeviceToolType kind = tool ? gdk_device_tool_get_tool_type(tool) : GDK_DEVICE_TOOL_TYPE_UNKNOWN;
    bool pen = kind == GDK_DEVICE_TOOL_TYPE_PEN || kind == GDK_DEVICE_TOOL_TYPE_ERASER ||
               kind == GDK_DEVICE_TOOL_TYPE_BRUSH || kind == GDK_DEVICE_TOOL_TYPE_PENCIL ||
               kind == GDK_DEVICE_TOOL_TYPE_AIRBRUSH;
    double pressure = 0;
    if (!(pen && gdk_event_get_axis(event, GDK_AXIS_PRESSURE, &pressure)))
        pressure = w->buttons ? 0.5 : 0;
    gr_event e = {
        .type = type, .time = event_seconds(event), .modifiers = modifiers(gdk_event_get_modifier_state(event))};
    e.value.pointer = (gr_pointer_event){
        .kind = pen ? GR_POINTER_PEN : GR_POINTER_MOUSE,
        .position = position,
        .buttons = w->buttons,
        .pressure = (float)fmin(fmax(pressure, 0), 1),
    };
    return e;
}

static void hover(GrGraphiteWidget *w, bool inside, gr_point position, guint32 time)
{
    if (inside)
        w->pointer = position;
    if (inside == w->inside || !w->host)
        return;
    w->inside = inside;
    gr_event e = {.type = inside ? GR_EVENT_POINTER_ENTER : GR_EVENT_POINTER_LEAVE, .time = time / 1000.0};
    e.value.pointer = (gr_pointer_event){.kind = GR_POINTER_MOUSE, .position = position};
    dispatch(w, e);
}

static gr_button button_of(guint button)
{
    switch (button)
    {
        case GDK_BUTTON_PRIMARY:
            return GR_BUTTON_PRIMARY;
        case GDK_BUTTON_MIDDLE:
            return GR_BUTTON_MIDDLE;
        case GDK_BUTTON_SECONDARY:
            return GR_BUTTON_SECONDARY;
        case 8:
            return GR_BUTTON_BACK;
        case 9:
            return GR_BUTTON_FORWARD;
        default:
            return GR_BUTTON_NONE;
    }
}

/* Counts consecutive presses of one button within the double-click time and distance. */
static uint32_t count_click(GrGraphiteWidget *w, gr_button button, guint32 time, gr_point p)
{
    int interval = 400, distance = 5;
    g_object_get(gtk_widget_get_settings(GTK_WIDGET(w)), "gtk-double-click-time", &interval,
                 "gtk-double-click-distance", &distance, NULL);
    bool repeat = w->clicks && button == w->click_button && time - w->click_time <= (guint32)interval &&
                  fabs(p.x - w->click_position.x) <= distance && fabs(p.y - w->click_position.y) <= distance;
    w->clicks = repeat ? w->clicks + 1 : 1;
    w->click_button = button;
    w->click_time = time;
    w->click_position = p;
    return w->clicks;
}

/* A press grabs the pointer implicitly, so moves and the release reach the widget until every button is up. */
static gboolean press(GrGraphiteWidget *w, GdkEvent *event, bool down)
{
    gr_button button = button_of(gdk_button_event_get_button(event));
    gr_point p;
    if (button == GR_BUTTON_NONE || !event_point(w, event, &p))
        return GDK_EVENT_PROPAGATE;
    if (down)
    {
        gtk_widget_grab_focus(GTK_WIDGET(w));
        if (!w->host)
            return GDK_EVENT_PROPAGATE;
    }
    uint32_t bit = 1u << button;
    w->buttons = down ? w->buttons | bit : w->buttons & ~bit;
    gr_event e = pointer_event(w, down ? GR_EVENT_POINTER_DOWN : GR_EVENT_POINTER_UP, event, p);
    e.value.pointer.button = button;
    e.value.pointer.click_count = down ? count_click(w, button, gdk_event_get_time(event), p) : w->clicks;
    bool handled = dispatch(w, e);
    /* Crossings are not reported during drags; catch up once the last button is released. */
    if (!w->buttons)
        hover(w, contains(w, p), p, gdk_event_get_time(event));
    return handled ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

static gboolean motion(GrGraphiteWidget *w, GdkEvent *event)
{
    gr_point p;
    if (!event_point(w, event, &p))
        return GDK_EVENT_PROPAGATE;
    if (!w->buttons)
        hover(w, true, p, gdk_event_get_time(event));
    return dispatch(w, pointer_event(w, GR_EVENT_POINTER_MOVE, event, p)) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

static gboolean scroll(GrGraphiteWidget *w, GdkEvent *event)
{
    gr_point p;
    if (!event_point(w, event, &p))
        p = w->pointer;
    gr_event e = {.type = GR_EVENT_SCROLL,
                  .time = event_seconds(event),
                  .modifiers = modifiers(gdk_event_get_modifier_state(event))};
    gr_scroll_event *s = &e.value.scroll;
    s->position = p;
    s->unit = GR_SCROLL_UNIT_LINE;
    /* Discrete wheel events carry deltas too, which follow the device even where the direction does not. */
    GdkScrollDirection direction = gdk_scroll_event_get_direction(event);
    gdk_scroll_event_get_deltas(event, &s->delta.x, &s->delta.y);
    if (s->delta.x == 0 && s->delta.y == 0)
    {
        s->delta.x = direction == GDK_SCROLL_LEFT ? -1 : direction == GDK_SCROLL_RIGHT ? 1 : 0;
        s->delta.y = direction == GDK_SCROLL_UP ? -1 : direction == GDK_SCROLL_DOWN ? 1 : 0;
    }
    if (direction == GDK_SCROLL_SMOOTH)
    {
        if (gdk_scroll_event_get_unit(event) == GDK_SCROLL_UNIT_SURFACE)
            s->unit = GR_SCROLL_UNIT_POINT;
        /* Touchpads report gestures; a stop event ends one. */
        GdkDevice *device = gdk_event_get_device(event);
        if (device && gdk_device_get_source(device) == GDK_SOURCE_TOUCHPAD)
        {
            bool stop = gdk_scroll_event_is_stop(event);
            s->phase = stop ? GR_SCROLL_PHASE_END : w->scrolling ? GR_SCROLL_PHASE_UPDATE : GR_SCROLL_PHASE_BEGIN;
            w->scrolling = !stop;
        }
    }
    return dispatch(w, e) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

/* The modifier a key holds, or 0. */
static gr_modifiers key_modifier(gr_key_code code)
{
    switch (code)
    {
        case GR_KEY_CODE_SHIFT_LEFT:
        case GR_KEY_CODE_SHIFT_RIGHT:
            return GR_MODIFIER_SHIFT;
        case GR_KEY_CODE_CONTROL_LEFT:
        case GR_KEY_CODE_CONTROL_RIGHT:
            return GR_MODIFIER_CONTROL;
        case GR_KEY_CODE_ALT_LEFT:
        case GR_KEY_CODE_ALT_RIGHT:
            return GR_MODIFIER_ALT;
        case GR_KEY_CODE_META_LEFT:
        case GR_KEY_CODE_META_RIGHT:
            return GR_MODIFIER_SUPER;
        default:
            return 0;
    }
}

/* GDK reports the modifier state before a key event; other platforms report it after. For modifier keys, recompute
 * the key's own modifier from the keys still down, so releasing one Shift while the other is held keeps Shift. */
static gr_modifiers modifiers_after(GrGraphiteWidget *w, gr_modifiers before, gr_key_code code)
{
    gr_modifiers own = key_modifier(code);
    if (!own)
        return before;
    bool held = false;
    for (guint keycode = 0; keycode < 256 && !held; ++keycode)
        held = (w->keys_down[keycode / 64] >> (keycode % 64) & 1) && key_modifier(gr_gtk_key_code(keycode)) == own;
    return held ? before | own : before & ~own;
}

/* Text input. The widget keeps a copy of the shared state (see host.h), answers its GtkIMContext from it and turns
 * input method signals and key bindings into TEXT_EDIT and TEXT_COMMAND events. */

/* Editing key bindings, following GtkText. */
static bool key_command(guint keyval, GdkModifierType state, gr_text_command_event *out)
{
    state &= GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK | GDK_HYPER_MASK;
    bool shift = state & GDK_SHIFT_MASK, control = state & GDK_CONTROL_MASK;
    if (state & ~(GDK_SHIFT_MASK | GDK_CONTROL_MASK))
        return false;
    gr_text_command command;
    bool extend = false;
    switch (keyval)
    {
        case GDK_KEY_BackSpace:
            command = !control ? GR_TEXT_COMMAND_DELETE_BACKWARD
                      : shift  ? GR_TEXT_COMMAND_DELETE_TO_LINE_START
                               : GR_TEXT_COMMAND_DELETE_WORD_BACKWARD;
            break;
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
            if (shift && !control)
                return false; /* Cut. */
            command = !control ? GR_TEXT_COMMAND_DELETE_FORWARD
                      : shift  ? GR_TEXT_COMMAND_DELETE_TO_LINE_END
                               : GR_TEXT_COMMAND_DELETE_WORD_FORWARD;
            break;
        case GDK_KEY_Left:
        case GDK_KEY_KP_Left:
            command = control ? GR_TEXT_COMMAND_MOVE_WORD_LEFT : GR_TEXT_COMMAND_MOVE_LEFT;
            extend = shift;
            break;
        case GDK_KEY_Right:
        case GDK_KEY_KP_Right:
            command = control ? GR_TEXT_COMMAND_MOVE_WORD_RIGHT : GR_TEXT_COMMAND_MOVE_RIGHT;
            extend = shift;
            break;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            command = GR_TEXT_COMMAND_MOVE_UP;
            extend = shift;
            break;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            command = GR_TEXT_COMMAND_MOVE_DOWN;
            extend = shift;
            break;
        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            command = control ? GR_TEXT_COMMAND_MOVE_DOCUMENT_START : GR_TEXT_COMMAND_MOVE_LINE_START;
            extend = shift;
            break;
        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            command = control ? GR_TEXT_COMMAND_MOVE_DOCUMENT_END : GR_TEXT_COMMAND_MOVE_LINE_END;
            extend = shift;
            break;
        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            command = GR_TEXT_COMMAND_MOVE_PAGE_UP;
            extend = shift;
            break;
        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            command = GR_TEXT_COMMAND_MOVE_PAGE_DOWN;
            extend = shift;
            break;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_ISO_Enter:
            if (control)
                return false;
            command = GR_TEXT_COMMAND_INSERT_NEWLINE;
            break;
        case GDK_KEY_Tab:
        case GDK_KEY_KP_Tab:
        case GDK_KEY_ISO_Left_Tab:
            if (control)
                return false;
            command =
                shift || keyval == GDK_KEY_ISO_Left_Tab ? GR_TEXT_COMMAND_INSERT_BACKTAB : GR_TEXT_COMMAND_INSERT_TAB;
            break;
        case GDK_KEY_Escape:
            if (state)
                return false;
            command = GR_TEXT_COMMAND_CANCEL;
            break;
        case GDK_KEY_a:
        case GDK_KEY_A:
            if (!control || shift)
                return false;
            command = GR_TEXT_COMMAND_SELECT_ALL;
            break;
        default:
            return false;
    }
    *out = (gr_text_command_event){command, extend};
    return true;
}

static bool composing(const GrGraphiteWidget *w)
{
    return w->text_on && w->text_input.composition.start < w->text_input.composition.end;
}

/* Replaces the stored state with a copy of input, or clears it with NULL. */
static void store_text_input(GrGraphiteWidget *w, const gr_text_input *input)
{
    char *text = input ? g_strndup(input->text.data ? input->text.data : "", input->text.count) : NULL;
    gr_text_layout *layout = input ? gr_text_layout_ref(input->layout) : NULL;
    g_free(w->text);
    gr_text_layout_unref(w->text_input.layout);
    w->text = text;
    w->text_on = input != NULL;
    w->text_input = input ? *input : (gr_text_input){0};
    w->text_input.text = (gr_string){text, input ? input->text.count : 0};
    w->text_input.layout = layout;
}

/* Where a position lands after range is replaced by count bytes. */
static size_t moved(size_t at, gr_text_range range, size_t count)
{
    return at <= range.start ? at : at >= range.end ? at - (range.end - range.start) + count : range.start + count;
}

/* Applies one of this widget's edits to the stored state, then delivers it. */
static void text_edit(GrGraphiteWidget *w, gr_text_range replace, const char *text, size_t count,
                      gr_text_range selection, gr_text_range composition)
{
    gr_string old = w->text_input.text;
    size_t length = old.count - (replace.end - replace.start) + count;
    char *joined = g_malloc(length + 1);
    memcpy(joined, old.data, replace.start);
    if (count)
        memcpy(joined + replace.start, text, count);
    memcpy(joined + replace.start + count, old.data + replace.end, old.count - replace.end);
    joined[length] = 0;
    g_free(w->text);
    w->text = joined;
    w->text_input.text = (gr_string){joined, length};
    w->text_input.selection = selection;
    w->text_input.composition = composition;
    gr_event e = {.type = GR_EVENT_TEXT_EDIT, .time = seconds(g_get_monotonic_time())};
    e.value.edit = (gr_text_edit_event){replace, {text, count}, selection, composition};
    dispatch(w, e);
}

/* What typing replaces: the composition, or else the selection. */
static gr_text_range typing_target(const GrGraphiteWidget *w)
{
    return composing(w) ? w->text_input.composition : w->text_input.selection;
}

static void im_commit(GtkIMContext *im, const char *text, gpointer userdata)
{
    (void)im;
    GrGraphiteWidget *w = userdata;
    if (!w->text_on || w->resetting)
        return;
    gr_text_range target = typing_target(w);
    size_t count = strlen(text), end = target.start + count;
    text_edit(w, target, text, count, (gr_text_range){end, end}, (gr_text_range){end, end});
}

static void im_preedit_changed(GtkIMContext *im, gpointer userdata)
{
    GrGraphiteWidget *w = userdata;
    if (!w->text_on || w->resetting)
        return;
    char *text = NULL;
    int cursor = 0;
    gtk_im_context_get_preedit_string(im, &text, NULL, &cursor);
    size_t count = strlen(text);
    if (count || composing(w))
    {
        gr_text_range target = typing_target(w);
        size_t at = target.start;
        size_t caret = at + (size_t)(g_utf8_offset_to_pointer(text, cursor) - text);
        text_edit(w, target, text, count, (gr_text_range){caret, caret}, (gr_text_range){at, at + count});
    }
    g_free(text);
}

/* Input methods see the text around the caret without the composition, which they hold themselves. */
static gboolean im_retrieve_surrounding(GtkIMContext *im, gpointer userdata)
{
    GrGraphiteWidget *w = userdata;
    if (!w->text_on)
        return FALSE;
    if (w->text_input.kind == GR_TEXT_INPUT_PASSWORD)
    {
        gtk_im_context_set_surrounding_with_selection(im, "", 0, 0, 0);
        return TRUE;
    }
    gr_string text = w->text_input.text;
    gr_text_range composition = w->text_input.composition, selection = w->text_input.selection;
    size_t cut = composition.end - composition.start;
    char *surrounding = g_malloc(text.count - cut + 1);
    memcpy(surrounding, text.data, composition.start);
    memcpy(surrounding + composition.start, text.data + composition.end, text.count - composition.end);
    surrounding[text.count - cut] = 0;
    size_t anchor = cut ? composition.start : selection.start, caret = cut ? composition.start : selection.end;
    gtk_im_context_set_surrounding_with_selection(im, surrounding, (int)(text.count - cut), (int)caret, (int)anchor);
    g_free(surrounding);
    return TRUE;
}

/* Deletes n_chars characters starting offset characters from the caret, in the text without the composition. */
static gboolean im_delete_surrounding(GtkIMContext *im, int offset, int n_chars, gpointer userdata)
{
    (void)im;
    GrGraphiteWidget *w = userdata;
    if (!w->text_on || n_chars < 0)
        return FALSE;
    gr_string text = w->text_input.text;
    gr_text_range composition = w->text_input.composition;
    size_t caret = composing(w) ? composition.start : w->text_input.selection.end;
    /* Walk characters in the full text, stepping over the composition. */
    const char *p = text.data + caret, *start = text.data, *end = text.data + text.count;
    for (; offset < 0 && p > start; ++offset)
    {
        p = g_utf8_prev_char(p);
        if ((size_t)(p - start) >= composition.start && (size_t)(p - start) < composition.end)
            p = start + composition.start;
    }
    for (; offset > 0 && p < end; --offset)
    {
        if ((size_t)(p - start) == composition.start && composition.start < composition.end)
            p = start + composition.end;
        p = g_utf8_next_char(p);
    }
    gr_text_range range = {(size_t)(p - start), (size_t)(p - start)};
    for (; n_chars > 0 && p < end; --n_chars)
        p = g_utf8_next_char(p);
    range.end = (size_t)(p - start);
    if (range.start < composition.end && range.end > composition.start)
        return FALSE; /* Never reaches into the composition. */
    gr_text_range selection = {moved(w->text_input.selection.start, range, 0),
                               moved(w->text_input.selection.end, range, 0)};
    gr_text_range kept = {moved(composition.start, range, 0), moved(composition.end, range, 0)};
    text_edit(w, range, "", 0, selection, kept);
    return TRUE;
}

/* Host-space rectangle of the caret at offset, from the shared layout; without one, the shared caret. */
static gr_rect caret_rect(const GrGraphiteWidget *w, size_t offset)
{
    const gr_text_input *in = &w->text_input;
    if (!in->layout)
        return in->caret;
    gr_rect r;
    gr_text_layout_get_caret_rect(in->layout, (gr_text_position){offset, GR_TEXT_AFFINITY_DOWNSTREAM}, &r);
    r.origin.x += in->origin.x;
    r.origin.y += in->origin.y;
    return r;
}

/* Input methods place their windows by the caret, at the start of the composition while there is one. */
static void update_cursor_location(GrGraphiteWidget *w)
{
    gr_rect r = caret_rect(w, composing(w) ? w->text_input.composition.start : w->text_input.selection.end);
    double x = floor(r.origin.x), y = floor(r.origin.y);
    GdkRectangle area = {(int)x, (int)y, (int)ceil(r.origin.x + r.size.width - x),
                         (int)ceil(r.origin.y + r.size.height - y)};
    gtk_im_context_set_cursor_location(w->im, &area);
}

static void update_im_focus(GrGraphiteWidget *w)
{
    bool focused = w->text_on && w->host && has_focus(w);
    if (focused == w->im_focused)
        return;
    w->im_focused = focused;
    if (focused)
        gtk_im_context_focus_in(w->im);
    else
        gtk_im_context_focus_out(w->im);
}

/* Drops the input method's composition without hearing about it. */
static void reset_im(GrGraphiteWidget *w)
{
    w->resetting = true;
    gtk_im_context_reset(w->im);
    w->resetting = false;
}

void gr_gtk_widget_set_text_input(GtkWidget *widget, const gr_text_input *input)
{
    GrGraphiteWidget *w = (GrGraphiteWidget *)widget;
    bool restart = input && (!w->text_on || input->kind != w->text_input.kind);
    bool recompose = input && w->text_on &&
                     (input->composition.start != w->text_input.composition.start ||
                      input->composition.end != w->text_input.composition.end);
    bool had_composition = composing(w);
    store_text_input(w, input);
    if (!input)
    {
        if (had_composition)
            reset_im(w);
        update_im_focus(w);
        return;
    }
    if (restart)
    {
        bool password = input->kind == GR_TEXT_INPUT_PASSWORD;
        g_object_set(w->im, "input-purpose", password ? GTK_INPUT_PURPOSE_PASSWORD : GTK_INPUT_PURPOSE_FREE_FORM,
                     "input-hints", password ? GTK_INPUT_HINT_PRIVATE | GTK_INPUT_HINT_NO_SPELLCHECK : 0, NULL);
    }
    else if (recompose)
        reset_im(w);
    update_im_focus(w);
    update_cursor_location(w);
}

static void clear_text_input(GrGraphiteWidget *w)
{
    if (composing(w))
        reset_im(w);
    store_text_input(w, NULL);
    update_im_focus(w);
}

static void use_im_context(GrGraphiteWidget *w, GtkIMContext *im)
{
    if (w->im)
    {
        if (w->im_focused)
            gtk_im_context_focus_out(w->im);
        g_signal_handlers_disconnect_by_data(w->im, w);
        gtk_im_context_set_client_widget(w->im, NULL);
        g_clear_object(&w->im);
    }
    w->im_focused = false;
    if (!im)
        return;
    w->im = g_object_ref(im);
    g_signal_connect(im, "commit", G_CALLBACK(im_commit), w);
    g_signal_connect(im, "preedit-changed", G_CALLBACK(im_preedit_changed), w);
    g_signal_connect(im, "retrieve-surrounding", G_CALLBACK(im_retrieve_surrounding), w);
    g_signal_connect(im, "delete-surrounding", G_CALLBACK(im_delete_surrounding), w);
    if (gtk_widget_get_realized(GTK_WIDGET(w)))
        gtk_im_context_set_client_widget(im, GTK_WIDGET(w));
}

void gr_gtk_widget_set_im_context(GtkWidget *widget, GtkIMContext *im)
{
    GrGraphiteWidget *w = (GrGraphiteWidget *)widget;
    use_im_context(w, im);
    update_im_focus(w);
}

static gboolean key(GrGraphiteWidget *w, GdkEvent *event, bool down)
{
    guint keyval = gdk_key_event_get_keyval(event), keycode = gdk_key_event_get_keycode(event);
    bool repeat = false;
    if (keycode < 256)
    {
        guint64 bit = (guint64)1 << (keycode % 64), *word = &w->keys_down[keycode / 64];
        repeat = down && (*word & bit);
        *word = down ? *word | bit : *word & ~bit;
    }
    gr_key named = gr_gtk_named_key(keyval);
    uint32_t character = 0;
    if (named == GR_KEY_UNKNOWN)
    {
        /* The text the key types on its layout without modifiers, for shortcuts. */
        guint plain = 0;
        if (!gdk_display_translate_key(gdk_event_get_display(event), keycode, 0, gdk_key_event_get_layout(event),
                                       &plain, NULL, NULL, NULL))
            plain = keyval;
        character = gdk_keyval_to_unicode(gdk_keyval_to_lower(plain));
        if (character < 0x20 || character == 0x7F)
            character = 0;
        if (character)
            named = GR_KEY_CHARACTER;
    }
    gr_key_code code = gr_gtk_key_code(keycode);
    gr_event e = {.type = down ? GR_EVENT_KEY_DOWN : GR_EVENT_KEY_UP,
                  .time = event_seconds(event),
                  .modifiers = modifiers_after(w, modifiers(gdk_event_get_modifier_state(event)), code)};
    bool held = composing(w); /* The input method owns keys while composing. */
    e.value.key = (gr_key_event){
        .code = code,
        .key = named,
        .character = character,
        .repeat = repeat,
        .composing = held,
        .native_code = keycode,
    };
    if (dispatch(w, e) && !held)
        return GDK_EVENT_STOP;
    /* Keys the handler leaves go to text input: the input method first, then the key bindings. */
    if (!w->text_on)
        return GDK_EVENT_PROPAGATE;
    if (gtk_im_context_filter_keypress(w->im, event))
        return GDK_EVENT_STOP;
    gr_text_command_event command;
    if (!down || held || !w->text_on || !key_command(keyval, gdk_event_get_modifier_state(event), &command))
        return GDK_EVENT_PROPAGATE;
    gr_event c = {.type = GR_EVENT_TEXT_COMMAND, .time = event_seconds(event), .modifiers = e.modifiers};
    c.value.command = command;
    return dispatch(w, c) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

static void cancel_pointer(GrGraphiteWidget *w, guint32 time)
{
    if (!w->buttons)
        return;
    w->buttons = 0;
    gr_event e = {.type = GR_EVENT_POINTER_CANCEL, .time = time / 1000.0};
    e.value.pointer.kind = GR_POINTER_MOUSE;
    dispatch(w, e);
}

static gboolean on_event(GtkEventControllerLegacy *controller, GdkEvent *event, gpointer userdata)
{
    (void)controller;
    GrGraphiteWidget *w = userdata;
    if (!w->host)
        return GDK_EVENT_PROPAGATE;
    switch (gdk_event_get_event_type(event))
    {
        case GDK_BUTTON_PRESS:
            return press(w, event, true);
        case GDK_BUTTON_RELEASE:
            return press(w, event, false);
        case GDK_MOTION_NOTIFY:
            return motion(w, event);
        case GDK_SCROLL:
            return scroll(w, event);
        case GDK_KEY_PRESS:
            return key(w, event, true);
        case GDK_KEY_RELEASE:
            return key(w, event, false);
        case GDK_GRAB_BROKEN:
            cancel_pointer(w, gdk_event_get_time(event));
            return GDK_EVENT_PROPAGATE;
        default:
            return GDK_EVENT_PROPAGATE;
    }
}

/* Crossings come from a motion controller, which reports them in widget coordinates. */
static void pointer_enter(GtkEventControllerMotion *controller, double x, double y, gpointer userdata)
{
    (void)controller;
    GrGraphiteWidget *w = userdata;
    if (!w->buttons)
        hover(w, true, (gr_point){x, y}, GDK_CURRENT_TIME);
}

static void pointer_leave(GtkEventControllerMotion *controller, gpointer userdata)
{
    (void)controller;
    GrGraphiteWidget *w = userdata;
    if (!w->buttons)
        hover(w, false, (gr_point){-1, -1}, GDK_CURRENT_TIME);
}

void gr_gtk_widget_set_cursor(GtkWidget *widget, gr_cursor cursor)
{
    static const char *const names[] = {
        [GR_CURSOR_DEFAULT] = NULL,
        [GR_CURSOR_POINTER] = "pointer",
        [GR_CURSOR_TEXT] = "text",
        [GR_CURSOR_CROSSHAIR] = "crosshair",
        [GR_CURSOR_GRAB] = "grab",
        [GR_CURSOR_GRABBING] = "grabbing",
        [GR_CURSOR_RESIZE_EW] = "ew-resize",
        [GR_CURSOR_RESIZE_NS] = "ns-resize",
        [GR_CURSOR_RESIZE_NESW] = "nesw-resize",
        [GR_CURSOR_RESIZE_NWSE] = "nwse-resize",
        [GR_CURSOR_NOT_ALLOWED] = "not-allowed",
        [GR_CURSOR_NONE] = "none",
    };
    gtk_widget_set_cursor_from_name(widget, names[cursor]);
}

static void commit_pending(GdkFrameClock *clock, gpointer userdata)
{
    (void)clock;
    GrGraphiteWidget *w = userdata;
    gr_gtk_shared *s = w->shared;
    if (!s || !s->pending)
        return;
    s->pending = false;
    gr_scene *scene = gr_scene_link_lock(s->scene);
    if (scene)
    {
        gr_scene_commit(scene);
        gr_scene_unref(scene);
    }
}

static void sync_scale(GrGraphiteWidget *w)
{
    if (!w->surface || !w->shared)
        return;
    gr_scene *scene = gr_scene_link_lock(w->shared->scene);
    if (scene)
    {
        gr_scene_set_backing_scale(scene, gdk_surface_get_scale(w->surface));
        gr_scene_unref(scene);
    }
}

static void scale_changed(GObject *object, GParamSpec *spec, gpointer userdata)
{
    (void)object;
    (void)spec;
    sync_scale(userdata);
    report_viewport(userdata);
}

static void disconnect_signals(GrGraphiteWidget *w)
{
    if (w->layout_handler)
    {
        g_signal_handler_disconnect(w->clock, w->layout_handler);
        w->layout_handler = 0;
    }
    if (w->update_handler)
    {
        g_signal_handler_disconnect(w->clock, w->update_handler);
        w->update_handler = 0;
    }
    if (w->active_handler)
    {
        g_signal_handler_disconnect(w->window, w->active_handler);
        w->active_handler = 0;
    }
    g_clear_object(&w->window);
    if (w->scale_handler)
    {
        g_signal_handler_disconnect(w->surface, w->scale_handler);
        w->scale_handler = 0;
    }
    g_clear_object(&w->clock);
    g_clear_object(&w->surface);
}

static void realize(GtkWidget *widget)
{
    GTK_WIDGET_CLASS(gr_graphite_widget_parent_class)->realize(widget);
    GrGraphiteWidget *w = (GrGraphiteWidget *)widget;
    GdkFrameClock *clock = gtk_widget_get_frame_clock(widget);
    if (clock)
    {
        w->clock = g_object_ref(clock);
        w->layout_handler = g_signal_connect(clock, "layout", G_CALLBACK(commit_pending), w);
        w->update_handler = g_signal_connect(clock, "update", G_CALLBACK(update_frame), w);
    }
    GtkRoot *root = gtk_widget_get_root(widget);
    if (GTK_IS_WINDOW(root))
    {
        w->window = g_object_ref(GTK_WINDOW(root));
        w->active_handler = g_signal_connect(root, "notify::is-active", G_CALLBACK(window_active_changed), w);
    }
    GtkNative *native = gtk_widget_get_native(widget);
    GdkSurface *surface = native ? gtk_native_get_surface(native) : NULL;
    if (surface)
    {
        w->surface = g_object_ref(surface);
        w->scale_handler = g_signal_connect(surface, "notify::scale", G_CALLBACK(scale_changed), w);
    }
    gtk_im_context_set_client_widget(w->im, widget);
    sync_scale(w);
    if (w->shared && w->shared->pending)
        gr_gtk_widget_request(widget);
    if (w->host && gr_scene_host_frame_requested(w->host))
        gr_gtk_widget_request_frame(widget);
}

static void unrealize(GtkWidget *widget)
{
    GrGraphiteWidget *w = (GrGraphiteWidget *)widget;
    disconnect_signals(w);
    cancel_pointer(w, GDK_CURRENT_TIME);
    w->inside = w->scrolling = false;
    memset(w->keys_down, 0, sizeof(w->keys_down));
    dispatch(w, (gr_event){.type = GR_EVENT_FOCUS_OUT, .time = seconds(g_get_monotonic_time())});
    update_im_focus(w);
    gtk_im_context_set_client_widget(w->im, NULL);
    GTK_WIDGET_CLASS(gr_graphite_widget_parent_class)->unrealize(widget);
}

static void size_allocate(GtkWidget *widget, int width, int height, int baseline)
{
    GtkWidgetClass *parent = GTK_WIDGET_CLASS(gr_graphite_widget_parent_class);
    if (parent->size_allocate)
        parent->size_allocate(widget, width, height, baseline);
    report_viewport((GrGraphiteWidget *)widget);
}

static void measure(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural,
                    int *minimum_baseline, int *natural_baseline)
{
    (void)for_size;
    GrGraphiteWidget *w = (GrGraphiteWidget *)widget;
    gr_size size = {0};
    if (w->shared)
        gr_gtk_tree_size(w->shared, &size);
    double n = ceil(orientation == GTK_ORIENTATION_HORIZONTAL ? size.width : size.height);
    *minimum = 0;
    *natural = (int)fmin(n, G_MAXINT);
    *minimum_baseline = *natural_baseline = -1;
}

static void snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    GrGraphiteWidget *w = (GrGraphiteWidget *)widget;
    if (!w->shared)
        return;
    GskRenderNode *node = gr_gtk_tree_root(w->shared);
    if (node)
    {
        gtk_snapshot_append_node(snapshot, node);
        gsk_render_node_unref(node);
    }
}

static void dispose(GObject *object)
{
    GrGraphiteWidget *w = (GrGraphiteWidget *)object;
    disconnect_signals(w);
    g_clear_handle_id(&w->timer, g_source_remove);
    use_im_context(w, NULL);
    store_text_input(w, NULL);
    if (w->shared)
    {
        g_weak_ref_set(&w->shared->widget, NULL);
        gr_gtk_shared_unref(w->shared);
        w->shared = NULL;
    }
    G_OBJECT_CLASS(gr_graphite_widget_parent_class)->dispose(object);
}

static void gr_graphite_widget_class_init(GrGraphiteWidgetClass *klass)
{
    GtkWidgetClass *widget = GTK_WIDGET_CLASS(klass);
    widget->realize = realize;
    widget->unrealize = unrealize;
    widget->measure = measure;
    widget->size_allocate = size_allocate;
    widget->snapshot = snapshot;
    G_OBJECT_CLASS(klass)->dispose = dispose;
}

static void gr_graphite_widget_init(GrGraphiteWidget *widget)
{
    gtk_widget_set_overflow(GTK_WIDGET(widget), GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_focusable(GTK_WIDGET(widget), TRUE);
    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(focus_changed), widget);
    g_signal_connect(focus, "leave", G_CALLBACK(focus_changed), widget);
    gtk_widget_add_controller(GTK_WIDGET(widget), focus);
    GtkEventController *events = gtk_event_controller_legacy_new();
    g_signal_connect(events, "event", G_CALLBACK(on_event), widget);
    gtk_widget_add_controller(GTK_WIDGET(widget), events);
    GtkEventController *crossing = gtk_event_controller_motion_new();
    g_signal_connect(crossing, "enter", G_CALLBACK(pointer_enter), widget);
    g_signal_connect(crossing, "leave", G_CALLBACK(pointer_leave), widget);
    gtk_widget_add_controller(GTK_WIDGET(widget), crossing);
    GtkIMContext *im = gtk_im_multicontext_new();
    use_im_context(widget, im);
    g_object_unref(im);
}

GtkWidget *gr_gtk_widget_new(gr_gtk_shared *shared)
{
    GrGraphiteWidget *widget = g_object_new(gr_graphite_widget_get_type(), NULL);
    widget->shared = gr_gtk_shared_ref(shared);
    return GTK_WIDGET(widget);
}
