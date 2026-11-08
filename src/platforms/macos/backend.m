#include "internal.h"

#import <objc/message.h>

static NSValue *key(gr_layer *layer)
{
    return [NSValue valueWithPointer:layer];
}

@interface GRDrawingDelegate : NSObject <CALayerDelegate>
@property(nonatomic) gr_drawer_provider *provider;
@property(nonatomic) gr_layer *identity;
@property(nonatomic) uint64_t version;
@end

@implementation GRDrawingDelegate

- (void)dealloc
{
    gr_drawer_provider_unref(_provider);
}

- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)context
{
    gr_draw_info info = {gr_from_cg_rect(layer.bounds), layer.contentsScale};
    gr_display_list *list = NULL;
    if (gr_drawer_provider_draw(_provider, _identity, _version, &info, &list) != GR_OK)
        return;
    CGContextSaveGState(context);
    if (CGContextGetCTM(context).d > 0)
    {
        CGContextTranslateCTM(context, 0, info.bounds.origin.y * 2 + info.bounds.size.height);
        CGContextScaleCTM(context, 1, -1);
    }
    gr_cg_replay(context, list);
    CGContextRestoreGState(context);
    gr_display_list_unref(list);
}

@end

@interface GRNode : NSObject
@property(nonatomic, strong) CALayer *outer;
@property(nonatomic, strong) CALayer *body;
@property(nonatomic, strong) CALayer *imageLayer;
@property(nonatomic) uint64_t imageID;
@property(nonatomic, strong) GRDrawingDelegate *drawing;
@property(nonatomic) bool hasDisplayKey;
@property(nonatomic) uint64_t version;
@property(nonatomic) CGRect displayBounds;
@property(nonatomic) double displayScale;
@property(nonatomic) gr_layer *parent;
@property(nonatomic, strong) NSMutableArray<NSValue *> *children;
@property(nonatomic) double blur;
@end
@implementation GRNode
@end

@interface GRImage : NSObject
@property(nonatomic) CGImageRef image;
@property(nonatomic) size_t uses;
@end
@implementation GRImage

- (void)dealloc
{
    if (_image)
        CGImageRelease(_image);
}

@end

@interface GRBackend : NSObject
@property(nonatomic, strong) CALayer *host;
@property(nonatomic, strong) NSMutableDictionary<NSValue *, GRNode *> *nodes;
@property(nonatomic, strong) NSMutableDictionary<NSNumber *, GRImage *> *images;
@property(nonatomic) gr_scene_link *scene;
@property(nonatomic) gr_drawer_provider *drawers;
@property(nonatomic) double scale;
@property(nonatomic) gr_layer *root;
@property(nonatomic) CFRunLoopObserverRef observer;
@property(nonatomic) CFRunLoopRef runLoop;
- (void)shutdown;
- (void)request;
- (void)apply:(const gr_commit_batch *)batch;
@end

/* Shape layers take solid paints only. */
static gr_color paint_color(gr_paint *paint)
{
    return gr_paint_get_desc(paint).value.solid;
}

