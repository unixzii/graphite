#ifndef GRAPHITE_MACOS_INTERNAL_H
#define GRAPHITE_MACOS_INTERNAL_H

#include <graphite/backend.h>
#include <graphite/macos.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <stdlib.h>

CGPoint gr_cg_point(gr_point p);
CGRect gr_cg_rect(gr_rect r);
gr_rect gr_from_cg_rect(CGRect r);
CGAffineTransform gr_cg_affine(gr_affine m);
CGColorRef gr_cg_color(gr_color c) CF_RETURNS_RETAINED;
CGPathRef gr_cg_path(const gr_path *p) CF_RETURNS_RETAINED;
CGImageRef gr_cg_image(const gr_image *image) CF_RETURNS_RETAINED;
CGGradientRef gr_cg_gradient(gr_gradient_stop_list stops) CF_RETURNS_RETAINED;
CACornerMask gr_ca_corners(gr_corners corners);
void gr_cg_replay(CGContextRef context, const gr_display_list *list);
extern const gr_text_layout_vtable gr_ct_layout_vtable;
void gr_ct_draw_layout(CGContextRef context, gr_text_layout *layout, gr_point origin, gr_color color);
void gr_ct_draw_glyphs(CGContextRef context, gr_text_layout *layout, size_t line, gr_glyph_range glyphs,
                       gr_point origin, gr_color color);
bool gr_macos_install(gr_scene *scene, CALayer *host);

@interface GRSceneView : NSView
@property(nonatomic) gr_scene_link *sceneLink;
@property(nonatomic, strong) CALayer *hostLayer;
/** Borrowed; set after wrapping and cleared by detachHost when the host is destroyed. */
@property(nonatomic) gr_scene_host *host;
- (void)syncBackingScale;
- (void)reportViewport;
- (void)requestFrame;
- (void)detachHost;
@end

@interface GRHost : NSObject
@property(nonatomic, strong) CALayer *layer;
@property(nonatomic, strong) CALayer *container;
@property(nonatomic, strong) NSView *view;
@end

#endif
