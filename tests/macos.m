#include "internal.h"
#include "test.h"

static CALayer *red_square(void)
{
    CALayer *container = [CALayer layer];
    container.anchorPoint = CGPointZero;
    container.frame = CGRectMake(0, 0, 100, 100);
    CALayer *square = [CALayer layer];
    square.frame = CGRectMake(0, 0, 10, 10);
    CGColorRef color = CGColorCreateSRGB(1, 0, 0, 1);
    square.backgroundColor = color;
    CGColorRelease(color);
    [container addSublayer:square];
    return container;
}

static void pixel(gr_macos_pixels *p, size_t x, size_t y, bool red)
{
    CHECK(x < p->width && y < p->height);
    uint8_t *v = p->data + (y * p->width + x) * 4;
    CHECK(v[0] == (red ? 255 : 0) && v[1] == 0 && v[2] == 0 && v[3] == (red ? 255 : 0));
}

static void ignore_host(void *data)
{
    (void)data;
}

static void invalid_arguments(void)
{
    @autoreleasepool
    {
        CALayer *layer = [CALayer layer];
        gr_macos_pixels pixels = {0};
        CHECK(!gr_macos_render_layer((__bridge void *)layer, (gr_size){INFINITY, 1}, 1, &pixels));
        CHECK(!gr_macos_render_layer((__bridge void *)layer, (gr_size){1, 1}, NAN, &pixels));
        CHECK(!gr_macos_render_layer((__bridge void *)layer, (gr_size){ldexp(1, 62), 1}, 1, &pixels));
        CHECK(!gr_macos_render_layer((__bridge void *)layer, (gr_size){1, ldexp(1, 62)}, 1, &pixels));
        gr_scene *scene = gr_scene_new((gr_size){10, 10});
        gr_scene_host *host = gr_macos_offscreen_new(scene, (gr_size){10, 10}, 1);
        CHECK(host);
        CHECK(!gr_macos_host_render(host, (gr_size){NAN, 10}, 1, &pixels));
        CHECK(!gr_macos_host_render(host, (gr_size){-1, 10}, 1, &pixels));
        CHECK(!gr_macos_host_render(host, (gr_size){10, 10}, NAN, &pixels));
        CHECK(!pixels.data);
        gr_scene_host_unref(host);

        /* Hosts from other platforms are rejected. */
        static const gr_scene_host_vtable foreign = {.destroy = ignore_host};
        host = gr_scene_host_new(scene, &foreign, NULL);
        CHECK(!gr_macos_host_get_layer(host) && !gr_macos_host_get_view(host));
        CHECK(!gr_macos_host_render(host, (gr_size){10, 10}, 1, &pixels) && !pixels.data);
        CHECK(!gr_macos_host_get_layer(NULL));
        gr_scene_host_unref(host);
        gr_scene_unref(scene);
    }
}

static void rows_are_top_down(void)
{
    @autoreleasepool
    {
        CALayer *layer = red_square();
        gr_macos_pixels pixels = {0};
        CHECK(gr_macos_render_layer((__bridge void *)layer, (gr_size){100, 100}, 1, &pixels));
        pixel(&pixels, 5, 95, true);
        pixel(&pixels, 5, 5, false);
        gr_macos_pixels_clear(&pixels);
    }
}

static void scaled_rendering(void)
{
    @autoreleasepool
    {
        CALayer *layer = red_square();
        gr_macos_pixels pixels = {0};
        CHECK(gr_macos_render_layer((__bridge void *)layer, (gr_size){100, 100}, 2, &pixels));
        CHECK(pixels.width == 200 && pixels.height == 200);
        pixel(&pixels, 19, 180, true);
        pixel(&pixels, 21, 180, false);
        pixel(&pixels, 19, 178, false);
        gr_macos_pixels_clear(&pixels);
    }
}

static void spin(void)
{
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
}

