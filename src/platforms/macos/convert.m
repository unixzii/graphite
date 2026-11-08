#include "internal.h"

CGPoint gr_cg_point(gr_point p)
{
    return CGPointMake(p.x, p.y);
}

CGRect gr_cg_rect(gr_rect r)
{
    return CGRectMake(r.origin.x, r.origin.y, r.size.width, r.size.height);
}

gr_rect gr_from_cg_rect(CGRect r)
{
    return (gr_rect){{r.origin.x, r.origin.y}, {r.size.width, r.size.height}};
}

CGAffineTransform gr_cg_affine(gr_affine m)
{
    return CGAffineTransformMake(m.a, m.b, m.c, m.d, m.tx, m.ty);
}

CGColorRef gr_cg_color(gr_color c)
{
    return CGColorCreateSRGB(c.r, c.g, c.b, c.a);
}

CACornerMask gr_ca_corners(gr_corners c)
{
    CACornerMask mask = 0;
    if (c & GR_CORNER_TOP_LEFT)
        mask |= kCALayerMinXMinYCorner;
    if (c & GR_CORNER_TOP_RIGHT)
        mask |= kCALayerMaxXMinYCorner;
    if (c & GR_CORNER_BOTTOM_LEFT)
        mask |= kCALayerMinXMaxYCorner;
    if (c & GR_CORNER_BOTTOM_RIGHT)
        mask |= kCALayerMaxXMaxYCorner;
    return mask;
}

CGPathRef gr_cg_path(const gr_path *path)
{
    CGMutablePathRef p = CGPathCreateMutable();
    gr_path_element_list elements = gr_path_get_elements(path);
    for (size_t i = 0; i < elements.count; ++i)
    {
        const gr_path_element *e = &elements.data[i];
        switch (e->verb)
        {
            case GR_PATH_MOVE:
                CGPathMoveToPoint(p, NULL, e->p[0].x, e->p[0].y);
                break;
            case GR_PATH_LINE:
                CGPathAddLineToPoint(p, NULL, e->p[0].x, e->p[0].y);
                break;
            case GR_PATH_QUAD:
                CGPathAddQuadCurveToPoint(p, NULL, e->p[0].x, e->p[0].y, e->p[1].x, e->p[1].y);
                break;
            case GR_PATH_CUBIC:
                CGPathAddCurveToPoint(p, NULL, e->p[0].x, e->p[0].y, e->p[1].x, e->p[1].y, e->p[2].x, e->p[2].y);
                break;
            case GR_PATH_CLOSE:
                CGPathCloseSubpath(p);
                break;
        }
    }
    return p;
}

static void release_pixels(void *image, const void *data, size_t size)
{
    (void)data;
    (void)size;
    gr_image_unref(image);
}

/* Images are immutable, so the CGImage borrows their pixels and holds a reference instead of copying them. */
CGImageRef gr_cg_image(gr_image *image)
{
    gr_bytes pixels = gr_image_get_pixels(image);
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(gr_image_ref(image), pixels.data, pixels.count, release_pixels);
    if (!provider)
    {
        gr_image_unref(image);
        return NULL;
    }
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    size_t w = gr_image_get_width(image), h = gr_image_get_height(image);
    CGImageRef result =
        CGImageCreate(w, h, 8, 32, w * 4, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrderDefault, provider,
                      NULL, true, kCGRenderingIntentDefault);
    CGColorSpaceRelease(space);
    CGDataProviderRelease(provider);
    return result;
}

CGGradientRef gr_cg_gradient(gr_gradient_stop_list stops)
{
    if (stops.count > SIZE_MAX / (5 * sizeof(CGFloat)))
        return NULL;
    CGFloat *data = malloc(stops.count * 5 * sizeof(CGFloat));
    if (!data)
        abort();
    CGFloat *locations = data + stops.count * 4;
    for (size_t i = 0; i < stops.count; ++i)
    {
        gr_color c = stops.data[i].color;
        data[i * 4] = c.r;
        data[i * 4 + 1] = c.g;
        data[i * 4 + 2] = c.b;
        data[i * 4 + 3] = c.a;
        locations[i] = stops.data[i].offset;
    }
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGGradientRef result = CGGradientCreateWithColorComponents(space, data, locations, stops.count);
    CGColorSpaceRelease(space);
    free(data);
    return result;
}
