#include "demo.h"
#include <graphite/macos.h>
#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/QuartzCore.h>

static void stop_application(void)
{
    [NSApp stop:nil];
    NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:event atStart:NO];
}

API_AVAILABLE(macos(14.0))
@interface GRDemoController : NSObject <NSWindowDelegate>
{
    demo *_demo;
    CADisplayLink *_displayLink;
    unsigned _frame;
    unsigned _fpsTicks;
    unsigned _unflushed[60];
    unsigned _unflushedCount;
    double _startTime;
    double _reportTime;
    bool _selftest;
    bool _reportFPS;
}
@property(nonatomic, weak) NSWindow *window;
@property(nonatomic, readonly) int status;
- (instancetype)initWithDemo:(demo *)content selftest:(bool)selftest fps:(bool)fps;
- (void)start;
- (void)stop;
@end

@implementation GRDemoController

- (instancetype)initWithDemo:(demo *)content selftest:(bool)selftest fps:(bool)fps
{
    self = [super init];
    if (self)
    {
        _demo = content;
        _selftest = selftest;
        _reportFPS = fps;
    }
    return self;
}

- (void)start
{
    _displayLink = [self.window displayLinkWithTarget:self selector:@selector(displayFrame:)];
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)stop
{
    // Invalidation removes the run-loop registration and releases its target.
    [_displayLink invalidate];
    _displayLink = nil;
    stop_application();
}

- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    [self stop];
}

- (void)reportFrameRateAtTime:(double)time
{
    ++_fpsTicks;
    double elapsed = time - _reportTime;
    if (elapsed < 2)
        return;
    printf("fps: %.1f\n", _fpsTicks / elapsed);
    fflush(stdout);
    _reportTime = time;
    _fpsTicks = 0;
}

- (void)finishSelftest
{
    printf("selftest: frames=%u uncommitted=[", _frame);
    for (unsigned i = 0; i < _unflushedCount && i < 60; ++i)
        printf("%s%u", i ? ", " : "", _unflushed[i]);
    printf("] backing_scale=%g\n", gr_scene_get_backing_scale(_demo->scene));
    fflush(stdout);
    _status = _unflushedCount ? 1 : 0;
    [self stop];
}

- (void)displayFrame:(CADisplayLink *)link
{
    if (!self.window.visible)
    {
        [self stop];
        return;
    }
    if (_frame == 0)
    {
        _startTime = link.timestamp;
        _reportTime = link.timestamp;
    }
    ++_frame;
    if (_selftest && _frame > 1 && gr_scene_needs_commit(_demo->scene))
        _unflushed[_unflushedCount++] = _frame;

    // Advance by display time, so animation speed is independent of refresh rate.
    demo_update(_demo, link.targetTimestamp - _startTime);
    if (_reportFPS)
        [self reportFrameRateAtTime:link.timestamp];
    if (_selftest && _frame == 60)
        [self finishSelftest];
}

@end

int demo_platform_run(demo *d, bool selftest, bool fps)
{
    @autoreleasepool
    {
        if (@available(macOS 14.0, *))
        {
            NSApplication *application = [NSApplication sharedApplication];
            [application setActivationPolicy:NSApplicationActivationPolicyRegular];
            gr_size size = demo_size(d->kind);
            NSWindow *window = [[NSWindow alloc]
                initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                            backing:NSBackingStoreBuffered
                              defer:NO];
            window.releasedWhenClosed = NO;
            window.title = @"graphite demo (Core Animation)";
            GRDemoController *controller = [[GRDemoController alloc] initWithDemo:d selftest:selftest fps:fps];
            controller.window = window;
            window.delegate = controller;
            gr_macos_host *host = gr_macos_attach_to_view(d->scene, (__bridge void *)window.contentView);
            if (!host)
            {
                fprintf(stderr, "cannot attach macOS backend\n");
                return 1;
            }
            [window center];
            [window makeKeyAndOrderFront:nil];
            [controller start];
            [application activate];
            [application run];
            [controller stop];
            window.delegate = nil;
            [window close];
            gr_macos_host_unref(host);
            return controller.status;
        }
        fprintf(stderr, "the display-linked demo requires macOS 14 or later\n");
        return 1;
    }
}

bool demo_platform_snapshot(demo *d, const char *path)
{
    @autoreleasepool
    {
        gr_size size = demo_size(d->kind);
        gr_macos_host *host = gr_macos_offscreen_new(d->scene, size, 1);
        if (!host)
        {
            fprintf(stderr, "cannot attach offscreen backend\n");
            return false;
        }
        gr_macos_pixels pixels = {0};
        bool rendered = gr_macos_host_render(host, size, 1, &pixels);
        gr_macos_host_unref(host);
        if (!rendered)
        {
            fprintf(stderr, "offscreen rendering is not available\n");
            return false;
        }
        CFDataRef bytes = CFDataCreate(NULL, pixels.data, (CFIndex)(pixels.width * pixels.height * 4));
        CGDataProviderRef provider = CGDataProviderCreateWithCFData(bytes);
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        /* Store the exact premultiplied readback bytes. */
        CGImageRef image = CGImageCreate(pixels.width, pixels.height, 8, 32, pixels.width * 4, space,
                                         kCGImageAlphaLast | kCGBitmapByteOrderDefault, provider, NULL, false,
                                         kCGRenderingIntentDefault);
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        CGImageDestinationRef destination =
            CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, NULL);
        bool ok = false;
        if (destination && image)
        {
            CGImageDestinationAddImage(destination, image, NULL);
            ok = CGImageDestinationFinalize(destination);
        }
        if (destination)
            CFRelease(destination);
        if (image)
            CGImageRelease(image);
        CGColorSpaceRelease(space);
        CGDataProviderRelease(provider);
        CFRelease(bytes);
        gr_macos_pixels_clear(&pixels);
        if (!ok)
            fprintf(stderr, "cannot write %s\n", path);
        return ok;
    }
}