static void view_hosting(void)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 200, 100)
                                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.releasedWhenClosed = NO;
        gr_scene *scene = gr_scene_new((gr_size){200, 100});
        gr_layer *child = gr_layer_new(scene, GR_LAYER_CONTAINER);
        OK(gr_layer_set_frame(child, (gr_rect){{0, 0}, {10, 10}}));
        OK(gr_layer_add_child(gr_scene_get_root(scene), child));
        gr_layer_unref(child);
        gr_scene_host *h = gr_macos_attach_to_view(scene, (__bridge void *)window.contentView);
        CHECK(h);
        [window orderFrontRegardless];
        OK(gr_scene_commit(scene));
        spin();
        CALayer *flipped = (__bridge CALayer *)gr_macos_host_get_layer(h), *parent = flipped.superlayer;
        CHECK(flipped.geometryFlipped && !parent.geometryFlipped);
        CALayer *outer = flipped.sublayers[0].sublayers[0].sublayers[0];
        CGRect inHost = [outer convertRect:outer.bounds toLayer:parent];
        CHECK_NEAR(inHost.origin.x, 0);
        CHECK_NEAR(inHost.origin.y, parent.bounds.size.height - 10);
        [window setContentSize:NSMakeSize(300, 160)];
        spin();
        CHECK(CGSizeEqualToSize(flipped.bounds.size, parent.bounds.size));
        CHECK_NEAR(parent.bounds.size.height, 160);
        CHECK_NEAR(gr_scene_get_backing_scale(scene), window.backingScaleFactor);
        NSView *view = (__bridge NSView *)gr_macos_host_get_view(h);
        gr_scene_host_unref(h);
        CHECK(view.superview == window.contentView);
        gr_scene_unref(scene);
        spin();
        CHECK(flipped.sublayers.count == 0);
        [window close];
    }
}

static void graphite_offscreen(void)
{
    @autoreleasepool
    {
        gr_scene *scene = gr_scene_new((gr_size){100, 100});
        gr_layer *layer = gr_layer_new(scene, GR_LAYER_CONTAINER);
        gr_color red = {1, 0, 0, 1};
        OK(gr_layer_set_frame(layer, (gr_rect){{0, 0}, {10, 10}}));
        OK(gr_layer_set_background(layer, &red));
        OK(gr_layer_add_child(gr_scene_get_root(scene), layer));
        gr_layer_unref(layer);
        gr_scene_host *host = gr_macos_offscreen_new(scene, (gr_size){100, 100}, 1);
        CHECK(host);
        gr_macos_pixels invalid = {0};
        CHECK(!gr_macos_host_render(host, (gr_size){-1, 100}, 1, &invalid));
        CHECK(!gr_macos_host_render(host, (gr_size){100, 100}, NAN, &invalid));
        CHECK(invalid.data == NULL);
        for (size_t i = 0; i < 2; ++i)
        {
            gr_macos_pixels pixels = {0};
            CHECK(gr_macos_host_render(host, (gr_size){100, 100}, 1, &pixels));
            pixel(&pixels, 5, 5, true);
            pixel(&pixels, 5, 95, false);
            gr_macos_pixels_clear(&pixels);
        }
        gr_scene_unref(scene);
        gr_scene_host_unref(host);
    }
}

static void expired_scene(void)
{
    @autoreleasepool
    {
        gr_scene *scene = gr_scene_new((gr_size){10, 10});
        gr_scene_host *host = gr_macos_offscreen_new(scene, (gr_size){10, 10}, 1);
        CHECK(host);
        CHECK(!gr_macos_offscreen_new(scene, (gr_size){10, 10}, 1));
        OK(gr_scene_commit(scene));
        gr_scene_unref(scene);
        gr_macos_pixels pixels = {0};
        CHECK(!gr_macos_host_render(host, (gr_size){10, 10}, 1, &pixels));
        CHECK(pixels.data == NULL);
        gr_scene_host_unref(host);
        spin();
    }
}

typedef struct
{
    gr_event events[64];
    size_t count;
} event_log;

static bool log_event(void *userdata, const gr_event *event)
{
    event_log *log = userdata;
    if (log->count < 64)
        log->events[log->count] = *event;
    ++log->count;
    return true;
}

static size_t count_events(const event_log *log, gr_event_type type)
{
    size_t n = 0;
    for (size_t i = 0; i < log->count && i < 64; ++i)
        n += log->events[i].type == type;
    return n;
}

static const gr_event *last_event(const event_log *log, gr_event_type type)
{
    for (size_t i = log->count < 64 ? log->count : 64; i-- > 0;)
        if (log->events[i].type == type)
            return &log->events[i];
    return NULL;
}

static void spin_until(const event_log *log, gr_event_type type, size_t count)
{
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:2];
    while (count_events(log, type) < count && [deadline timeIntervalSinceNow] > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
}

/* A window whose key status the test controls, since a test process is usually not the active application. */
@interface GRTestWindow : NSWindow
@property(nonatomic) bool forceKey;
@end

@implementation GRTestWindow

- (BOOL)isKeyWindow
{
    return self.forceKey || [super isKeyWindow];
}

@end

static void set_key(GRTestWindow *window, bool key)
{
    window.forceKey = key;
    [NSNotificationCenter.defaultCenter
        postNotificationName:key ? NSWindowDidBecomeKeyNotification : NSWindowDidResignKeyNotification
                      object:window];
}

