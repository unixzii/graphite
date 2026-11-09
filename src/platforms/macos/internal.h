#ifndef GRAPHITE_MACOS_INTERNAL_H
#define GRAPHITE_MACOS_INTERNAL_H

#include <graphite/backend.h>
#include <graphite/macos.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <stdlib.h>

static inline CGPoint gr_cg_point(gr_point p)
{
    return CGPointMake(p.x, p.y);
}

static inline CGRect gr_cg_rect(gr_rect r)
{
    return CGRectMake(r.origin.x, r.origin.y, r.size.width, r.size.height);
}

static inline gr_rect gr_from_cg_rect(CGRect r)
{
    return (gr_rect){{r.origin.x, r.origin.y}, {r.size.width, r.size.height}};
}

static inline CGAffineTransform gr_cg_affine(gr_affine m)
{
    return CGAffineTransformMake(m.a, m.b, m.c, m.d, m.tx, m.ty);
}

/* Graphite's corner bits are Core Animation's, in a flipped (y-down) layer. */
_Static_assert(GR_CORNER_TOP_LEFT == kCALayerMinXMinYCorner && GR_CORNER_TOP_RIGHT == kCALayerMaxXMinYCorner &&
                   GR_CORNER_BOTTOM_LEFT == kCALayerMinXMaxYCorner && GR_CORNER_BOTTOM_RIGHT == kCALayerMaxXMaxYCorner,
               "corner bits");

static inline CACornerMask gr_ca_corners(gr_corners corners)
{
    return (CACornerMask)corners;
}

CGColorRef gr_cg_color(gr_color c) CF_RETURNS_RETAINED;
CGPathRef gr_cg_path(const gr_path *p) CF_RETURNS_RETAINED;
CGImageRef gr_cg_image(gr_image *image) CF_RETURNS_RETAINED;
CGGradientRef gr_cg_gradient(gr_gradient_stop_list stops) CF_RETURNS_RETAINED;
void gr_cg_replay(CGContextRef context, const gr_display_list *list);
extern const gr_text_layout_vtable gr_ct_layout_vtable;
void gr_ct_draw_layout(CGContextRef context, gr_text_layout *layout, gr_point origin, gr_color color);
void gr_ct_draw_glyphs(CGContextRef context, gr_text_layout *layout, size_t line, gr_glyph_range glyphs,
                       gr_point origin, gr_color color);
bool gr_macos_install(gr_scene *scene, CALayer *host);
gr_key_code gr_macos_key_code(unsigned short code);
gr_key gr_macos_named_key(unsigned short code);
/* The device-specific modifier flag a modifier key sets while down, or 0 for other keys. */
NSUInteger gr_macos_modifier_mask(unsigned short code);

/* Private classes: their own members are direct (no message dispatch); AppKit overrides and selector targets are not.
 */
__attribute__((objc_direct_members))
/* Conforming to NSTextInputClient here keeps its methods dynamic under objc_direct_members. */
@interface GRSceneView : NSView<NSTextInputClient>
@property(nonatomic) gr_scene_link *sceneLink;
@property(nonatomic, strong) CALayer *hostLayer;
/** Borrowed; set after wrapping and cleared by detachHost when the host is destroyed. */
@property(nonatomic) gr_scene_host *host;
- (void)syncBackingScale;
- (void)reportViewport;
- (void)requestFrame;
- (void)setTimer:(double)deadline;
- (void)detachHost;
- (void)setCursorKind:(gr_cursor)cursor;
- (void)setTextInput:(const gr_text_input *)input;
@end

__attribute__((objc_direct_members))
@interface GRHost : NSObject
@property(nonatomic, strong) CALayer *layer;
@property(nonatomic, strong) CALayer *container;
@property(nonatomic, strong) NSView *view;
@end

#endif
