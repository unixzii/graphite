#include "internal.h"

@implementation GRHost
@end

static gr_modifiers modifiers(NSEventModifierFlags flags)
{
    return (flags & NSEventModifierFlagShift ? GR_MODIFIER_SHIFT : 0) |
           (flags & NSEventModifierFlagControl ? GR_MODIFIER_CONTROL : 0) |
           (flags & NSEventModifierFlagOption ? GR_MODIFIER_ALT : 0) |
           (flags & NSEventModifierFlagCommand ? GR_MODIFIER_SUPER : 0) |
           (flags & NSEventModifierFlagCapsLock ? GR_MODIFIER_CAPS_LOCK : 0);
}

static gr_button event_button(NSEvent *event)
{
    switch (event.type)
    {
        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
            return GR_BUTTON_PRIMARY;
        case NSEventTypeRightMouseDown:
        case NSEventTypeRightMouseUp:
            return GR_BUTTON_SECONDARY;
        default:
            switch (event.buttonNumber)
            {
                case 2:
                    return GR_BUTTON_MIDDLE;
                case 3:
                    return GR_BUTTON_BACK;
                case 4:
                    return GR_BUTTON_FORWARD;
                default:
                    return GR_BUTTON_NONE;
            }
    }
}

static gr_scroll_phase scroll_phase(NSEventPhase phase)
{
    switch (phase)
    {
        case NSEventPhaseBegan:
            return GR_SCROLL_PHASE_BEGIN;
        case NSEventPhaseChanged:
        case NSEventPhaseStationary:
            return GR_SCROLL_PHASE_UPDATE;
        case NSEventPhaseEnded:
        case NSEventPhaseCancelled:
            return GR_SCROLL_PHASE_END;
        default:
            return GR_SCROLL_PHASE_NONE;
    }
}

static NSCursor *native_cursor(gr_cursor cursor)
{
    switch (cursor)
    {
        case GR_CURSOR_POINTER:
            return NSCursor.pointingHandCursor;
        case GR_CURSOR_TEXT:
            return NSCursor.IBeamCursor;
        case GR_CURSOR_CROSSHAIR:
            return NSCursor.crosshairCursor;
        case GR_CURSOR_GRAB:
            return NSCursor.openHandCursor;
        case GR_CURSOR_GRABBING:
            return NSCursor.closedHandCursor;
        case GR_CURSOR_RESIZE_EW:
            return NSCursor.resizeLeftRightCursor;
        case GR_CURSOR_RESIZE_NS:
            return NSCursor.resizeUpDownCursor;
        case GR_CURSOR_RESIZE_NESW:
        case GR_CURSOR_RESIZE_NWSE:
            if (@available(macOS 15.0, *))
            {
                NSCursorFrameResizePosition position = cursor == GR_CURSOR_RESIZE_NESW
                                                           ? NSCursorFrameResizePositionTopRight
                                                           : NSCursorFrameResizePositionTopLeft;
                return [NSCursor frameResizeCursorFromPosition:position inDirections:NSCursorFrameResizeDirectionsAll];
            }
            return NSCursor.crosshairCursor;
        case GR_CURSOR_NOT_ALLOWED:
            return NSCursor.operationNotAllowedCursor;
        case GR_CURSOR_NONE:
        {
            static NSCursor *none;
            if (!none)
                none = [[NSCursor alloc] initWithImage:[[NSImage alloc] initWithSize:NSMakeSize(1, 1)]
                                               hotSpot:NSZeroPoint];
            return none;
        }
        case GR_CURSOR_DEFAULT:
        default:
            return NSCursor.arrowCursor;
    }
}

/* Selector targets stay dynamic. */
@interface GRSceneView ()
- (void)displayFrame:(CADisplayLink *)link API_AVAILABLE(macos(14.0));
- (void)windowKeyChanged:(NSNotification *)notification;
@end

__attribute__((objc_direct_members))
@implementation GRSceneView
{
    CADisplayLink *_displayLink;
    bool _focused;
    bool _inside;      /* The pointer hovers over the view (as last reported). */
    uint32_t _buttons; /* Pressed buttons, 1u << gr_button. */
    NSTrackingArea *_tracking;
    NSCursor *_cursor;
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
    _inside = false;
    _buttons = 0;
    _cursor = nil;
    [self.window invalidateCursorRectsForView:self];
    // Invalidation removes the run-loop registration and releases the link's strong reference to this view.
    [_displayLink invalidate];
    _displayLink = nil;
}

