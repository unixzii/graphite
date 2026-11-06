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
        gr_macos_host *host = gr_macos_offscreen_new(scene, (gr_size){10, 10}, 1);
        CHECK(host);
        CHECK(!gr_macos_host_render(host, (gr_size){NAN, 10}, 1, &pixels));
        CHECK(!gr_macos_host_render(host, (gr_size){-1, 10}, 1, &pixels));
        CHECK(!gr_macos_host_render(host, (gr_size){10, 10}, NAN, &pixels));
        CHECK(!pixels.data);
        gr_macos_host_unref(host);
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
        gr_macos_host *h = gr_macos_attach_to_view(scene, (__bridge void *)window.contentView);
        CHECK(h);
        [window orderFrontRegardless];
        OK(gr_scene_commit(scene));
        spin();
        CALayer *flipped = (__bridge CALayer *)gr_macos_host_get_layer(h), *parent = flipped.superlayer;
        CHECK(flipped.geometryFlipped && !parent.geometryFlipped);
        CALayer *outer = flipped.sublayers[0].sublayers[0].sublayers[0];
        CGRect inHost = [outer convertRect:outer.bounds toLayer:parent];
        NEAR(inHost.origin.x, 0);
        NEAR(inHost.origin.y, parent.bounds.size.height - 10);
        [window setContentSize:NSMakeSize(300, 160)];
        spin();
        CHECK(CGSizeEqualToSize(flipped.bounds.size, parent.bounds.size));
        NEAR(parent.bounds.size.height, 160);
        NEAR(gr_scene_get_backing_scale(scene), window.backingScaleFactor);
        NSView *view = (__bridge NSView *)gr_macos_host_get_view(h);
        gr_macos_host_unref(h);
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
        gr_macos_host *host = gr_macos_offscreen_new(scene, (gr_size){100, 100}, 1);
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
        gr_macos_host_unref(host);
    }
}

static void expired_scene(void)
{
    @autoreleasepool
    {
        gr_scene *scene = gr_scene_new((gr_size){10, 10});
        gr_macos_host *host = gr_macos_offscreen_new(scene, (gr_size){10, 10}, 1);
        CHECK(host);
        CHECK(!gr_macos_offscreen_new(scene, (gr_size){10, 10}, 1));
        OK(gr_scene_commit(scene));
        gr_scene_unref(scene);
        gr_macos_pixels pixels = {0};
        CHECK(!gr_macos_host_render(host, (gr_size){10, 10}, 1, &pixels));
        CHECK(pixels.data == NULL);
        gr_macos_host_unref(host);
        spin();
    }
}

int main(int argc, char **argv)
{
    @autoreleasepool
    {
        const gr_test_case cases[] = {GR_TEST_CASE(invalid_arguments),  GR_TEST_CASE(rows_are_top_down),
                                      GR_TEST_CASE(scaled_rendering),   GR_TEST_CASE(view_hosting),
                                      GR_TEST_CASE(graphite_offscreen), GR_TEST_CASE(expired_scene)};
        return gr_test_run("macos", cases, sizeof(cases) / sizeof(cases[0]), argc, argv);
    }
}
