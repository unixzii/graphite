#include "internal.h"

typedef struct
{
    GtkWidget parent_instance;
    gr_gtk_shared *shared;
    GdkFrameClock *clock;
    GdkSurface *surface;
    gulong layout_handler, scale_handler;
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
}

static void disconnect_signals(GrGraphiteWidget *w)
{
    if (w->layout_handler)
    {
        g_signal_handler_disconnect(w->clock, w->layout_handler);
        w->layout_handler = 0;
    }
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
}

static void unrealize(GtkWidget *widget)
{
    disconnect_signals((GrGraphiteWidget *)widget);
    GTK_WIDGET_CLASS(gr_graphite_widget_parent_class)->unrealize(widget);
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
    widget->snapshot = snapshot;
    G_OBJECT_CLASS(klass)->dispose = dispose;
}

static void gr_graphite_widget_init(GrGraphiteWidget *widget)
{
    gtk_widget_set_overflow(GTK_WIDGET(widget), GTK_OVERFLOW_HIDDEN);
}

GtkWidget *gr_gtk_widget_new(gr_gtk_shared *shared)
{
    GrGraphiteWidget *widget = g_object_new(gr_graphite_widget_get_type(), NULL);
    widget->shared = gr_gtk_shared_ref(shared);
    return GTK_WIDGET(widget);
}