static void apply_shape(CAShapeLayer *layer, const gr_shape_props *s)
{
    CGPathRef path = s->path ? gr_cg_path(s->path) : NULL;
    layer.path = path;
    if (path)
        CGPathRelease(path);
    CGColorRef fill = s->fill ? gr_cg_color(paint_color(s->fill)) : NULL;
    layer.fillColor = fill;
    if (fill)
        CGColorRelease(fill);
    layer.fillRule = s->fill_rule == GR_FILL_EVEN_ODD ? kCAFillRuleEvenOdd : kCAFillRuleNonZero;
    if (s->stroke)
    {
        gr_stroke_style st = gr_stroke_get_style(s->stroke);
        CGColorRef color = gr_cg_color(paint_color(gr_stroke_get_paint(s->stroke)));
        layer.strokeColor = color;
        CGColorRelease(color);
        layer.lineWidth = st.width;
        layer.miterLimit = st.miter_limit;
        layer.lineCap = st.cap == GR_CAP_ROUND    ? kCALineCapRound
                        : st.cap == GR_CAP_SQUARE ? kCALineCapSquare
                                                  : kCALineCapButt;
        layer.lineJoin = st.join == GR_JOIN_ROUND   ? kCALineJoinRound
                         : st.join == GR_JOIN_BEVEL ? kCALineJoinBevel
                                                    : kCALineJoinMiter;
        if (st.dash.count)
        {
            NSMutableArray *dash = [NSMutableArray arrayWithCapacity:st.dash.count];
            for (size_t i = 0; i < st.dash.count; ++i)
                [dash addObject:@(st.dash.data[i])];
            layer.lineDashPattern = dash;
        }
        else
            layer.lineDashPattern = nil;
        layer.lineDashPhase = st.dash_phase;
    }
    else
    {
        layer.strokeColor = NULL;
        layer.lineWidth = 0;
    }
    layer.strokeStart = s->stroke_start;
    layer.strokeEnd = s->stroke_end;
}

static void apply_gradient(CAGradientLayer *layer, gr_paint *paint)
{
    if (!paint)
    {
        layer.colors = nil;
        return;
    }
    gr_paint_desc d = gr_paint_get_desc(paint);
    gr_gradient_stop_list stops;
    if (d.kind == GR_PAINT_LINEAR)
    {
        layer.type = kCAGradientLayerAxial;
        layer.startPoint = gr_cg_point(d.value.linear.start);
        layer.endPoint = gr_cg_point(d.value.linear.end);
        stops = d.value.linear.stops;
    }
    else
    {
        layer.type = kCAGradientLayerRadial;
        layer.startPoint = gr_cg_point(d.value.radial.center);
        layer.endPoint = CGPointMake(d.value.radial.center.x + d.value.radial.radius,
                                     d.value.radial.center.y + d.value.radial.radius);
        stops = d.value.radial.stops;
    }
    NSMutableArray *colors = [NSMutableArray arrayWithCapacity:stops.count],
                   *locations = [NSMutableArray arrayWithCapacity:stops.count];
    for (size_t i = 0; i < stops.count; ++i)
    {
        CGColorRef color = gr_cg_color(stops.data[i].color);
        [colors addObject:(__bridge id)color];
        CGColorRelease(color);
        [locations addObject:@(stops.data[i].offset)];
    }
    layer.colors = colors;
    layer.locations = locations;
}

/* Private CAFilter, the filter behind NSVisualEffectView; inputRadius is the Gaussian sigma in layer space. */
static NSArray *blur_filters(double sigma)
{
    if (!(sigma > 0))
        return nil;
    Class type = NSClassFromString(@"CAFilter");
    SEL make = NSSelectorFromString(@"filterWithType:");
    if (![type respondsToSelector:make])
        return nil;
    id filter = ((id(*)(id, SEL, NSString *))objc_msgSend)(type, make, @"gaussianBlur");
    [filter setValue:@"gaussianBlur" forKey:@"name"];
    [filter setValue:@(sigma) forKey:@"inputRadius"];
    return filter ? @[ filter ] : nil;
}

/* The mask layer shares the body's local coordinates and covers its box. */
static void apply_mask(CALayer *body, const gr_layer_props *p, CGRect local, double scale)
{
    if (!p->has_mask)
    {
        body.mask = nil;
        return;
    }
    const gr_mask *m = &p->mask;
    Class type = m->kind == GR_MASK_PATH ? [CAShapeLayer class] : [CAGradientLayer class];
    CALayer *mask = body.mask;
    if (![mask isMemberOfClass:type])
    {
        mask = [type layer];
        mask.anchorPoint = CGPointZero;
        body.mask = mask;
    }
    mask.bounds = local;
    mask.position = local.origin;
    mask.contentsScale = scale;
    if (m->kind == GR_MASK_PATH)
    {
        CAShapeLayer *shape = (CAShapeLayer *)mask;
        CGPathRef path = gr_cg_path(m->value.path.path);
        shape.path = path;
        CGPathRelease(path);
        shape.fillColor = CGColorGetConstantColor(kCGColorBlack);
        shape.fillRule = m->value.path.rule == GR_FILL_EVEN_ODD ? kCAFillRuleEvenOdd : kCAFillRuleNonZero;
    }
    else
        apply_gradient((CAGradientLayer *)mask, m->value.gradient);
}

