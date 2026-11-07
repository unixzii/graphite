#include "platform.h"

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
@interface GRExampleController : NSObject <NSWindowDelegate>
{
    example *_example;
    CADisplayLink *_displayLink;
    unsigned _frame;
    unsigned _fpsTicks;
    unsigned _uncommitted[60];
    unsigned _uncommittedCount;
    unsigned _coalesced;
    unsigned _committedIdles;
    bool _changedSinceIdle;
    CFRunLoopObserverRef _idleObserver;
    double _startTime;
    double _reportTime;
    bool _selftest;
    bool _reportFPS;
}
@property(nonatomic, weak) NSWindow *window;
@property(nonatomic, readonly) int status;
- (instancetype)initWithExample:(example *)content selftest:(bool)selftest fps:(bool)fps;
- (void)start;
- (void)stop;
@end

@implementation GRExampleController

- (instancetype)initWithExample:(example *)content selftest:(bool)selftest fps:(bool)fps
{
    self = [super init];
    if (self)
    {
        _example = content;
        _selftest = selftest;
        _reportFPS = fps;
    }
    return self;
}

- (void)start
{
    _displayLink = [self.window displayLinkWithTarget:self selector:@selector(displayFrame:)];
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    if (_selftest)
        [self installIdleObserver];
}

- (void)stop
{
    // Invalidation removes the run-loop registration and releases its target.
    [_displayLink invalidate];
    _displayLink = nil;
    if (_idleObserver)
    {
        CFRunLoopObserverInvalidate(_idleObserver);
        CFRelease(_idleObserver);
        _idleObserver = NULL;
    }
    stop_application();
}

/* The macOS backend commits when the main run loop is about to sleep (observer
 * order 1999000), just before Core Animation's own commit (2000000). Check that
 * contract after the backend's observer: every idle point must leave the Scene
 * clean. Ticks the run loop delivers back to back, without sleeping in between,
 * are only counted, because their changes legitimately merge into one commit. */
- (void)installIdleObserver
{
    __weak GRExampleController *weakSelf = self;
    _idleObserver = CFRunLoopObserverCreateWithHandler(NULL, kCFRunLoopBeforeWaiting, true, 1999500,
                                                       ^(CFRunLoopObserverRef observer, CFRunLoopActivity activity) {
                                                         (void)observer;
                                                         (void)activity;
                                                         [weakSelf checkIdleCommit];
                                                       });
    CFRunLoopAddObserver(CFRunLoopGetMain(), _idleObserver, kCFRunLoopCommonModes);
}

- (void)checkIdleCommit
{
    if (!_changedSinceIdle)
        return;
    _changedSinceIdle = false;
    if (!gr_scene_needs_commit(_example->scene))
        ++_committedIdles;
    else if (_uncommittedCount < 60)
        _uncommitted[_uncommittedCount++] = _frame;
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
    for (unsigned i = 0; i < _uncommittedCount; ++i)
        printf("%s%u", i ? ", " : "", _uncommitted[i]);
    printf("] idle_commits=%u coalesced=%u backing_scale=%g\n", _committedIdles, _coalesced,
           gr_scene_get_backing_scale(_example->scene));
    fflush(stdout);
    _status = _uncommittedCount || !_committedIdles ? 1 : 0;
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
    // The run loop did not go idle since the previous tick, so both ticks share one commit.
    if (_selftest && _frame > 1 && gr_scene_needs_commit(_example->scene))
        ++_coalesced;

    // Advance by display time, so animation speed is independent of refresh rate.
    example_update(_example, link.targetTimestamp - _startTime);
    _changedSinceIdle = true;
    if (_reportFPS)
        [self reportFrameRateAtTime:link.timestamp];
    if (_selftest && _frame == 60)
        [self finishSelftest];
}

@end

gr_text_system *example_platform_text_system(void)
{
    return gr_macos_text_system_new();
}

int example_platform_run(example *ex, bool selftest, bool fps)
{
    @autoreleasepool
    {
        if (@available(macOS 14.0, *))
        {
            NSApplication *application = [NSApplication sharedApplication];
            [application setActivationPolicy:NSApplicationActivationPolicyRegular];
            gr_size size = ex->desc->size;
            NSWindow *window = [[NSWindow alloc]
                initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                            backing:NSBackingStoreBuffered
                              defer:NO];
            window.releasedWhenClosed = NO;
            window.title = [NSString stringWithFormat:@"graphite %s (Core Animation)", ex->desc->name];
            GRExampleController *controller = [[GRExampleController alloc] initWithExample:ex
                                                                                  selftest:selftest
                                                                                       fps:fps];
            controller.window = window;
            window.delegate = controller;
            gr_macos_host *host = gr_macos_attach_to_view(ex->scene, (__bridge void *)window.contentView);
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
        fprintf(stderr, "the display-linked example requires macOS 14 or later\n");
        return 1;
    }
}

bool example_platform_snapshot(example *ex, const char *path)
{
    @autoreleasepool
    {
        gr_size size = ex->desc->size;
        gr_macos_host *host = gr_macos_offscreen_new(ex->scene, size, 1);
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