static void view_events(void)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        GRTestWindow *window =
            [[GRTestWindow alloc] initWithContentRect:NSMakeRect(100, 100, 200, 100)
                                            styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
        window.releasedWhenClosed = NO;
        gr_scene *scene = gr_scene_new((gr_size){200, 100});

        /* Layer and offscreen hosts have no input surface. */
        gr_scene_host *offscreen = gr_macos_offscreen_new(scene, (gr_size){10, 10}, 1);
        CHECK(offscreen && !gr_scene_host_is_interactive(offscreen));
        CHECK(gr_scene_host_request_frame(offscreen) == GR_ERROR_UNSUPPORTED);
        gr_scene_host_unref(offscreen);
        gr_scene_unref(scene);
        scene = gr_scene_new((gr_size){200, 100});

        gr_scene_host *host = gr_macos_attach_to_view(scene, (__bridge void *)window.contentView);
        CHECK(host && gr_scene_host_is_interactive(host));
        GRSceneView *view = (__bridge GRSceneView *)gr_macos_host_get_view(host);
        CHECK(view.acceptsFirstResponder);

        /* The size at attachment is recorded before any handler exists. */
        gr_viewport_event viewport;
        CHECK(gr_scene_host_get_viewport(host, &viewport));
        CHECK_NEAR(viewport.size.width, 200);
        CHECK_NEAR(viewport.size.height, 100);

        event_log log = {0};
        gr_event_handler handler = {&log, log_event, NULL};
        OK(gr_scene_host_set_event_handler(host, &handler));
        [window orderFrontRegardless];
        spin();
        CHECK_NEAR(gr_scene_get_backing_scale(scene), window.backingScaleFactor);
        CHECK(gr_scene_host_get_viewport(host, &viewport) && viewport.scale == window.backingScaleFactor);

        [window setContentSize:NSMakeSize(300, 160)];
        const gr_event *resized = last_event(&log, GR_EVENT_VIEWPORT);
        CHECK(resized);
        CHECK_NEAR(resized->value.viewport.size.width, 300);
        CHECK_NEAR(resized->value.viewport.size.height, 160);

        /* One request gives exactly one frame, before the next commit point, in the frame clock's domain. */
        double before = gr_scene_host_get_time(host);
        OK(gr_scene_host_request_frame(host));
        spin_until(&log, GR_EVENT_FRAME, 1);
        CHECK(count_events(&log, GR_EVENT_FRAME) == 1);
        const gr_event *frame = last_event(&log, GR_EVENT_FRAME);
        CHECK(frame->time >= before - 0.1 && frame->time <= gr_scene_host_get_time(host));
        CHECK(frame->value.frame.interval > 0 && frame->value.frame.target_time > frame->time);
        spin();
        CHECK(count_events(&log, GR_EVENT_FRAME) == 1);
        OK(gr_scene_host_request_frame(host));
        spin_until(&log, GR_EVENT_FRAME, 2);
        CHECK(count_events(&log, GR_EVENT_FRAME) == 2);

        /* Focus follows first responder status in the key window. */
        CHECK(!window.isKeyWindow);
        OK(gr_scene_host_request_focus(host));
        CHECK(window.firstResponder == view && !gr_scene_host_has_focus(host));
        set_key(window, true);
        CHECK(gr_scene_host_has_focus(host) && last_event(&log, GR_EVENT_FOCUS_IN));
        set_key(window, false);
        CHECK(!gr_scene_host_has_focus(host) && last_event(&log, GR_EVENT_FOCUS_OUT));
        set_key(window, true);
        CHECK(gr_scene_host_has_focus(host));
        [window makeFirstResponder:nil];
        CHECK(!gr_scene_host_has_focus(host));
        OK(gr_scene_host_request_focus(host));
        CHECK(gr_scene_host_has_focus(host));
        set_key(window, false);

        /* A request outstanding at release stops with the host. */
        OK(gr_scene_host_request_frame(host));
        size_t frames = count_events(&log, GR_EVENT_FRAME);
        gr_scene_host_unref(host);
        CHECK(!view.acceptsFirstResponder);
        spin();
        CHECK(count_events(&log, GR_EVENT_FRAME) == frames);
        gr_scene_unref(scene);
        [window close];
    }
}

int main(int argc, char **argv)
{
    @autoreleasepool
    {
        const gr_test_case cases[] = {GR_TEST_CASE(invalid_arguments),  GR_TEST_CASE(rows_are_top_down),
                                      GR_TEST_CASE(scaled_rendering),   GR_TEST_CASE(view_hosting),
                                      GR_TEST_CASE(graphite_offscreen), GR_TEST_CASE(expired_scene),
                                      GR_TEST_CASE(view_events)};
        return gr_test_run("macos", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
    }
}
