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

static void dispatch(GrGraphiteWidget *w, gr_event event)
{
    if (w->host)
        gr_scene_host_dispatch_event(w->host, &event);
}

static double seconds(gint64 microseconds)
{
    return (double)microseconds / G_TIME_SPAN_SECOND;
}

void gr_gtk_widget_set_host(GtkWidget *widget, gr_scene_host *host)
{
    ((GrGraphiteWidget *)widget)->host = host;
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
static void update_focus(GrGraphiteWidget *w)
{
    bool focused = gtk_widget_has_focus(GTK_WIDGET(w)) && (!w->window || gtk_window_is_active(w->window));
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
    dispatch(w, (gr_event){.type = GR_EVENT_FOCUS_OUT, .time = seconds(g_get_monotonic_time())});
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
}

GtkWidget *gr_gtk_widget_new(gr_gtk_shared *shared)
{
    GrGraphiteWidget *widget = g_object_new(gr_graphite_widget_get_type(), NULL);
    widget->shared = gr_gtk_shared_ref(shared);
    return GTK_WIDGET(widget);
}
