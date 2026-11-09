#include "internal.h"

#include <Carbon/Carbon.h>

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
    NSTimer *_timer;
    bool _focused;
    bool _inside;      /* The pointer hovers over the view (as last reported). */
    uint32_t _buttons; /* Pressed buttons, 1u << gr_button. */
    NSTrackingArea *_tracking;
    NSCursor *_cursor;
    NSTextInputContext *_textContext; /* Created when text input first turns on. */
    bool _secureInput;                /* Secure event input is enabled for a focused password field. */
    bool _inputMethodMouse;           /* The input method took the current primary-button press. */
    /* The shared text input state with this view's edits applied; text borrows _text, layout holds a reference. */
    bool _textOn;
    gr_text_input _textInput;
    gr_attributed_text *_text;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [self storeTextInput:NULL];
    gr_scene_link_unref(_sceneLink);
}

- (void)detachHost
{
    _host = NULL;
    _focused = false;
    _inside = false;
    _buttons = 0;
    _inputMethodMouse = false;
    _cursor = nil;
    [self.window invalidateCursorRectsForView:self];
    [_textContext discardMarkedText];
    if (self.window.firstResponder == self)
        [_textContext deactivate];
    _textContext = nil;
    [self storeTextInput:NULL];
    [self updateSecureInput];
    // Invalidation removes the run-loop registration and releases the link's strong reference to this view.
    [_displayLink invalidate];
    _displayLink = nil;
    [_timer invalidate];
    _timer = nil;
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

/* While composing, the input method sees primary-button presses first, like NSTextView: it may take a click in the
 * composition, and commits or cancels the composition for clicks elsewhere. Returns whether it took the event; a
 * press it takes keeps its drags and release. */
- (bool)mouseToInputMethod:(NSEvent *)event
{
    if (event.type == NSEventTypeLeftMouseDown)
        return _inputMethodMouse = [self hasMarkedText] && [self.inputContext handleEvent:event];
    if (!_inputMethodMouse)
        return false;
    [self.inputContext handleEvent:event];
    if (event.type == NSEventTypeLeftMouseUp)
        _inputMethodMouse = false;
    return true;
}

- (void)mouseDown:(NSEvent *)event
{
    if (![self mouseToInputMethod:event] && ![self press:event down:true])
        [super mouseDown:event];
}

- (void)mouseUp:(NSEvent *)event
{
    if (![self mouseToInputMethod:event] && ![self press:event down:false])
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
    if (![self mouseToInputMethod:event] && ![self move:event])
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

- (bool)key:(NSEvent *)event down:(bool)down composing:(bool)composing
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
        .composing = composing,
        .native_code = code,
    };
    return [self dispatch:e] && !composing;
}

/* With text input on, keys the handler leaves go to the input context, which turns them into edits and commands.
 * During a composition the input method owns every key. */
- (void)keyDown:(NSEvent *)event
{
    NSTextInputContext *context = self.inputContext;
    if ([self key:event down:true composing:context && [self hasMarkedText]])
        return;
    if (context && self.inputContext == context && [context handleEvent:event])
        return;
    [super keyDown:event];
}

- (void)keyUp:(NSEvent *)event
{
    if (![self key:event down:false composing:self.inputContext && [self hasMarkedText]])
        [super keyUp:event];
}