@implementation GRBackend

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        _nodes = [NSMutableDictionary dictionary];
        _images = [NSMutableDictionary dictionary];
        _scale = 1;
    }
    return self;
}

- (void)shutdown
{
    if (_observer)
    {
        CFRunLoopObserverInvalidate(_observer);
        CFRelease(_observer);
        _observer = NULL;
    }
    for (GRNode *node in _nodes.allValues)
    {
        node.body.delegate = nil;
        [node.outer removeFromSuperlayer];
    }
    [_nodes removeAllObjects];
    [_images removeAllObjects];
}

- (void)dealloc
{
    [self shutdown];
    if (_runLoop)
        CFRelease(_runLoop);
    gr_drawer_provider_unref(_drawers);
    gr_scene_link_unref(_scene);
}

- (void)request
{
    if (_observer || !_runLoop)
        return;
    __weak GRBackend *weakSelf = self;
    _observer = CFRunLoopObserverCreateWithHandler(NULL, kCFRunLoopBeforeWaiting, false, 1999000,
                                                   ^(CFRunLoopObserverRef observer, CFRunLoopActivity activity) {
                                                     (void)observer;
                                                     (void)activity;
                                                     GRBackend *backend = weakSelf;
                                                     if (!backend)
                                                         return;
                                                     CFRelease(backend.observer);
                                                     backend.observer = NULL;
                                                     gr_scene *scene = gr_scene_link_lock(backend.scene);
                                                     if (scene)
                                                     {
                                                         gr_scene_commit(scene);
                                                         gr_scene_unref(scene);
                                                     }
                                                   });
    if (_observer)
    {
        CFRunLoopAddObserver(_runLoop, _observer, kCFRunLoopCommonModes);
        CFRunLoopWakeUp(_runLoop);
    }
}

- (GRNode *)node:(gr_layer *)identity
{
    GRNode *node = _nodes[key(identity)];
    if (!node)
        abort();
    return node;
}

/* Wraps the body in a new outer layer that takes over its place in the tree; update reapplies the outer properties. */
- (void)split:(GRNode *)node
{
    CALayer *body = node.body, *outer = [CALayer layer];
    outer.allowsGroupOpacity = YES;
    outer.allowsEdgeAntialiasing = YES;
    outer.contentsScale = _scale;
    outer.anchorPoint = CGPointZero;
    if (body.superlayer)
        [body.superlayer replaceSublayer:body with:outer];
    body.transform = CATransform3DIdentity;
    body.opacity = 1;
    body.hidden = NO;
    body.filters = nil;
    body.shadowOpacity = 0;
    body.shadowPath = NULL;
    [outer addSublayer:body];
    node.outer = outer;
    node.blur = 0;
}

- (void)syncChildren:(gr_layer *)identity
{
    GRNode *node = [self node:identity];
    NSMutableArray *layers = [NSMutableArray arrayWithCapacity:node.children.count + 1];
    if (node.imageLayer)
        [layers addObject:node.imageLayer];
    for (NSValue *child in node.children)
        [layers addObject:[self node:child.pointerValue].outer];
    node.body.sublayers = layers;
}

- (void)releaseImage:(uint64_t)identity
{
    GRImage *image = _images[@(identity)];
    if (image && --image.uses == 0)
        [_images removeObjectForKey:@(identity)];
}

