#include "internal.h"

static void clip(CGContextRef c, gr_fill_rule rule)
{
    if (rule == GR_FILL_EVEN_ODD)
        CGContextEOClip(c);
    else
        CGContextClip(c);
}

static void gradient(CGContextRef c, const gr_paint *paint)
{
    gr_paint_desc d = gr_paint_get_desc(paint);
    CGGradientDrawingOptions extend = kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation;
    CGGradientRef g = NULL;
    if (d.kind == GR_PAINT_LINEAR)
    {
        g = gr_cg_gradient(d.value.linear.stops);
        if (g)
            CGContextDrawLinearGradient(c, g, gr_cg_point(d.value.linear.start), gr_cg_point(d.value.linear.end),
                                        extend);
    }
    else if (d.kind == GR_PAINT_RADIAL)
    {
        g = gr_cg_gradient(d.value.radial.stops);
        if (g)
            CGContextDrawRadialGradient(c, g, gr_cg_point(d.value.radial.center), 0, gr_cg_point(d.value.radial.center),
                                        d.value.radial.radius, extend);
    }
    if (g)
        CGGradientRelease(g);
}

static void fill(CGContextRef c, gr_path *path, gr_fill_rule rule, gr_paint *paint)
{
    gr_paint_desc d = gr_paint_get_desc(paint);
    CGPathRef p = gr_cg_path(path);
    if (d.kind == GR_PAINT_SOLID)
    {
        CGColorRef color = gr_cg_color(d.value.solid);
        CGContextSetFillColorWithColor(c, color);
        CGColorRelease(color);
        CGContextAddPath(c, p);
        if (rule == GR_FILL_EVEN_ODD)
            CGContextEOFillPath(c);
        else
            CGContextFillPath(c);
    }
    else
    {
        CGContextSaveGState(c);
        CGContextAddPath(c, p);
        clip(c, rule);
        gradient(c, paint);
        CGContextRestoreGState(c);
    }
    CGPathRelease(p);
}

void gr_cg_replay(CGContextRef c, const gr_display_list *list)
{
    gr_draw_op_list ops = gr_display_list_get_ops(list);
    CGFloat *alpha = malloc((ops.count + 1) * sizeof(*alpha));
    if (!alpha)
        abort();
    size_t depth = 0;
    alpha[0] = 1;
    for (size_t i = 0; i < ops.count; ++i)
    {
        const gr_draw_op *op = &ops.data[i];
        switch (op->kind)
        {
            case GR_DRAW_SAVE:
                CGContextSaveGState(c);
                alpha[depth + 1] = alpha[depth];
                ++depth;
                break;
            case GR_DRAW_RESTORE:
                CGContextRestoreGState(c);
                if (depth)
                    --depth;
                break;
            case GR_DRAW_TRANSFORM:
                CGContextConcatCTM(c, gr_cg_affine(op->value.transform));
                break;
            case GR_DRAW_ALPHA:
                alpha[depth] *= op->value.alpha;
                CGContextSetAlpha(c, alpha[depth]);
                break;
            case GR_DRAW_CLIP_RECT:
                CGContextClipToRect(c, gr_cg_rect(op->value.clip_rect));
                break;
            case GR_DRAW_CLIP_PATH:
            {
                CGPathRef p = gr_cg_path(op->value.clip_path.path);
                CGContextAddPath(c, p);
                CGPathRelease(p);
                clip(c, op->value.clip_path.rule);
                break;
            }
            case GR_DRAW_FILL_PATH:
                fill(c, op->value.fill_path.path, op->value.fill_path.rule, op->value.fill_path.paint);
                break;
            case GR_DRAW_STROKE_PATH:
            {
                gr_stroke *stroke = op->value.stroke_path.stroke;
                gr_stroke_style s = gr_stroke_get_style(stroke);
                CGContextSetLineWidth(c, s.width);
                CGContextSetLineCap(c, (CGLineCap)s.cap);
                CGContextSetLineJoin(c, (CGLineJoin)s.join);
                CGContextSetMiterLimit(c, s.miter_limit);
                CGContextSetLineDash(c, s.dash.count ? s.dash_phase : 0, s.dash.data, s.dash.count);
                gr_paint *paint = gr_stroke_get_paint(stroke);
                gr_paint_desc d = gr_paint_get_desc(paint);
                CGPathRef p = gr_cg_path(op->value.stroke_path.path);
                if (d.kind == GR_PAINT_SOLID)
                {
                    CGColorRef color = gr_cg_color(d.value.solid);
                    CGContextSetStrokeColorWithColor(c, color);
                    CGColorRelease(color);
                    CGContextAddPath(c, p);
                    CGContextStrokePath(c);
                }
                else
                {
                    CGContextSaveGState(c);
                    CGContextAddPath(c, p);
                    CGContextReplacePathWithStrokedPath(c);
                    CGContextClip(c);
                    gradient(c, paint);
                    CGContextRestoreGState(c);
                }
                CGPathRelease(p);
                break;
            }
            case GR_DRAW_TEXT_LAYOUT:
                /* Native text drawing lands with the CoreText text system. */
                break;
            case GR_DRAW_IMAGE:
            {
                CGImageRef image = gr_cg_image(op->value.image.image);
                if (image)
                {
                    CGRect dst = gr_cg_rect(op->value.image.destination);
                    CGContextSaveGState(c);
                    CGContextTranslateCTM(c, dst.origin.x, CGRectGetMaxY(dst));
                    CGContextScaleCTM(c, 1, -1);
                    CGContextDrawImage(c, CGRectMake(0, 0, dst.size.width, dst.size.height), image);
                    CGContextRestoreGState(c);
                    CGImageRelease(image);
                }
                break;
            }
        }
    }
    free(alpha);
}