/* Modifier keys arrive as flag changes. Caps Lock reports only its toggles, so each one is a press and release. */
- (void)flagsChanged:(NSEvent *)event
{
    bool handled, composing = self.inputContext && [self hasMarkedText];
    if (event.keyCode == 0x39 /* kVK_CapsLock */)
    {
        bool pressed = [self key:event down:true composing:composing];
        handled = [self key:event down:false composing:composing] || pressed;
    }
    else
    {
        NSUInteger mask = gr_macos_modifier_mask(event.keyCode);
        handled = mask && [self key:event down:(event.modifierFlags & mask) != 0 composing:composing];
    }
    /* Input methods watch modifiers too: Shift switches Pinyin between Chinese and English. */
    NSTextInputContext *context = self.inputContext;
    if (!handled && !(context && [context handleEvent:event]))
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

/* Timers: a one-shot timer on the main run loop in common modes, so it also fires during tracking. */

- (void)setTimer:(double)deadline
{
    [_timer invalidate];
    _timer = nil;
    if (!_host || deadline == INFINITY)
        return;
    __weak GRSceneView *weak = self;
    _timer = [NSTimer timerWithTimeInterval:fmax(deadline - CACurrentMediaTime(), 0)
                                    repeats:NO
                                      block:^(NSTimer *timer) {
                                        GRSceneView *view = weak;
                                        if (view)
                                            [view timerFired:timer];
                                      }];
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)timerFired:(NSTimer *)timer
{
    if (timer != _timer)
        return;
    _timer = nil;
    [self dispatch:(gr_event){.type = GR_EVENT_TIMER, .time = CACurrentMediaTime()}];
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
    [self updateSecureInput];
    return YES;
}

- (BOOL)resignFirstResponder
{
    if (![super resignFirstResponder])
        return NO;
    [self updateFocus:false];
    if (_secureInput)
    {
        DisableSecureEventInput();
        _secureInput = false;
    }
    return YES;
}

/* Text input. The view keeps a copy of the shared state (see host.h), answers the input context from it in the
 * UTF-16 offsets AppKit uses, and turns input method calls into TEXT_EDIT and TEXT_COMMAND events. */

static gr_attributed_text *plain_text(gr_string utf8)
{
    gr_text_style style = gr_text_style_default();
    return gr_attributed_text_new(utf8, &style, (gr_text_span_list){NULL, 0});
}

- (const gr_text_input *)sharedInput
{
    return _textOn ? &_textInput : NULL;
}

/* Replaces the stored state with a copy of input, or clears it with NULL. */
- (void)storeTextInput:(const gr_text_input *)input
{
    gr_attributed_text *text = input ? plain_text(input->text) : NULL;
    gr_attributed_text_unref(_text);
    gr_text_layout_unref(_textInput.layout);
    _text = text;
    _textOn = text != NULL;
    _textInput = text ? *input : (gr_text_input){0};
    if (text)
    {
        _textInput.text = gr_attributed_text_get_utf8(text);
        gr_text_layout_ref(input->layout);
    }
}

/* Applies one of this view's TEXT_EDIT events to the stored state. */
- (void)applyEdit:(const gr_text_edit_event *)e
{
    gr_string old = _textInput.text;
    size_t length = old.count - (e->replace.end - e->replace.start) + e->text.count;
    char *joined = malloc(length ? length : 1);
    if (!joined)
        return;
    memcpy(joined, old.data, e->replace.start);
    if (e->text.count)
        memcpy(joined + e->replace.start, e->text.data, e->text.count);
    memcpy(joined + e->replace.start + e->text.count, old.data + e->replace.end, old.count - e->replace.end);
    gr_attributed_text *text = plain_text((gr_string){joined, length});
    free(joined);
    if (!text)
        return;
    gr_attributed_text_unref(_text);
    _text = text;
    _textInput.text = gr_attributed_text_get_utf8(text);
    _textInput.selection = e->selection;
    _textInput.composition = e->composition;
}

/* Host-space rectangle of range's first line, or its caret for an empty range, from the shared layout; without
 * one, the shared caret. */
- (gr_rect)textRect:(gr_text_range)range
{
    if (!_textInput.layout)
        return _textInput.caret;
    /* After edits the layout may lag the text until the field shares again; its own queries clamp. */
    gr_rect r;
    if (range.start >= range.end || !gr_text_layout_get_range_rects(_textInput.layout, range, &r, 1))
        gr_text_layout_get_caret_rect(_textInput.layout, (gr_text_position){range.start, GR_TEXT_AFFINITY_DOWNSTREAM},
                                      &r);
    r.origin.x += _textInput.origin.x;
    r.origin.y += _textInput.origin.y;
    return r;
}

- (NSTextInputContext *)inputContext
{
    if (![self sharedInput])
        return nil;
    if (!_textContext)
        _textContext = [[NSTextInputContext alloc] initWithClient:self];
    return _textContext;
}

/* Password fields hold secure event input while they have focus, like NSSecureTextField. */
- (void)updateSecureInput
{
    const gr_text_input *input = [self sharedInput];
    bool secure = input && input->kind == GR_TEXT_INPUT_PASSWORD && self.window.firstResponder == self;
    if (secure == _secureInput)
        return;
    _secureInput = secure;
    if (secure)
        EnableSecureEventInput();
    else
        DisableSecureEventInput();
}

- (void)setTextInput:(const gr_text_input *)input
{
    bool restart = input && (!_textOn || input->kind != _textInput.kind);
    bool recompose = input && _textOn &&
                     (input->composition.start != _textInput.composition.start ||
                      input->composition.end != _textInput.composition.end);
    [self storeTextInput:input];
    input = [self sharedInput];
    bool first = self.window.firstResponder == self;
    if (!input)
    {
        [_textContext discardMarkedText];
        if (first)
            [_textContext deactivate];
        [self updateSecureInput];
        return;
    }
    NSTextInputContext *context = self.inputContext;
    if (restart)
    {
        context.allowedInputSourceLocales =
            input->kind == GR_TEXT_INPUT_PASSWORD ? @[ NSAllRomanInputSourcesLocaleIdentifier ] : nil;
        if (first)
            [context activate];
        [self updateSecureInput];
    }
    else if (recompose)
        [context discardMarkedText];
    [context invalidateCharacterCoordinates];
}

- (NSRange)utf16Range:(gr_text_range)range
{
    gr_attributed_text *text = _text;
    size_t start = gr_attributed_text_utf16_index(text, range.start);
    return NSMakeRange(start, gr_attributed_text_utf16_index(text, range.end) - start);
}

/* Clamps an AppKit range to the shared text and converts it to UTF-8 offsets. */
- (gr_text_range)utf8Range:(NSRange)range clamped:(NSRange *)clamped
{
    gr_attributed_text *text = _text;
    NSUInteger count = gr_attributed_text_get_utf16(text).count;
    NSUInteger start = range.location < count ? range.location : count;
    NSUInteger end = start + (range.length < count - start ? range.length : count - start);
    if (clamped)
        *clamped = NSMakeRange(start, end - start);
    return (gr_text_range){gr_attributed_text_utf8_index(text, start), gr_attributed_text_utf8_index(text, end)};
}

- (NSRect)screenRect:(gr_rect)r
{
    NSRect local =
        NSMakeRect(r.origin.x, self.bounds.size.height - r.origin.y - r.size.height, r.size.width, r.size.height);
    NSRect inWindow = [self convertRect:local toView:nil];
    return self.window ? [self.window convertRectToScreen:inWindow] : inWindow;
}

/* UTF-8 length of the first count UTF-16 units of string, never splitting a surrogate pair. */
static size_t utf8_prefix(NSString *string, NSUInteger count)
{
    if (count > string.length)
        count = string.length;
    if (count && count < string.length && CFStringIsSurrogateLowCharacter([string characterAtIndex:count]))
        --count;
    return [[string substringToIndex:count] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
}

/* Replaces replacement (or else the composition, or else the selection) with string, as a composition when marked. */
- (void)edit:(id)string selected:(NSRange)selected replacement:(NSRange)replacement marked:(bool)marked
{
    const gr_text_input *input = [self sharedInput];
    if (!input)
        return;
    NSString *plain = [string isKindOfClass:NSAttributedString.class] ? [(NSAttributedString *)string string] : string;
    gr_text_range target = replacement.location != NSNotFound ? [self utf8Range:replacement clamped:NULL]
                           : input->composition.start < input->composition.end ? input->composition
                                                                               : input->selection;
    NSData *utf8 = [plain dataUsingEncoding:NSUTF8StringEncoding];
    size_t length = utf8.length;
    gr_event e = {.type = GR_EVENT_TEXT_EDIT, .time = CACurrentMediaTime()};
    e.value.edit.replace = target;
    e.value.edit.text = (gr_string){utf8.bytes, length};
    size_t at = target.start;
    if (marked && length)
    {
        e.value.edit.composition = (gr_text_range){at, at + length};
        size_t from = utf8_prefix(plain, selected.location);
        size_t to =
            utf8_prefix(plain, selected.location + (selected.length < plain.length ? selected.length : plain.length));
        e.value.edit.selection = (gr_text_range){at + from, at + (to > from ? to : from)};
    }
    else
    {
        e.value.edit.composition = (gr_text_range){at, at};
        e.value.edit.selection = (gr_text_range){at + length, at + length};
    }
    [self applyEdit:&e.value.edit];
    [self dispatch:e];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    [self edit:string selected:NSMakeRange(0, 0) replacement:replacementRange marked:false];
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange
{
    [self edit:string selected:selectedRange replacement:replacementRange marked:true];
}

/* Keeps the composed text as it is and ends the composition. */
- (void)unmarkText
{
    const gr_text_input *input = [self sharedInput];
    if (!input || input->composition.start == input->composition.end)
        return;
    gr_event e = {.type = GR_EVENT_TEXT_EDIT, .time = CACurrentMediaTime()};
    e.value.edit = (gr_text_edit_event){
        .replace = {input->selection.start, input->selection.start},
        .selection = input->selection,
        .composition = {input->selection.start, input->selection.start},
    };
    [self applyEdit:&e.value.edit];
    [self dispatch:e];
}

- (BOOL)hasMarkedText
{
    const gr_text_input *input = [self sharedInput];
    return input && input->composition.start < input->composition.end;
}

- (NSRange)markedRange
{
    return [self hasMarkedText] ? [self utf16Range:[self sharedInput]->composition] : NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange
{
    const gr_text_input *input = [self sharedInput];
    return input ? [self utf16Range:input->selection] : NSMakeRange(NSNotFound, 0);
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText
{
    return @[];
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    const gr_text_input *input = [self sharedInput];
    if (!input || input->kind == GR_TEXT_INPUT_PASSWORD)
        return nil;
    NSRange clamped;
    [self utf8Range:range clamped:&clamped];
    if (actualRange)
        *actualRange = clamped;
    gr_utf16 units = gr_attributed_text_get_utf16(_text);
    NSString *text = [[NSString alloc] initWithCharacters:units.data + clamped.location length:clamped.length];
    return [[NSAttributedString alloc] initWithString:text];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    if (![self sharedInput])
        return NSZeroRect;
    NSRange clamped;
    gr_text_range utf8 = [self utf8Range:range clamped:&clamped];
    if (actualRange)
        *actualRange = clamped;
    return [self screenRect:[self textRect:utf8]];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    if (![self sharedInput] || !_textInput.layout || !self.window)
        return NSNotFound;
    NSPoint local = [self convertPoint:[self.window convertPointFromScreen:point] fromView:nil];
    gr_point at = {local.x - _textInput.origin.x, self.bounds.size.height - local.y - _textInput.origin.y};
    return gr_attributed_text_utf16_index(_text, gr_text_layout_hit_test(_textInput.layout, at).index);
}

/* Key bindings: the standard selectors (Option+Delete, Control+A, Shift+arrows, Escape) become TEXT_COMMAND
 * events; the rest, and commands the handler leaves, go up the responder chain. */
static bool text_command(SEL selector, gr_text_command_event *out)
{
    static const struct
    {
        const char *name;
        gr_text_command command;
        bool extend;
    } commands[] = {
        {"insertNewline:", GR_TEXT_COMMAND_INSERT_NEWLINE, false},
        {"insertLineBreak:", GR_TEXT_COMMAND_INSERT_NEWLINE, false},
        {"insertNewlineIgnoringFieldEditor:", GR_TEXT_COMMAND_INSERT_NEWLINE, false},
        {"insertTab:", GR_TEXT_COMMAND_INSERT_TAB, false},
        {"insertTabIgnoringFieldEditor:", GR_TEXT_COMMAND_INSERT_TAB, false},
        {"insertBacktab:", GR_TEXT_COMMAND_INSERT_BACKTAB, false},
        {"deleteBackward:", GR_TEXT_COMMAND_DELETE_BACKWARD, false},
        {"deleteBackwardByDecomposingPreviousCharacter:", GR_TEXT_COMMAND_DELETE_BACKWARD, false},
        {"deleteForward:", GR_TEXT_COMMAND_DELETE_FORWARD, false},
        {"deleteWordBackward:", GR_TEXT_COMMAND_DELETE_WORD_BACKWARD, false},
        {"deleteWordForward:", GR_TEXT_COMMAND_DELETE_WORD_FORWARD, false},
        {"deleteToBeginningOfLine:", GR_TEXT_COMMAND_DELETE_TO_LINE_START, false},
        {"deleteToBeginningOfParagraph:", GR_TEXT_COMMAND_DELETE_TO_LINE_START, false},
        {"deleteToEndOfLine:", GR_TEXT_COMMAND_DELETE_TO_LINE_END, false},
        {"deleteToEndOfParagraph:", GR_TEXT_COMMAND_DELETE_TO_LINE_END, false},
        {"moveLeft:", GR_TEXT_COMMAND_MOVE_LEFT, false},
        {"moveBackward:", GR_TEXT_COMMAND_MOVE_LEFT, false},
        {"moveRight:", GR_TEXT_COMMAND_MOVE_RIGHT, false},
        {"moveForward:", GR_TEXT_COMMAND_MOVE_RIGHT, false},
        {"moveUp:", GR_TEXT_COMMAND_MOVE_UP, false},
        {"moveDown:", GR_TEXT_COMMAND_MOVE_DOWN, false},
        {"moveWordLeft:", GR_TEXT_COMMAND_MOVE_WORD_LEFT, false},
        {"moveWordBackward:", GR_TEXT_COMMAND_MOVE_WORD_LEFT, false},
        {"moveWordRight:", GR_TEXT_COMMAND_MOVE_WORD_RIGHT, false},
        {"moveWordForward:", GR_TEXT_COMMAND_MOVE_WORD_RIGHT, false},
        {"moveToBeginningOfLine:", GR_TEXT_COMMAND_MOVE_LINE_START, false},
        {"moveToLeftEndOfLine:", GR_TEXT_COMMAND_MOVE_LINE_START, false},
        {"moveToBeginningOfParagraph:", GR_TEXT_COMMAND_MOVE_LINE_START, false},
        {"moveToEndOfLine:", GR_TEXT_COMMAND_MOVE_LINE_END, false},
        {"moveToRightEndOfLine:", GR_TEXT_COMMAND_MOVE_LINE_END, false},
        {"moveToEndOfParagraph:", GR_TEXT_COMMAND_MOVE_LINE_END, false},
        {"moveToBeginningOfDocument:", GR_TEXT_COMMAND_MOVE_DOCUMENT_START, false},
        {"moveToEndOfDocument:", GR_TEXT_COMMAND_MOVE_DOCUMENT_END, false},
        {"pageUp:", GR_TEXT_COMMAND_MOVE_PAGE_UP, false},
        {"pageDown:", GR_TEXT_COMMAND_MOVE_PAGE_DOWN, false},
        {"moveLeftAndModifySelection:", GR_TEXT_COMMAND_MOVE_LEFT, true},
        {"moveBackwardAndModifySelection:", GR_TEXT_COMMAND_MOVE_LEFT, true},
        {"moveRightAndModifySelection:", GR_TEXT_COMMAND_MOVE_RIGHT, true},
        {"moveForwardAndModifySelection:", GR_TEXT_COMMAND_MOVE_RIGHT, true},
        {"moveUpAndModifySelection:", GR_TEXT_COMMAND_MOVE_UP, true},
        {"moveDownAndModifySelection:", GR_TEXT_COMMAND_MOVE_DOWN, true},
        {"moveWordLeftAndModifySelection:", GR_TEXT_COMMAND_MOVE_WORD_LEFT, true},
        {"moveWordBackwardAndModifySelection:", GR_TEXT_COMMAND_MOVE_WORD_LEFT, true},
        {"moveWordRightAndModifySelection:", GR_TEXT_COMMAND_MOVE_WORD_RIGHT, true},
        {"moveWordForwardAndModifySelection:", GR_TEXT_COMMAND_MOVE_WORD_RIGHT, true},
        {"moveToBeginningOfLineAndModifySelection:", GR_TEXT_COMMAND_MOVE_LINE_START, true},
        {"moveToLeftEndOfLineAndModifySelection:", GR_TEXT_COMMAND_MOVE_LINE_START, true},
        {"moveToBeginningOfParagraphAndModifySelection:", GR_TEXT_COMMAND_MOVE_LINE_START, true},
        {"moveParagraphBackwardAndModifySelection:", GR_TEXT_COMMAND_MOVE_LINE_START, true},
        {"moveToEndOfLineAndModifySelection:", GR_TEXT_COMMAND_MOVE_LINE_END, true},
        {"moveToRightEndOfLineAndModifySelection:", GR_TEXT_COMMAND_MOVE_LINE_END, true},
        {"moveToEndOfParagraphAndModifySelection:", GR_TEXT_COMMAND_MOVE_LINE_END, true},
        {"moveParagraphForwardAndModifySelection:", GR_TEXT_COMMAND_MOVE_LINE_END, true},
        {"moveToBeginningOfDocumentAndModifySelection:", GR_TEXT_COMMAND_MOVE_DOCUMENT_START, true},
        {"moveToEndOfDocumentAndModifySelection:", GR_TEXT_COMMAND_MOVE_DOCUMENT_END, true},
        {"pageUpAndModifySelection:", GR_TEXT_COMMAND_MOVE_PAGE_UP, true},
        {"pageDownAndModifySelection:", GR_TEXT_COMMAND_MOVE_PAGE_DOWN, true},
        {"selectAll:", GR_TEXT_COMMAND_SELECT_ALL, false},
        {"cancelOperation:", GR_TEXT_COMMAND_CANCEL, false},
    };

    const char *name = sel_getName(selector);
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
        if (strcmp(name, commands[i].name) == 0)
        {
            *out = (gr_text_command_event){commands[i].command, commands[i].extend};
            return true;
        }
    return false;
}

- (void)doCommandBySelector:(SEL)selector
{
    gr_text_command_event command;
    if ([self sharedInput] && text_command(selector, &command))
    {
        gr_event e = {.type = GR_EVENT_TEXT_COMMAND, .time = CACurrentMediaTime()};
        e.value.command = command;
        if ([self dispatch:e])
            return;
    }
    [super doCommandBySelector:selector];
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

static void set_timer(void *data, double deadline)
{
    [host_view(data) setTimer:deadline];
}

static void set_cursor(void *data, gr_cursor cursor)
{
    [host_view(data) setCursorKind:cursor];
}

static void set_text_input(void *data, const gr_text_input *input)
{
    [host_view(data) setTextInput:input];
}

static void request_focus(void *data)
{
    GRSceneView *view = host_view(data);
    [view.window makeFirstResponder:view];
}

static const gr_scene_host_vtable view_host_vtable = {
    destroy_view_host, request_frame, now, set_timer, set_cursor, set_text_input, request_focus,
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
