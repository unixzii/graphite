#include "internal.h"

@implementation GRHost

- (void)dealloc
{
    gr_scene_link_unref(_sceneLink);
}

@end

@implementation GRSceneView

- (void)dealloc
{
    gr_scene_link_unref(_sceneLink);
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self syncBackingScale];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self syncBackingScale];
}

- (void)syncBackingScale
{
    NSWindow *window = self.window;
    if (!window)
        return;
    double scale = window.backingScaleFactor;
    self.layer.contentsScale = scale;
    self.hostLayer.contentsScale = scale;
    gr_scene *scene = gr_scene_link_lock(_sceneLink);
    if (scene)
    {
        gr_scene_set_backing_scale(scene, scale);
        gr_scene_unref(scene);
    }
}

@end

static CALayer *new_host_layer(void)
{
    CALayer *layer = [CALayer layer];
    layer.geometryFlipped = YES;
    layer.masksToBounds = YES;
    layer.anchorPoint = CGPointZero;
    layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    return layer;
}

static gr_macos_host *wrap_host(GRHost *object)
{
    gr_macos_host *host = calloc(1, sizeof(*host));
    if (!host)
        abort();
    host->refs = 1;
    host->object = (__bridge_retained void *)object;
    return host;
}

gr_macos_host *gr_macos_attach_to_layer(gr_scene *scene, void *container_pointer)
{
    if (!scene || !container_pointer)
        return NULL;
    CALayer *container = (__bridge CALayer *)container_pointer;
    if (!isfinite(container.contentsScale) || container.contentsScale <= 0)
        return NULL;
    CALayer *layer = new_host_layer();
    layer.frame = container.bounds;
    layer.contentsScale = container.contentsScale;
    if (!gr_macos_install(scene, layer))
        return NULL;
    [container addSublayer:layer];
    gr_scene_set_backing_scale(scene, container.contentsScale);
    GRHost *host = [GRHost new];
    host.layer = layer;
    host.sceneLink = gr_scene_get_link(scene);
    return wrap_host(host);
}

gr_macos_host *gr_macos_attach_to_view(gr_scene *scene, void *parent_pointer)
{
    if (!scene || !parent_pointer)
        return NULL;
    NSView *parent = (__bridge NSView *)parent_pointer;
    CALayer *layer = new_host_layer();
    GRSceneView *view = [[GRSceneView alloc] initWithFrame:parent.bounds];
    view.sceneLink = gr_scene_get_link(scene);
    view.hostLayer = layer;
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    CALayer *viewLayer = [CALayer layer];
    view.layer = viewLayer;
    view.wantsLayer = YES;
    layer.frame = viewLayer.bounds;
    [viewLayer addSublayer:layer];
    if (!gr_macos_install(scene, layer))
        return NULL;
    [parent addSubview:view];
    [view syncBackingScale];
    GRHost *host = [GRHost new];
    host.layer = layer;
    host.view = view;
    host.sceneLink = gr_scene_get_link(scene);
    return wrap_host(host);
}

gr_macos_host *gr_macos_offscreen_new(gr_scene *scene, gr_size size, double scale)
{
    if (!scene || !isfinite(size.width) || !isfinite(size.height) || size.width <= 0 || size.height <= 0 ||
        !isfinite(scale) || scale <= 0)
        return NULL;
    CALayer *container = [CALayer layer];
    container.anchorPoint = CGPointZero;
    container.frame = CGRectMake(0, 0, size.width, size.height);
    container.contentsScale = scale;
    gr_macos_host *host = gr_macos_attach_to_layer(scene, (__bridge void *)container);
    if (host)
        ((__bridge GRHost *)host->object).container = container;
    return host;
}

gr_macos_host *gr_macos_host_ref(gr_macos_host *host)
{
    if (host)
    {
        if (host->refs == SIZE_MAX)
            abort();
        ++host->refs;
    }
    return host;
}

void gr_macos_host_unref(gr_macos_host *host)
{
    if (!host || --host->refs)
        return;
    (void)CFBridgingRelease(host->object);
    free(host);
}

void *gr_macos_host_get_layer(const gr_macos_host *host)
{
    return (__bridge void *)((__bridge GRHost *)host->object).layer;
}

void *gr_macos_host_get_view(const gr_macos_host *host)
{
    return (__bridge void *)((__bridge GRHost *)host->object).view;
}

bool gr_macos_host_render(gr_macos_host *host, gr_size size, double scale, gr_macos_pixels *out)
{
    if (!out)
        return false;
    *out = (gr_macos_pixels){0};
    if (!host || !isfinite(size.width) || !isfinite(size.height) || size.width <= 0 || size.height <= 0 ||
        !isfinite(scale) || scale <= 0)
        return false;
    GRHost *object = (__bridge GRHost *)host->object;
    if (!object.container)
        return false;
    gr_scene *scene = gr_scene_link_lock(object.sceneLink);
    if (!scene)
        return false;
    if (gr_scene_set_backing_scale(scene, scale) != GR_OK)
    {
        gr_scene_unref(scene);
        return false;
    }
    object.container.bounds = CGRectMake(0, 0, size.width, size.height);
    object.container.contentsScale = scale;
    object.layer.frame = object.container.bounds;
    object.layer.contentsScale = scale;
    gr_result result = gr_scene_commit(scene);
    gr_scene_unref(scene);
    if (result != GR_OK)
        return false;
    return gr_macos_render_layer((__bridge void *)object.container, size, scale, out);
}