- (CGImageRef)acquireImage:(gr_image *)image
{
    NSNumber *identity = @(gr_image_get_id(image));
    GRImage *entry = _images[identity];
    if (entry)
    {
        ++entry.uses;
        return entry.image;
    }
    CGImageRef cg = gr_cg_image(image);
    if (!cg)
        return NULL;
    entry = [GRImage new];
    entry.image = cg;
    entry.uses = 1;
    _images[identity] = entry;
    return cg;
}

- (void)content:(const gr_resolved_content *)content identity:(gr_layer *)identity
{
    GRNode *node = [self node:identity];
    gr_image *image = content->kind == GR_CONTENT_IMAGE ? content->value.image.image : NULL;
    uint64_t wanted = image ? gr_image_get_id(image) : 0;
    if (node.imageID != wanted)
    {
        if (node.imageID)
            [self releaseImage:node.imageID];
        CGImageRef cg = image ? [self acquireImage:image] : NULL;
        node.imageID = wanted;
        if (cg)
        {
            if (!node.imageLayer)
            {
                node.imageLayer = [CALayer layer];
                node.imageLayer.allowsEdgeAntialiasing = YES;
                node.imageLayer.contentsGravity = kCAGravityResize;
                [self syncChildren:identity];
            }
            node.imageLayer.contents = (__bridge id)cg;
        }
        else if (node.imageLayer)
        {
            node.imageLayer = nil;
            [self syncChildren:identity];
        }
    }
    if (image)
        node.imageLayer.contentsScale = gr_image_get_scale(image);
    node.body.contentsScale = _scale;
    if ([node.body isKindOfClass:[CAShapeLayer class]])
    {
        if (content->kind == GR_CONTENT_SHAPE)
            apply_shape((CAShapeLayer *)node.body, &content->value.shape);
    }
    else if ([node.body isKindOfClass:[CAGradientLayer class]])
    {
        apply_gradient((CAGradientLayer *)node.body,
                       content->kind == GR_CONTENT_GRADIENT ? content->value.gradient : NULL);
    }
    else if (content->kind == GR_CONTENT_DRAWN)
    {
        if (!node.drawing)
        {
            node.drawing = [GRDrawingDelegate new];
            node.drawing.provider = gr_drawer_provider_ref(_drawers);
            node.drawing.identity = identity;
            node.body.delegate = node.drawing;
            node.hasDisplayKey = false;
        }
        node.drawing.version = content->value.display_version;
    }
    else
    {
        node.body.delegate = nil;
        node.body.contents = nil;
        node.drawing = nil;
        node.hasDisplayKey = false;
    }
}

