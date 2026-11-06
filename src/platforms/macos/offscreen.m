#include "internal.h"

static bool render(CALayer *layer, size_t width, size_t height, gr_macos_pixels *out)
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
        return false;
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                    width:width
                                                                                   height:height
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    desc.storageMode = MTLStorageModeShared;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!texture || !queue)
        return false;
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    NSDictionary *options = @{kCARendererMetalCommandQueue : queue, kCARendererColorSpace : (__bridge id)space};
    CARenderer *renderer = [CARenderer rendererWithMTLTexture:texture options:options];
    CGColorSpaceRelease(space);
    renderer.layer = layer;
    CGRect bounds = CGRectMake(0, 0, width, height);
    renderer.bounds = bounds;
    [CATransaction flush];
    [renderer beginFrameAtTime:CACurrentMediaTime() timeStamp:NULL];
    [renderer addUpdateRect:bounds];
    [renderer render];
    [renderer endFrame];
    renderer.layer = nil;
    id<MTLCommandBuffer> fence = [queue commandBuffer];
    if (!fence)
        return false;
    [fence commit];
    [fence waitUntilCompleted];
    if (fence.status == MTLCommandBufferStatusError)
        return false;
    size_t row = width * 4, bytes = row * height;
    uint8_t *bgra = malloc(bytes), *rgba = malloc(bytes);
    if (!bgra || !rgba)
        abort();
    [texture getBytes:bgra bytesPerRow:row fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x)
        {
            const uint8_t *source = bgra + (height - y - 1) * row + x * 4;
            uint8_t *dest = rgba + y * row + x * 4;
            dest[0] = source[2];
            dest[1] = source[1];
            dest[2] = source[0];
            dest[3] = source[3];
        }
    free(bgra);
    *out = (gr_macos_pixels){width, height, rgba};
    return true;
}

bool gr_macos_render_layer(void *pointer, gr_size size, double scale, gr_macos_pixels *out)
{
    if (!out)
        return false;
    *out = (gr_macos_pixels){0};
    double w = round(size.width * scale), h = round(size.height * scale);
    if (!pointer || !isfinite(scale) || scale <= 0 || !isfinite(w) || !isfinite(h) || w < 1 || h < 1 ||
        w >= (double)(SIZE_MAX / 4) || h >= (double)(SIZE_MAX / 4))
        return false;
    size_t width = (size_t)w, height = (size_t)h;
    if (height > SIZE_MAX / (width * 4))
        return false;
    CALayer *layer = (__bridge CALayer *)pointer;
    if (layer.superlayer)
        return false;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    CALayer *wrapper = [CALayer layer];
    wrapper.anchorPoint = CGPointZero;
    wrapper.position = CGPointZero;
    wrapper.bounds = CGRectMake(0, 0, width, height);
    wrapper.sublayerTransform = CATransform3DMakeScale(scale, scale, 1);
    [wrapper addSublayer:layer];
    [CATransaction commit];
    bool ok = render(wrapper, width, height, out);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [layer removeFromSuperlayer];
    [CATransaction commit];
    return ok;
}

void gr_macos_pixels_clear(gr_macos_pixels *pixels)
{
    if (pixels)
    {
        free(pixels->data);
        *pixels = (gr_macos_pixels){0};
    }
}
