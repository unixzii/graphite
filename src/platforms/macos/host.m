#include "internal.h"

@implementation GRHost
@end

@implementation GRSceneView
{
    CADisplayLink *_displayLink;
    bool _focused;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    gr_scene_link_unref(_sceneLink);
}

- (void)detachHost
{
    _host = NULL;
    _focused = false;
    // Invalidation removes the run-loop registration and releases the link's strong reference to this view.
    [_displayLink invalidate];
    _displayLink = nil;
}

- (void)dispatch:(gr_event)event
{
    if (_host)
        gr_scene_host_dispatch_event(_host, &event);
}

/* Frames */

- (void)requestFrame
{
    if (!_host)
        return;
    if (@available(macOS 14.0, *))
    {
        if (!_displayLink)
        {
            // The view's display link follows the display the view is on.
            _displayLink = [self displayLinkWithTarget:self selector:@selector(displayFrame:)];
            [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
        }
        _displayLink.paused = NO;
    }
}

/* Runs from the main run loop before the backend's commit at kCFRunLoopBeforeWaiting, so changes made for this
 * frame commit with it. Pauses once a frame passes without a new request. */
- (void)displayFrame:(CADisplayLink *)link API_AVAILABLE(macos(14.0))
{
    double interval = link.targetTimestamp - link.timestamp;
    [self dispatch:(gr_event){.type = GR_EVENT_FRAME,
                              .time = link.timestamp,
                              .value.frame = {.target_time = link.targetTimestamp, .interval = fmax(interval, 0)}}];
    if (!_host || !gr_scene_host_frame_requested(_host))
        link.paused = YES;
}

/* Viewport */

- (void)reportViewport
{
    NSSize size = self.bounds.size;
    [self dispatch:(gr_event){.type = GR_EVENT_VIEWPORT,
                              .time = CACurrentMediaTime(),
                              .value.viewport = {{size.width, size.height}, self.hostLayer.contentsScale}}];
}

- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    [self reportViewport];
}

/* Focus: the view has keyboard focus while it is the first responder of the key window. */

- (BOOL)acceptsFirstResponder
{
    return _host != NULL;
}

- (void)updateFocus:(bool)focused
{
    if (focused == _focused || !_host)
        return;
    _focused = focused;
    [self dispatch:(gr_event){.type = focused ? GR_EVENT_FOCUS_IN : GR_EVENT_FOCUS_OUT, .time = CACurrentMediaTime()}];
}

- (BOOL)becomeFirstResponder
{
    if (![super becomeFirstResponder])
        return NO;
    [self updateFocus:self.window.isKeyWindow];
    return YES;
}

- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    [self updateFocus:false];
    return YES;
}

- (void)windowKeyChanged:(NSNotification *)notification
{
    (void)notification;
    NSWindow *window = self.window;
    [self updateFocus:window.isKeyWindow && window.firstResponder == self];
}

- (void)viewWillMoveToWindow:(NSWindow *)window
{
    [super viewWillMoveToWindow:window];
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    [center removeObserver:self];
    if (window)
    {
        [center addObserver:self
                   selector:@selector(windowKeyChanged:)
                       name:NSWindowDidBecomeKeyNotification
                     object:window];
        [center addObserver:self
                   selector:@selector(windowKeyChanged:)
                       name:NSWindowDidResignKeyNotification
                     object:window];
    }
    if (window != self.window)
        [self updateFocus:false];
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
    [self reportViewport];
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

static void destroy_host(void *data)
{
    (void)CFBridgingRelease(data);
}

static const gr_scene_host_vtable host_vtable = {.destroy = destroy_host};

/* View hosts are interactive. */

static GRSceneView *host_view(void *data)
{
    return (GRSceneView *)((__bridge GRHost *)data).view;
}

static void destroy_view_host(void *data)
{
    [host_view(data) detachHost];
    destroy_host(data);
}

static void request_frame(void *data)
{
    [host_view(data) requestFrame];
}

static double now(void *data)
{
    (void)data;
    return CACurrentMediaTime();
}

/* Cursor and text input are accepted but not applied yet. */
static void set_cursor(void *data, gr_cursor cursor)
{
    (void)data;
    (void)cursor;
}

static void set_text_input(void *data, const gr_text_input *input)
{
    (void)data;
    (void)input;
}

static void request_focus(void *data)
{
    GRSceneView *view = host_view(data);
    [view.window makeFirstResponder:view];
}

static const gr_scene_host_vtable view_host_vtable = {
    destroy_view_host, request_frame, now, set_cursor, set_text_input, request_focus,
};

static gr_scene_host *wrap_host(gr_scene *scene, GRHost *object)
{
    const gr_scene_host_vtable *vtable = object.view ? &view_host_vtable : &host_vtable;
    return gr_scene_host_new(scene, vtable, (__bridge_retained void *)object);
}

static GRHost *host_object(const gr_scene_host *host)
{
    if (!host ||
        (gr_scene_host_get_vtable(host) != &host_vtable && gr_scene_host_get_vtable(host) != &view_host_vtable))
        return nil;
    return (__bridge GRHost *)gr_scene_host_get_data(host);
}

gr_scene_host *gr_macos_attach_to_layer(gr_scene *scene, void *container_pointer)
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
    return wrap_host(scene, host);
}

gr_scene_host *gr_macos_attach_to_view(gr_scene *scene, void *parent_pointer)
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
    gr_scene_host *result = wrap_host(scene, host);
    view.host = result;
    // The view reported its size before it had a host; record it now.
    [view reportViewport];
    return result;
}

gr_scene_host *gr_macos_offscreen_new(gr_scene *scene, gr_size size, double scale)
{
    if (!scene || !isfinite(size.width) || !isfinite(size.height) || size.width <= 0 || size.height <= 0 ||
        !isfinite(scale) || scale <= 0)
        return NULL;
    CALayer *container = [CALayer layer];
    container.anchorPoint = CGPointZero;
    container.frame = CGRectMake(0, 0, size.width, size.height);
    container.contentsScale = scale;
    gr_scene_host *host = gr_macos_attach_to_layer(scene, (__bridge void *)container);
    if (host)
        host_object(host).container = container;
    return host;
}

void *gr_macos_host_get_layer(const gr_scene_host *host)
{
    return (__bridge void *)host_object(host).layer;
}

void *gr_macos_host_get_view(const gr_scene_host *host)
{
    return (__bridge void *)host_object(host).view;
}

bool gr_macos_host_render(gr_scene_host *host, gr_size size, double scale, gr_macos_pixels *out)
{
    if (!out)
        return false;
    *out = (gr_macos_pixels){0};
    if (!host || !isfinite(size.width) || !isfinite(size.height) || size.width <= 0 || size.height <= 0 ||
        !isfinite(scale) || scale <= 0)
        return false;
    GRHost *object = host_object(host);
    if (!object.container)
        return false;
    gr_scene *scene = gr_scene_host_get_scene(host);
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