- (void)update:(const gr_layer_update *)update
{
    gr_layer *identity = update->layer;
    GRNode *node = [self node:identity];
    const gr_resolved *r = &update->resolved;
    const gr_layer_props *p = &r->props;
    gr_dirty_bits dirty = update->changed;
    CGRect local = gr_cg_rect(p->bounds);
    /* Clips and masks would clip the layer's own shadow and blur, so such a layer moves its shadow, transform,
     * opacity and blur to an outer layer, once and for good. */
    if ((p->has_shadow || p->blur_sigma > 0) && (p->masks_to_bounds || p->has_mask) && node.outer == node.body)
    {
        [self split:node];
        dirty |= GR_DIRTY_GEOMETRY | GR_DIRTY_APPEARANCE | GR_DIRTY_SHADOW;
    }
    bool split = node.outer != node.body;
    if (dirty & GR_DIRTY_GEOMETRY)
    {
        /* The resolved transform already places the box, so anchor points and positions stay zero. A single layer's
         * bounds start at the bounds origin, which the transform maps to the box's corner just the same. */
        CATransform3D transform = CATransform3DMakeAffineTransform(gr_cg_affine(r->transform));
        node.body.bounds = local;
        if (split)
            node.outer.bounds = CGRectMake(0, 0, local.size.width, local.size.height);
        node.outer.transform = transform;
    }
    if (dirty & GR_DIRTY_APPEARANCE)
    {
        node.outer.opacity = p->opacity;
        node.outer.hidden = p->hidden;
        node.body.masksToBounds = p->masks_to_bounds;
        node.body.cornerRadius = p->corner_radius;
        node.body.maskedCorners = gr_ca_corners(p->masked_corners);
        CGColorRef color = p->has_background ? gr_cg_color(p->background) : NULL;
        node.body.backgroundColor = color;
        if (color)
            CGColorRelease(color);
        color = p->has_border ? gr_cg_color(p->border.color) : NULL;
        node.body.borderColor = color;
        if (color)
            CGColorRelease(color);
        node.body.borderWidth = p->has_border ? p->border.width : 0;
        if (node.blur != p->blur_sigma)
        {
            node.blur = p->blur_sigma;
            node.outer.filters = blur_filters(p->blur_sigma);
        }
    }
    /* A single layer's shadow path is in local space, so it also follows the bounds origin. */
    if (dirty & (GR_DIRTY_SHADOW | (split ? 0 : GR_DIRTY_GEOMETRY)))
    {
        if (p->has_shadow)
        {
            gr_color color = p->shadow.color;
            color.a = 1;
            CGColorRef cg = gr_cg_color(color);
            node.outer.shadowColor = cg;
            CGColorRelease(cg);
            node.outer.shadowOpacity = p->shadow.color.a;
            node.outer.shadowOffset = CGSizeMake(p->shadow.offset.x, p->shadow.offset.y);
            node.outer.shadowRadius = p->shadow.blur_sigma;
            gr_path *outline = gr_resolved_new_shadow_path(r);
            CGPathRef path = outline ? gr_cg_path(outline) : NULL;
            if (path && !split)
            {
                CGAffineTransform shift = CGAffineTransformMakeTranslation(local.origin.x, local.origin.y);
                CGPathRef moved = CGPathCreateCopyByTransformingPath(path, &shift);
                CGPathRelease(path);
                path = moved;
            }
            node.outer.shadowPath = path;
            if (path)
                CGPathRelease(path);
            gr_path_unref(outline);
        }
        else
        {
            node.outer.shadowOpacity = 0;
            node.outer.shadowPath = NULL;
        }
    }
    if (dirty & (GR_DIRTY_GEOMETRY | GR_DIRTY_MASK))
        apply_mask(node.body, p, local, _scale);
    if (dirty & GR_DIRTY_CONTENT)
        [self content:&r->content identity:identity];
    if (dirty & (GR_DIRTY_GEOMETRY | GR_DIRTY_CONTENT))
    {
        if (r->content.kind == GR_CONTENT_IMAGE)
        {
            CGRect rect = gr_cg_rect(r->content.value.image.rect);
            rect.origin.x += local.origin.x;
            rect.origin.y += local.origin.y;
            node.imageLayer.frame = rect;
        }
        else if (r->content.kind == GR_CONTENT_DRAWN && node.drawing)
        {
            uint64_t version = r->content.value.display_version;
            node.drawing.version = version;
            if (!node.hasDisplayKey || node.version != version || !CGRectEqualToRect(node.displayBounds, local) ||
                node.displayScale != _scale)
            {
                node.hasDisplayKey = true;
                node.version = version;
                node.displayBounds = local;
                node.displayScale = _scale;
                [node.body setNeedsDisplay];
            }
        }
    }
}

- (void)releaseNode:(gr_layer *)identity
{
    GRNode *node = _nodes[key(identity)];
    if (!node)
        return;
    [_nodes removeObjectForKey:key(identity)];
    [node.outer removeFromSuperlayer];
    node.body.delegate = nil;
    if (node.imageID)
        [self releaseImage:node.imageID];
    if (node.parent)
        [_nodes[key(node.parent)].children removeObject:key(identity)];
    for (NSValue *childID in node.children)
    {
        GRNode *child = _nodes[childID];
        if (child.parent == identity)
        {
            child.parent = NULL;
            [child.outer removeFromSuperlayer];
        }
    }
}