- (bool)dispatch:(gr_event)event
{
    return _host && gr_scene_host_dispatch_event(_host, &event);
}

/* Pointer input. Clicks give the view keyboard focus; drags stay with the view until every button is released. */

- (gr_point)pointOf:(NSEvent *)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    return (gr_point){p.x, self.bounds.size.height - p.y};
}

- (gr_event)pointer:(gr_event_type)type event:(NSEvent *)event
{
    /* subtype and pressure are only valid for button and movement events, not crossings. */
    bool pen =
        NSEventMaskFromType(event.type) &
            (NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskRightMouseDown | NSEventMaskRightMouseUp |
             NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp | NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged |
             NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged) &&
        event.subtype == NSEventSubtypeTabletPoint;
    gr_event e = {.type = type, .time = event.timestamp, .modifiers = modifiers(event.modifierFlags)};
    e.value.pointer = (gr_pointer_event){
        .kind = pen ? GR_POINTER_PEN : GR_POINTER_MOUSE,
        .position = [self pointOf:event],
        .buttons = _buttons,
        .pressure = pen        ? (float)fmin(fmax(event.pressure, 0), 1)
                    : _buttons ? 0.5f
                               : 0,
    };
    return e;
}

- (void)hover:(bool)inside event:(NSEvent *)event
{
    if (inside == _inside || !_host)
        return;
    _inside = inside;
    if (inside)
        [_cursor set];
    [self dispatch:[self pointer:inside ? GR_EVENT_POINTER_ENTER : GR_EVENT_POINTER_LEAVE event:event]];
}

/* Returns whether a press or release was consumed; NO passes it on. */
- (bool)press:(NSEvent *)event down:(bool)down
{
    gr_button button = event_button(event);
    if (!_host || button == GR_BUTTON_NONE)
        return false;
    if (down)
    {
        [self.window makeFirstResponder:self];
        if (!_host)
            return false;
    }
    uint32_t bit = 1u << button;
    _buttons = down ? _buttons | bit : _buttons & ~bit;
    gr_event e = [self pointer:down ? GR_EVENT_POINTER_DOWN : GR_EVENT_POINTER_UP event:event];
    e.value.pointer.button = button;
    e.value.pointer.click_count = event.clickCount > 0 ? (uint32_t)event.clickCount : 0;
    bool handled = [self dispatch:e];
    /* Crossings are not reported during drags; catch up once the last button is released. */
    if (!_buttons)
        [self hover:NSPointInRect([self convertPoint:event.locationInWindow fromView:nil], self.bounds) event:event];
    return handled;
}

- (bool)move:(NSEvent *)event
{
    if (!_host)
        return false;
    if (!_buttons)
        [self hover:true event:event];
    return [self dispatch:[self pointer:GR_EVENT_POINTER_MOVE event:event]];
}

- (void)mouseDown:(NSEvent *)event
{
    if (![self press:event down:true])
        [super mouseDown:event];
}

- (void)mouseUp:(NSEvent *)event
{
    if (![self press:event down:false])
        [super mouseUp:event];
}

- (void)rightMouseDown:(NSEvent *)event
{
    if (![self press:event down:true])
        [super rightMouseDown:event];
}

- (void)rightMouseUp:(NSEvent *)event
{
    if (![self press:event down:false])
        [super rightMouseUp:event];
}

- (void)otherMouseDown:(NSEvent *)event
{
    if (![self press:event down:true])
        [super otherMouseDown:event];
}

- (void)otherMouseUp:(NSEvent *)event
{
    if (![self press:event down:false])
        [super otherMouseUp:event];
}

- (void)mouseMoved:(NSEvent *)event
{
    if (![self move:event])
        [super mouseMoved:event];
}

- (void)mouseDragged:(NSEvent *)event
{
    if (![self move:event])
        [super mouseDragged:event];
}

- (void)rightMouseDragged:(NSEvent *)event
{
    if (![self move:event])
        [super rightMouseDragged:event];
}

- (void)otherMouseDragged:(NSEvent *)event
{
    if (![self move:event])
        [super otherMouseDragged:event];
}

- (void)mouseEntered:(NSEvent *)event
{
    if (!_buttons)
        [self hover:true event:event];
}

