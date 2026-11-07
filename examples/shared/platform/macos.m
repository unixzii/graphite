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

@interface GRExampleController : NSObject <NSWindowDelegate>
{
    example_frames _frames;
    unsigned _idleFrame;
    unsigned _committedIdles;
    CFRunLoopObserverRef _idleObserver;
    bool _stopped;
}
@property(nonatomic, readonly) int status;
- (instancetype)initWithExample:(example *)content host:(gr_scene_host *)host selftest:(bool)selftest fps:(bool)fps;
- (bool)start;
- (void)stop;
@end

static void finish_selftest(example_frames *frames);

@implementation GRExampleController

- (instancetype)initWithExample:(example *)content host:(gr_scene_host *)host selftest:(bool)selftest fps:(bool)fps
{
    self = [super init];
    if (self)
    {
        /* The run loop may deliver display-link frames back to back; their changes legitimately share a commit. */
        _frames = (example_frames){.content = content,
                                   .host = host,
                                   .selftest = selftest,
                                   .fps = fps,
                                   .strict = false,
                                   .finished = finish_selftest,
                                   .platform = (__bridge void *)self};
    }
    return self;
}

- (bool)start
{
    if (_frames.selftest)
        [self installIdleObserver];
    return example_frames_start(&_frames);
}

- (void)stop
{
    if (_stopped)
        return;
    _stopped = true;
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
 * contract after the backend's observer: every idle point after a new frame must
 * leave the Scene clean. */
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
    if (_frames.frame == _idleFrame || _frames.done)
        return;
    _idleFrame = _frames.frame;
    if (!gr_scene_needs_commit(_frames.content->scene))
        ++_committedIdles;
    else
        example_frames_fail(&_frames);
}

- (void)finishSelftest
{
    char details[64];
    snprintf(details, sizeof(details), "idle_commits=%u ", _committedIdles);
    _status = example_frames_report(&_frames, details, _committedIdles > 0);
    [self stop];
}

- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    [self stop];
}

@end

static void finish_selftest(example_frames *frames)
{
    [(__bridge GRExampleController *)frames->platform finishSelftest];
}

gr_text_system *example_platform_text_system(void)
{
    return gr_macos_text_system_new();
}

int example_platform_run(example *ex, bool selftest, bool fps)
{
    @autoreleasepool
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
        gr_scene_host *host = gr_macos_attach_to_view(ex->scene, (__bridge void *)window.contentView);
        if (!host)
        {
            fprintf(stderr, "cannot attach macOS backend\n");
            return 1;
        }
        GRExampleController *controller = [[GRExampleController alloc] initWithExample:ex
                                                                                  host:host
                                                                              selftest:selftest
                                                                                   fps:fps];
        window.delegate = controller;
        [window center];
        [window makeKeyAndOrderFront:nil];
        int status = 1;
        if ([controller start])
        {
            [application activate];
            [application run];
            status = controller.status;
        }
        [controller stop];
        window.delegate = nil;
        [window close];
        /* Releasing the host destroys the frame handler, which borrows the controller's state. */
        ex->host = NULL;
        gr_scene_host_unref(host);
        return status;
    }
}

bool example_platform_snapshot(example *ex, const char *path)
{
    @autoreleasepool
    {
        gr_size size = ex->desc->size;
        gr_scene_host *host = gr_macos_offscreen_new(ex->scene, size, 1);
        if (!host)
        {
            fprintf(stderr, "cannot attach offscreen backend\n");
            return false;
        }
        gr_macos_pixels pixels = {0};
        bool rendered = gr_macos_host_render(host, size, 1, &pixels);
        gr_scene_host_unref(host);
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