- (void)apply:(const gr_commit_batch *)batch
{
    gr_drawer_provider_unref(_drawers);
    _drawers = gr_drawer_provider_ref(batch->drawers);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (_scale != batch->backing_scale)
    {
        _scale = batch->backing_scale;
        for (GRNode *node in _nodes.allValues)
        {
            node.outer.contentsScale = _scale;
            node.body.contentsScale = _scale;
            node.body.mask.contentsScale = _scale;
        }
    }
    for (size_t i = 0; i < batch->realized.count; ++i)
    {
        gr_realized_layer value = batch->realized.data[i];
        GRNode *node = [GRNode new];
        /* One layer until a shadow needs an outer layer (see split:). */
        node.body = value.kind == GR_LAYER_SHAPE      ? [CAShapeLayer layer]
                    : value.kind == GR_LAYER_GRADIENT ? [CAGradientLayer layer]
                                                      : [CALayer layer];
        node.outer = node.body;
        node.children = [NSMutableArray array];
        node.body.allowsGroupOpacity = YES;
        node.body.allowsEdgeAntialiasing = YES;
        node.body.contentsScale = _scale;
        node.body.anchorPoint = CGPointZero;
        _nodes[key(value.layer)] = node;
    }
    if (batch->root)
    {
        _root = batch->root;
        [_host addSublayer:[self node:_root].outer];
    }
    /* Match Core Animation's realization order: geometry before attachment. */
    for (size_t i = 0; i < batch->updates.count; ++i)
        [self update:&batch->updates.data[i]];
    for (size_t i = 0; i < batch->children.count; ++i)
    {
        const gr_children_update *u = &batch->children.data[i];
        GRNode *parent = [self node:u->parent];
        for (NSValue *old in parent.children)
        {
            bool found = false;
            for (size_t j = 0; j < u->children.count; ++j)
                found |= old.pointerValue == u->children.data[j];
            GRNode *child = [self node:old.pointerValue];
            if (!found && child.parent == u->parent)
            {
                child.parent = NULL;
                [child.outer removeFromSuperlayer];
            }
        }
    }
    for (size_t i = 0; i < batch->children.count; ++i)
    {
        const gr_children_update *u = &batch->children.data[i];
        NSMutableArray *children = [NSMutableArray arrayWithCapacity:u->children.count];
        for (size_t j = 0; j < u->children.count; ++j)
        {
            gr_layer *identity = u->children.data[j];
            GRNode *child = [self node:identity];
            if (child.parent && child.parent != u->parent)
                [_nodes[key(child.parent)].children removeObject:key(identity)];
            child.parent = u->parent;
            [children addObject:key(identity)];
        }
        [self node:u->parent].children = children;
        [self syncChildren:u->parent];
    }
    for (size_t i = 0; i < batch->released.count; ++i)
        [self releaseNode:batch->released.data[i]];
    [CATransaction commit];
}

@end

static void request_backend(void *p)
{
    [(__bridge GRBackend *)p request];
}

static void apply_backend(void *p, const gr_commit_batch *batch)
{
    [(__bridge GRBackend *)p apply:batch];
}

static void destroy_backend(void *p)
{
    GRBackend *backend = CFBridgingRelease(p);
    [backend shutdown];
}

bool gr_macos_install(gr_scene *scene, CALayer *host)
{
    GRBackend *backend = [GRBackend new];
    backend.host = host;
    backend.scene = gr_scene_get_link(scene);
    backend.runLoop = (CFRunLoopRef)CFRetain(CFRunLoopGetCurrent());
    void *owned = (__bridge_retained void *)backend;
    gr_backend_vtable vtable = {request_backend, apply_backend, destroy_backend};
    if (gr_scene_set_backend(scene, &vtable, owned) != GR_OK)
    {
        destroy_backend(owned);
        return false;
    }
    return true;
}