- (void)mouseExited:(NSEvent *)event
{
    if (!_buttons)
        [self hover:false event:event];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_tracking)
        [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingCursorUpdate |
                     NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
               owner:self
            userInfo:nil];
    [self addTrackingArea:_tracking];
}

/* Wheel and touchpad scrolling. Deltas are negated into the "positive scrolls down" convention. */

- (void)scrollWheel:(NSEvent *)event
{
    bool momentum = event.momentumPhase != NSEventPhaseNone;
    if (!_host || (!momentum && event.phase == NSEventPhaseMayBegin))
    {
        [super scrollWheel:event];
        return;
    }
    gr_event e = {.type = GR_EVENT_SCROLL, .time = event.timestamp, .modifiers = modifiers(event.modifierFlags)};
    e.value.scroll = (gr_scroll_event){
        .position = [self pointOf:event],
        .delta = {-event.scrollingDeltaX, -event.scrollingDeltaY},
        .unit = event.hasPreciseScrollingDeltas ? GR_SCROLL_UNIT_POINT : GR_SCROLL_UNIT_LINE,
        .phase = scroll_phase(momentum ? event.momentumPhase : event.phase),
        .momentum = momentum,
    };
    if (![self dispatch:e])
        [super scrollWheel:event];
}

/* Keys */

- (bool)key:(NSEvent *)event down:(bool)down
{
    if (!_host)
        return false;
    unsigned short code = event.keyCode;
    gr_key key = gr_macos_named_key(code);
    uint32_t character = 0;
    if (key == GR_KEY_UNKNOWN && event.type != NSEventTypeFlagsChanged)
    {
        NSString *text = [event charactersByApplyingModifiers:0];
        UTF32Char c = 0;
        if (text.length && [text getBytes:&c
                                    maxLength:sizeof(c)
                                   usedLength:NULL
                                     encoding:NSUTF32LittleEndianStringEncoding
                                      options:0
                                        range:NSMakeRange(0, [text rangeOfComposedCharacterSequenceAtIndex:0].length)
                               remainingRange:NULL])
            c = CFSwapInt32LittleToHost(c);
        /* Skip control characters and AppKit's function-key private-use range. */
        if (c >= 0x20 && c != 0x7F && !(c >= 0xF700 && c <= 0xF8FF))
        {
            key = GR_KEY_CHARACTER;
            character = c;
        }
    }
    gr_event e = {.type = down ? GR_EVENT_KEY_DOWN : GR_EVENT_KEY_UP,
                  .time = event.timestamp,
                  .modifiers = modifiers(event.modifierFlags)};
    e.value.key = (gr_key_event){
        .code = gr_macos_key_code(code),
        .key = key,
        .character = character,
        .repeat = down && event.type == NSEventTypeKeyDown && event.ARepeat,
        .native_code = code,
    };
    return [self dispatch:e];
}

- (void)keyDown:(NSEvent *)event
{
    if (![self key:event down:true])
        [super keyDown:event];
}

- (void)keyUp:(NSEvent *)event
{
    if (![self key:event down:false])
        [super keyUp:event];
}

/* Modifier keys arrive as flag changes. Caps Lock reports only its toggles, so each one is a press and release. */
- (void)flagsChanged:(NSEvent *)event
{
    bool handled;
    if (event.keyCode == 0x39 /* kVK_CapsLock */)
    {
        bool pressed = [self key:event down:true];
        handled = [self key:event down:false] || pressed;
    }
    else
    {
        NSUInteger mask = gr_macos_modifier_mask(event.keyCode);
        handled = mask && [self key:event down:(event.modifierFlags & mask) != 0];
    }
    if (!handled)
        [super flagsChanged:event];
}

/* Cursor */

- (void)setCursorKind:(gr_cursor)cursor
{
    _cursor = cursor == GR_CURSOR_DEFAULT ? nil : native_cursor(cursor);
    if (_inside)
        [_cursor ? _cursor : NSCursor.arrowCursor set];
}

- (void)cursorUpdate:(NSEvent *)event
{
    if (_host && _cursor)
        [_cursor set];
    else
        [super cursorUpdate:event];
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

static void set_cursor(void *data, gr_cursor cursor)
{
    [host_view(data) setCursorKind:cursor];
}

/* Text input is accepted but not applied yet. */
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
