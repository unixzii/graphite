#include "internal.h"

#import <CoreText/CoreText.h>
#include <os/lock.h>
#include <stdatomic.h>

/* CoreText text system.
 *
 * Attributed text becomes a CFAttributedString: the base style and then each span set private per-field attributes
 * in order, so later spans win, and each attribute run then gets the CTFont its fields resolve to. Lines come from a
 * CTTypesetter, one paragraph at a time, so max_lines and ellipses stay under our control. An ellipsized line is a
 * CTLine for its visible part plus a separate token line: CTLineCreateTruncatedLine maps the token run onto the elided
 * string range, which breaks caret queries. Layout is built on first use under a lock and is immutable afterwards.
 */

static NSString *const family_key = @"GRFontFamily";
static NSString *const size_key = @"GRFontSize";
static NSString *const weight_key = @"GRFontWeight";
static NSString *const italic_key = @"GRFontItalic";
static NSString *const decorations_key = @"GRDecorations";

#define CT_KEY(name) ((__bridge NSString *)(name))

typedef struct
{
    gr_text_system *system; /* Weak: the system owns this userdata. */
    os_unfair_lock lock;
    CFMutableDictionaryRef fonts; /* [family, size, weight, italic] -> CTFont. */
} ct_system;

typedef struct
{
    CTLineRef line;      /* Visible glyphs, or NULL for an empty or fully elided line. */
    CTLineRef token;     /* Ellipsis after elided text, or NULL. */
    CFRange units;       /* UTF-16 range, including any hard break and elided text. */
    CFIndex elided;      /* UTF-16 start of elided text; the range end when nothing is elided. */
    CFIndex caret_limit; /* Last caret index: before a hard break or elided text. */
    gr_text_range range;
    bool rtl, soft; /* soft: wrapped without a hard break, with text following. */
    double top, ascent, descent, leading;
    double x;        /* Left edge of the visible content, after alignment. */
    double pen;      /* Text position for drawing line. */
    double token_x;  /* Left edge of the token. */
    double visible;  /* line width without trailing whitespace. */
    double trailing; /* line trailing whitespace width. */
    double token_w;  /* token width. */
} ct_line;

typedef struct
{
    ct_system *system;
    gr_attributed_text *text; /* Retained by the owning gr_text_layout. */
    gr_text_layout_options options;
    CFStringRef string;
    os_unfair_lock lock;
    atomic_bool ready;
    ct_line *lines;
    size_t count;
    gr_text_layout_metrics metrics;
} ct_layout;

static void *zalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size);
    if (!p)
        abort();
    return p;
}

/* Fonts */

/* Maps CSS weights to CoreText weight traits through the AppKit named weights. */
static double weight_trait(uint16_t weight)
{
    static const double table[][2] = {{1, -1},    {100, -0.8}, {200, -0.6}, {300, -0.4}, {400, 0}, {500, 0.23},
                                      {600, 0.3}, {700, 0.4},  {800, 0.56}, {900, 0.62}, {1000, 1}};
    for (size_t i = 1; i < sizeof(table) / sizeof(table[0]); ++i)
        if (weight <= table[i][0])
        {
            double t = (weight - table[i - 1][0]) / (table[i][0] - table[i - 1][0]);
            return table[i - 1][1] + t * (table[i][1] - table[i - 1][1]);
        }
    return 1;
}

/* Returns a retained font. */
static CTFontRef create_font(NSString *family, double size, uint16_t weight, bool italic)
{
    NSFontDescriptorSystemDesign design = nil;
    if ([family isEqualToString:@"system-ui"] || [family isEqualToString:@"sans-serif"])
        design = NSFontDescriptorSystemDesignDefault;
    else if ([family isEqualToString:@"serif"])
        design = NSFontDescriptorSystemDesignSerif;
    else if ([family isEqualToString:@"monospace"])
        design = NSFontDescriptorSystemDesignMonospaced;
    NSDictionary *traits = @{
        CT_KEY(kCTFontWeightTrait) : @(weight_trait(weight)),
        CT_KEY(kCTFontSymbolicTrait) : @(italic ? kCTFontItalicTrait : 0)
    };
    NSDictionary *attributes = @{CT_KEY(kCTFontTraitsAttribute) : traits};
    CTFontDescriptorRef descriptor;
    if (design)
    {
        CTFontRef ui = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, NULL);
        NSFontDescriptor *system = CFBridgingRelease(CTFontCopyFontDescriptor(ui));
        CFRelease(ui);
        NSFontDescriptor *designed = [system fontDescriptorWithDesign:design];
        if (!designed)
            designed = system;
        descriptor = CTFontDescriptorCreateCopyWithAttributes((__bridge CTFontDescriptorRef)designed,
                                                              (__bridge CFDictionaryRef)attributes);
    }
    else
    {
        NSMutableDictionary *named = [attributes mutableCopy];
        named[CT_KEY(kCTFontFamilyNameAttribute)] = family;
        descriptor = CTFontDescriptorCreateWithAttributes((__bridge CFDictionaryRef)named);
    }
    CTFontRef font = CTFontCreateWithFontDescriptor(descriptor, size, NULL);
    CFRelease(descriptor);
    /* System fonts ignore the italic trait in descriptors. */
    if (italic && !(CTFontGetSymbolicTraits(font) & kCTFontItalicTrait))
    {
        CTFontRef slanted =
            CTFontCreateCopyWithSymbolicTraits(font, size, NULL, kCTFontItalicTrait, kCTFontItalicTrait);
        if (slanted)
        {
            CFRelease(font);
            font = slanted;
        }
    }
    return font;
}

/* Returns a font owned by the system cache. */
static CTFontRef cached_font(ct_system *s, NSString *family, NSNumber *size, NSNumber *weight, NSNumber *italic)
{
    NSArray *key = @[ family, size, weight, italic ];
    os_unfair_lock_lock(&s->lock);
    CTFontRef font = CFDictionaryGetValue(s->fonts, (__bridge CFArrayRef)key);
    os_unfair_lock_unlock(&s->lock);
    if (font)
        return font;
    CTFontRef created = create_font(family, size.doubleValue, weight.unsignedShortValue, italic.boolValue);
    os_unfair_lock_lock(&s->lock);
    font = CFDictionaryGetValue(s->fonts, (__bridge CFArrayRef)key);
    if (!font)
    {
        CFDictionarySetValue(s->fonts, (__bridge CFArrayRef)key, created);
        font = created;
    }
    os_unfair_lock_unlock(&s->lock);
    CFRelease(created);
    return font;
}

static NSString *string_from(gr_string s)
{
    NSString *string = [[NSString alloc] initWithBytes:s.data length:s.count encoding:NSUTF8StringEncoding];
    return string ? string : @"";
}

static CTFontRef style_font(ct_system *s, const gr_text_style *style)
{
    return cached_font(s, string_from(style->family), @(style->size), @(style->weight),
                       @(style->slant == GR_FONT_SLANT_ITALIC));
}

/* Attributed strings */

static void apply_style(NSMutableAttributedString *s, NSRange range, const gr_text_style *style,
                        gr_text_style_fields fields)
{
    if (fields & GR_TEXT_STYLE_FAMILY)
        [s addAttribute:family_key value:string_from(style->family) range:range];
    if (fields & GR_TEXT_STYLE_SIZE)
        [s addAttribute:size_key value:@(style->size) range:range];
    if (fields & GR_TEXT_STYLE_WEIGHT)
        [s addAttribute:weight_key value:@(style->weight) range:range];
    if (fields & GR_TEXT_STYLE_SLANT)
        [s addAttribute:italic_key value:@(style->slant == GR_FONT_SLANT_ITALIC) range:range];
    if (fields & GR_TEXT_STYLE_DECORATIONS)
    {
        /* Underlines are CoreText's own (they skip descenders); strikethrough is drawn here. */
        [s addAttribute:decorations_key value:@(style->decorations) range:range];
        bool underline = style->decorations & GR_TEXT_DECORATION_UNDERLINE;
        [s addAttribute:CT_KEY(kCTUnderlineStyleAttributeName)
                  value:@(underline ? kCTUnderlineStyleSingle : kCTUnderlineStyleNone)
                  range:range];
    }
    if (fields & GR_TEXT_STYLE_LETTER_SPACING)
        [s addAttribute:CT_KEY(kCTTrackingAttributeName) value:@(style->letter_spacing) range:range];
    if (fields & GR_TEXT_STYLE_COLOR)
    {
        CGColorRef color = gr_cg_color(style->color);
        [s addAttribute:CT_KEY(kCTForegroundColorAttributeName) value:(__bridge id)color range:range];
        CGColorRelease(color);
        [s addAttribute:CT_KEY(kCTForegroundColorFromContextAttributeName) value:@NO range:range];
    }
}

static bool hard_break(UniChar c)
{
    return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029 || c == 0x85 || c == 0x0B || c == 0x0C;
}

static bool paragraph_break(UniChar c)
{
    return c == '\n' || c == '\r' || c == 0x2029;
}

/* Returns the end of the paragraph starting at start, just after its separator. */
static CFIndex paragraph_end(const UniChar *u, CFIndex n, CFIndex start)
{
    for (CFIndex i = start; i < n; ++i)
        if (paragraph_break(u[i]))
            return i + (u[i] == '\r' && i + 1 < n && u[i + 1] == '\n' ? 2 : 1);
    return n;
}

/* Hebrew, Arabic, Syriac, Thaana, NKo and the other right-to-left blocks. */
static bool rtl_letter(UTF32Char c)
{
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF) ||
           (c >= 0x10800 && c <= 0x10FFF) || (c >= 0x1E800 && c <= 0x1EFFF);
}

/* Approximates the first strong character rule with letters: digits, punctuation and marks are skipped. */
static bool paragraph_rtl(const UniChar *u, CFIndex start, CFIndex end)
{
    CFCharacterSetRef letters = CFCharacterSetGetPredefined(kCFCharacterSetLetter);
    CFCharacterSetRef marks = CFCharacterSetGetPredefined(kCFCharacterSetNonBase);
    for (CFIndex i = start; i < end; ++i)
    {
        UTF32Char c = u[i];
        if (CFStringIsSurrogateHighCharacter(u[i]) && i + 1 < end && CFStringIsSurrogateLowCharacter(u[i + 1]))
        {
            c = CFStringGetLongCharacterForSurrogatePair(u[i], u[i + 1]);
            ++i;
        }
        if (CFCharacterSetIsLongCharacterMember(letters, c) && !CFCharacterSetIsLongCharacterMember(marks, c))
            return rtl_letter(c);
    }
    return false;
}

static NSMutableAttributedString *attributed_string(ct_layout *l, const UniChar *u, CFIndex n)
{
    NSMutableAttributedString *s = [[NSMutableAttributedString alloc] initWithString:(__bridge NSString *)l->string];
    if (!n)
        return s;
    [s beginEditing];
    NSRange all = NSMakeRange(0, (NSUInteger)n);
    gr_text_style base = gr_attributed_text_get_base(l->text);
    apply_style(s, all, &base, GR_TEXT_STYLE_ALL & ~(GR_TEXT_STYLE_COLOR | GR_TEXT_STYLE_LETTER_SPACING));
    if (base.letter_spacing != 0)
        apply_style(s, all, &base, GR_TEXT_STYLE_LETTER_SPACING);
    [s addAttribute:CT_KEY(kCTForegroundColorFromContextAttributeName) value:@YES range:all];
    gr_text_span_list spans = gr_attributed_text_get_spans(l->text);
    for (size_t i = 0; i < spans.count; ++i)
    {
        const gr_text_span *span = &spans.data[i];
        CFIndex start = (CFIndex)gr_attributed_text_utf16_index(l->text, span->range.start);
        CFIndex end = (CFIndex)gr_attributed_text_utf16_index(l->text, span->range.end);
        apply_style(s, NSMakeRange((NSUInteger)start, (NSUInteger)(end - start)), &span->style, span->fields);
    }
    for (CFIndex start = 0; start < n;)
    {
        CFIndex end = paragraph_end(u, n, start);
        bool rtl = l->options.direction == GR_TEXT_DIRECTION_RTL ||
                   (l->options.direction == GR_TEXT_DIRECTION_AUTO && paragraph_rtl(u, start, end));
        CTWritingDirection direction = rtl ? kCTWritingDirectionRightToLeft : kCTWritingDirectionLeftToRight;
        CTParagraphStyleSetting setting = {kCTParagraphStyleSpecifierBaseWritingDirection, sizeof(direction),
                                           &direction};
        CTParagraphStyleRef style = CTParagraphStyleCreate(&setting, 1);
        [s addAttribute:CT_KEY(kCTParagraphStyleAttributeName)
                  value:(__bridge id)style
                  range:NSMakeRange((NSUInteger)start, (NSUInteger)(end - start))];
        CFRelease(style);
        start = end;
    }
    ct_system *system = l->system;
    [s enumerateAttributesInRange:all
                          options:0
                       usingBlock:^(NSDictionary *attributes, NSRange range, BOOL *stop) {
                         (void)stop;
                         CTFontRef font = cached_font(system, attributes[family_key], attributes[size_key],
                                                      attributes[weight_key], attributes[italic_key]);
                         [s addAttribute:CT_KEY(kCTFontAttributeName) value:(__bridge id)font range:range];
                       }];
    [s endEditing];
    return s;
}

/* Layout */

static double line_width(CTLineRef line, double *ascent, double *descent, double *leading)
{
    CGFloat a = 0, d = 0, g = 0;
    double width = line ? CTLineGetTypographicBounds(line, &a, &d, &g) : 0;
    *ascent = fmax(*ascent, a);
    *descent = fmax(*descent, d);
    *leading = fmax(*leading, g);
    return width;
}

/* Creates an ellipsis styled like the character at index. */
static CTLineRef create_token(NSAttributedString *s, CFIndex index)
{
    NSDictionary *attributes = [s attributesAtIndex:(NSUInteger)index effectiveRange:NULL];
    NSAttributedString *token = [[NSAttributedString alloc] initWithString:@"…" attributes:attributes];
    return CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)token);
}

/* Replaces line's glyphs with the clusters from its start that fit width with an ellipsis, and the token. */
static void elide(ct_line *line, CTTypesetterRef ts, NSAttributedString *s, double width, CFIndex content_end)
{
    CFIndex start = line->units.location;
    CTLineRef token = create_token(s, start);
    double room = width - CTLineGetTypographicBounds(token, NULL, NULL, NULL);
    CFIndex fit = 0;
    if (start < content_end && room > 0)
    {
        fit = CTTypesetterSuggestClusterBreak(ts, start, room);
        if (fit > content_end - start)
            fit = content_end - start;
        /* A cluster wider than the room still comes back; drop it unless it actually fits. */
        CTLineRef probe = fit ? CTTypesetterCreateLine(ts, CFRangeMake(start, fit)) : NULL;
        if (probe &&
            CTLineGetTypographicBounds(probe, NULL, NULL, NULL) - CTLineGetTrailingWhitespaceWidth(probe) > room + 1e-6)
            fit = 0;
        if (probe)
            CFRelease(probe);
    }
    if (line->line)
        CFRelease(line->line);
    line->line = fit ? CTTypesetterCreateLine(ts, CFRangeMake(start, fit)) : NULL;
    line->token = token;
    line->elided = start + fit;
}

static void build(ct_layout *l)
{
    gr_utf16 utf16 = gr_attributed_text_get_utf16(l->text);
    const UniChar *u = utf16.data;
    CFIndex n = (CFIndex)utf16.count;
    const gr_text_layout_options *o = &l->options;
    gr_text_style base = gr_attributed_text_get_base(l->text);
    NSMutableAttributedString *s = attributed_string(l, u, n);
    CTTypesetterRef ts = n ? CTTypesetterCreateWithAttributedString((__bridge CFAttributedStringRef)s) : NULL;

    size_t capacity = 4, count = 0;
    ct_line *lines = zalloc(capacity, sizeof(*lines));
    bool cut = false, elided = false;
    CFIndex paragraph_start = 0, paragraph_stop = 0;
    bool rtl = o->direction == GR_TEXT_DIRECTION_RTL;
    const double unbounded = 1e9;
    for (CFIndex start = 0; start < n;)
    {
        if (start >= paragraph_stop)
        {
            paragraph_start = start;
            paragraph_stop = paragraph_end(u, n, start);
            rtl = o->direction == GR_TEXT_DIRECTION_RTL ||
                  (o->direction == GR_TEXT_DIRECTION_AUTO && paragraph_rtl(u, paragraph_start, paragraph_stop));
        }
        if (o->max_lines && count == o->max_lines)
        {
            cut = true;
            break;
        }
        CFIndex length;
        if (!o->has_max_width || o->wrap == GR_TEXT_WRAP_NONE)
            length = CTTypesetterSuggestClusterBreak(ts, start, unbounded);
        else if (o->wrap == GR_TEXT_WRAP_WORD)
            length = CTTypesetterSuggestLineBreak(ts, start, o->max_width);
        else
            length = CTTypesetterSuggestClusterBreak(ts, start, o->max_width);
        if (length < 1)
            length = 1;
        if (length > paragraph_stop - start)
            length = paragraph_stop - start;
        /* CoreText breaks between CR and LF. */
        if (u[start + length - 1] == '\r' && start + length < n && u[start + length] == '\n')
            ++length;
        if (count == capacity)
        {
            capacity *= 2;
            lines = realloc(lines, capacity * sizeof(*lines));
            if (!lines)
                abort();
        }
        CFIndex end = start + length;
        lines[count++] = (ct_line){.line = CTTypesetterCreateLine(ts, CFRangeMake(start, length)),
                                   .units = CFRangeMake(start, length),
                                   .elided = end,
                                   .rtl = rtl,
                                   .soft = end < n && !hard_break(u[end - 1])};
        start = end;
    }
    if (!cut && (n == 0 || hard_break(u[n - 1])))
    {
        if (count == capacity)
        {
            lines = realloc(lines, ++capacity * sizeof(*lines));
            if (!lines)
                abort();
        }
        bool empty_rtl = o->direction == GR_TEXT_DIRECTION_RTL;
        lines[count++] = (ct_line){.units = CFRangeMake(n, 0), .elided = n, .rtl = empty_rtl};
    }

    /* Ellipses. */
    for (size_t i = 0; i < count && o->truncation == GR_TEXT_TRUNCATE_END; ++i)
    {
        ct_line *line = &lines[i];
        bool last_cut = cut && i + 1 == count;
        if (last_cut)
        {
            /* The last visible line owns the rest of its paragraph. */
            CFIndex start = line->units.location;
            CFIndex stop = paragraph_end(u, n, start);
            line->units.length = stop - start;
            line->soft = false;
        }
        if (!line->line)
            continue;
        CFIndex stop = line->units.location + line->units.length;
        CFIndex content_end = stop;
        while (content_end > line->units.location && hard_break(u[content_end - 1]))
            --content_end;
        double visible =
            CTLineGetTypographicBounds(line->line, NULL, NULL, NULL) - CTLineGetTrailingWhitespaceWidth(line->line);
        if (last_cut)
            elide(line, ts, s, o->has_max_width ? o->max_width : visible, content_end);
        else if (o->has_max_width && visible > o->max_width + 1e-6)
            elide(line, ts, s, o->max_width, content_end);
        else
            continue;
        elided = true;
    }

    /* Metrics and alignment. */
    double box = o->has_max_width ? o->max_width : 0, top = 0;
    for (size_t i = 0; i < count; ++i)
    {
        ct_line *line = &lines[i];
        CFIndex stop = line->units.location + line->units.length;
        line->caret_limit = line->token ? line->elided : stop;
        if (!line->token && line->caret_limit > line->units.location && hard_break(u[line->caret_limit - 1]))
        {
            bool crlf = u[line->caret_limit - 1] == '\n' && line->caret_limit - 1 > line->units.location &&
                        u[line->caret_limit - 2] == '\r';
            line->caret_limit -= crlf ? 2 : 1;
        }
        double width = line_width(line->line, &line->ascent, &line->descent, &line->leading);
        line->trailing = line->line ? CTLineGetTrailingWhitespaceWidth(line->line) : 0;
        line->visible = width - line->trailing;
        line->token_w = line->token ? line_width(line->token, &line->ascent, &line->descent, &line->leading) : 0;
        if (!line->line && !line->token)
        {
            CTFontRef font = n ? (__bridge CTFontRef)[s attribute:CT_KEY(kCTFontAttributeName)
                                                          atIndex:(NSUInteger)(n - 1)
                                                   effectiveRange:NULL]
                               : style_font(l->system, &base);
            line->ascent = CTFontGetAscent(font);
            line->descent = CTFontGetDescent(font);
            line->leading = CTFontGetLeading(font);
        }
        line->top = top;
        top += line->ascent + line->descent + line->leading;
        line->range = (gr_text_range){gr_attributed_text_utf8_index(l->text, (size_t)line->units.location),
                                      gr_attributed_text_utf8_index(l->text, (size_t)stop)};
        if (!o->has_max_width)
            box = fmax(box, line->visible + line->token_w);
    }
    for (size_t i = 0; i < count; ++i)
    {
        ct_line *line = &lines[i];
        double flush = o->align == GR_TEXT_ALIGN_CENTER ? 0.5 : (o->align == GR_TEXT_ALIGN_START) == !line->rtl ? 0 : 1;
        line->x = (box - line->visible - line->token_w) * flush;
        if (line->rtl)
        {
            line->token_x = line->x;
            line->pen = line->x + line->token_w - line->trailing;
        }
        else
        {
            line->pen = line->x;
            line->token_x = line->x + line->visible;
        }
    }
    if (ts)
        CFRelease(ts);

    ct_line *first = &lines[0], *last = &lines[count - 1];
    l->lines = lines;
    l->count = count;
    l->metrics = (gr_text_layout_metrics){
        .size = {box, top},
        .line_count = count,
        .first_baseline = first->top + first->ascent,
        .last_baseline = last->top + last->ascent,
        .visible = {0, cut ? last->range.end : gr_attributed_text_get_utf8(l->text).count},
        .truncated = cut || elided,
    };
}

static ct_layout *ready(void *data)
{
    ct_layout *l = data;
    if (!atomic_load_explicit(&l->ready, memory_order_acquire))
    {
        os_unfair_lock_lock(&l->lock);
        if (!atomic_load_explicit(&l->ready, memory_order_relaxed))
        {
            @autoreleasepool
            {
                build(l);
            }
            atomic_store_explicit(&l->ready, true, memory_order_release);
        }
        os_unfair_lock_unlock(&l->lock);
    }
    return l;
}

/* Queries */

static size_t utf8(const ct_layout *l, CFIndex index)
{
    return gr_attributed_text_utf8_index(l->text, (size_t)index);
}

static CFIndex utf16(const ct_layout *l, size_t index)
{
    return (CFIndex)gr_attributed_text_utf16_index(l->text, index);
}

static gr_text_layout_metrics ct_metrics(void *data)
{
    return ready(data)->metrics;
}

static void ct_line_info(void *data, size_t index, gr_text_line *out)
{
    const ct_line *line = &ready(data)->lines[index];
    double height = line->ascent + line->descent + line->leading;
    *out = (gr_text_line){line->range,
                          {{line->x, line->top}, {line->visible + line->token_w, height}},
                          line->top + line->ascent,
                          line->ascent,
                          line->descent};
}

static size_t ct_next_stop(void *data, size_t index)
{
    ct_layout *l = data;
    CFIndex u = utf16(l, index);
    if (u >= CFStringGetLength(l->string))
        return gr_attributed_text_get_utf8(l->text).count;
    CFRange cluster = CFStringGetRangeOfComposedCharactersAtIndex(l->string, u);
    return utf8(l, cluster.location + cluster.length);
}

static size_t ct_previous_stop(void *data, size_t index)
{
    ct_layout *l = data;
    CFIndex u = utf16(l, index);
    /* index inside a code point already rounded down to its start, which may itself be a stop. */
    CFIndex probe = utf8(l, u) < index ? u : u - 1;
    if (probe < 0)
        return 0;
    return utf8(l, CFStringGetRangeOfComposedCharactersAtIndex(l->string, probe).location);
}

static size_t line_at_y(const ct_layout *l, double y)
{
    for (size_t i = 0; i < l->count; ++i)
        if (y < l->lines[i].top + l->lines[i].ascent + l->lines[i].descent + l->lines[i].leading)
            return i;
    return l->count - 1;
}

static gr_text_position ct_hit_test(void *data, gr_point point)
{
    ct_layout *l = ready(data);
    const ct_line *line = &l->lines[line_at_y(l, point.y)];
    CFIndex index = line->units.location;
    bool in_token = line->token && (line->rtl ? point.x <= line->token_x + line->token_w : point.x >= line->token_x);
    if (in_token || !line->line)
        index = line->token ? line->elided : line->units.location;
    else
    {
        index = CTLineGetStringIndexForPosition(line->line, CGPointMake(point.x - line->pen, 0));
        if (index == kCFNotFound)
            index = line->units.location;
    }
    if (index < line->units.location)
        index = line->units.location;
    if (index > line->caret_limit)
        index = line->caret_limit;
    size_t position = utf8(l, index);
    bool upstream = line->soft && position == line->range.end;
    return (gr_text_position){position, upstream ? GR_TEXT_AFFINITY_UPSTREAM : GR_TEXT_AFFINITY_DOWNSTREAM};
}

static const ct_line *line_for_position(const ct_layout *l, gr_text_position p)
{
    for (size_t i = 0; i < l->count; ++i)
    {
        const ct_line *line = &l->lines[i];
        if (p.index < line->range.end ||
            (p.index == line->range.end && line->soft && p.affinity == GR_TEXT_AFFINITY_UPSTREAM))
            return line;
    }
    return &l->lines[l->count - 1];
}

/* x of the caret at UTF-16 index within line, in layout coordinates. */
/* Offset of one edge of the grapheme starting at index, or NAN when the line has no caret there. */
static double edge(CTLineRef line, CFIndex index, bool leading)
{
    __block double found = NAN;
    CTLineEnumerateCaretOffsets(line, ^(double offset, CFIndex at, bool is_leading, bool *stop) {
      if (at == index && is_leading == leading)
      {
          found = offset;
          *stop = true;
      }
    });
    return found;
}

/* x of the caret at UTF-16 index within line, in layout coordinates. Downstream is the leading edge of the grapheme
 * at index, upstream the trailing edge of the one before; CoreText's primary offset is neither in bidi text. */
static double caret_x(const ct_layout *l, const ct_line *line, CFIndex index, bool upstream)
{
    if (line->token && index >= line->elided)
        return line->rtl ? line->token_x + line->token_w : line->token_x;
    if (!line->line)
        return line->x;
    CFRange glyphs = CTLineGetStringRange(line->line);
    bool has_next = index<glyphs.location + glyphs.length, has_previous = index> glyphs.location;
    double x = NAN;
    if (has_previous && (upstream || !has_next))
        x = edge(line->line, CFStringGetRangeOfComposedCharactersAtIndex(l->string, index - 1).location, false);
    else if (has_next)
        x = edge(line->line, index, true);
    if (isnan(x))
        x = CTLineGetOffsetForStringIndex(line->line, index, NULL);
    return line->pen + x;
}

static gr_rect ct_caret_rect(void *data, gr_text_position position)
{
    ct_layout *l = ready(data);
    const ct_line *line = line_for_position(l, position);
    CFIndex index = utf16(l, position.index);
    if (index > line->caret_limit)
        index = line->caret_limit;
    double x = caret_x(l, line, index, position.affinity == GR_TEXT_AFFINITY_UPSTREAM);
    return (gr_rect){{x, line->top}, {0, line->ascent + line->descent + line->leading}};
}

typedef struct
{
    double lo, hi;
} span_x;

static int compare_span(const void *a, const void *b)
{
    double x = ((const span_x *)a)->lo, y = ((const span_x *)b)->lo;
    return x < y ? -1 : x > y;
}

static size_t ct_range_rects(void *data, gr_text_range range, gr_rect *rects, size_t capacity)
{
    ct_layout *l = ready(data);
    CFIndex start = utf16(l, range.start), end = utf16(l, range.end);
    size_t total = 0;
    for (size_t i = 0; i < l->count; ++i)
    {
        const ct_line *line = &l->lines[i];
        CFIndex a = start > line->units.location ? start : line->units.location;
        CFIndex line_end = line->units.location + line->units.length;
        CFIndex b = end < line_end ? end : line_end;
        if (a >= b)
            continue;
        CFIndex glyph_end = b < line->elided ? b : line->elided;
        size_t chars = glyph_end > a ? (size_t)(glyph_end - a) : 0;
        span_x *spans = zalloc(chars + 1, sizeof(*spans));
        for (size_t k = 0; k < chars; ++k)
            spans[k] = (span_x){INFINITY, -INFINITY};
        if (chars && line->line)
        {
            CTLineEnumerateCaretOffsets(line->line, ^(double offset, CFIndex index, bool leading, bool *stop) {
              (void)leading;
              (void)stop;
              if (index >= a && index < glyph_end)
              {
                  span_x *x = &spans[index - a];
                  x->lo = fmin(x->lo, offset);
                  x->hi = fmax(x->hi, offset);
              }
            });
            for (size_t k = 0; k < chars; ++k)
            {
                spans[k].lo += line->pen;
                spans[k].hi += line->pen;
            }
        }
        size_t used = chars;
        if (line->token && b > line->elided)
            spans[used++] = (span_x){line->token_x, line->token_x + line->token_w};
        qsort(spans, used, sizeof(*spans), compare_span);
        double height = line->ascent + line->descent + line->leading;
        for (size_t k = 0; k < used;)
        {
            if (!(spans[k].hi > spans[k].lo))
            {
                ++k;
                continue;
            }
            span_x merged = spans[k++];
            while (k < used && spans[k].lo <= merged.hi + 1e-3)
            {
                if (spans[k].hi > spans[k].lo)
                    merged.hi = fmax(merged.hi, spans[k].hi);
                ++k;
            }
            if (total < capacity)
                rects[total] = (gr_rect){{merged.lo, line->top}, {merged.hi - merged.lo, height}};
            ++total;
        }
        free(spans);
    }
    return total;
}

static void ct_layout_destroy(void *data)
{
    ct_layout *l = data;
    for (size_t i = 0; i < l->count; ++i)
    {
        if (l->lines[i].line)
            CFRelease(l->lines[i].line);
        if (l->lines[i].token)
            CFRelease(l->lines[i].token);
    }
    free(l->lines);
    CFRelease(l->string);
    free(l);
}

const gr_text_layout_vtable gr_ct_layout_vtable = {
    ct_metrics,  ct_line_info,  ct_next_stop,   ct_previous_stop,
    ct_hit_test, ct_caret_rect, ct_range_rects, ct_layout_destroy,
};

/* Drawing */

/* Draws strikethrough from the run's font metrics; CoreText draws none. */
static void strike(CGContextRef c, CTLineRef line, double x, double baseline, CGColorRef fallback)
{
    NSArray *runs = (__bridge NSArray *)CTLineGetGlyphRuns(line);
    for (id object in runs)
    {
        CTRunRef run = (__bridge CTRunRef)object;
        NSDictionary *attributes = (__bridge NSDictionary *)CTRunGetAttributes(run);
        unsigned decorations = [attributes[decorations_key] unsignedIntValue];
        CTFontRef font = (__bridge CTFontRef)attributes[CT_KEY(kCTFontAttributeName)];
        CFIndex count = CTRunGetGlyphCount(run);
        if (!(decorations & GR_TEXT_DECORATION_STRIKETHROUGH) || !font || !count)
            continue;
        CGPoint *positions = zalloc((size_t)count, sizeof(*positions));
        CTRunGetPositions(run, CFRangeMake(0, 0), positions);
        double left = INFINITY;
        for (CFIndex i = 0; i < count; ++i)
            left = fmin(left, positions[i].x);
        free(positions);
        double width = CTRunGetTypographicBounds(run, CFRangeMake(0, 0), NULL, NULL, NULL);
        bool from_context = [attributes[CT_KEY(kCTForegroundColorFromContextAttributeName)] boolValue];
        CGColorRef color =
            from_context ? fallback : (__bridge CGColorRef)attributes[CT_KEY(kCTForegroundColorAttributeName)];
        CGContextSetFillColorWithColor(c, color ? color : fallback);
        double thickness = fmax(CTFontGetUnderlineThickness(font), 0.5);
        double y = baseline - CTFontGetXHeight(font) / 2;
        CGContextFillRect(c, CGRectMake(x + left, y - thickness / 2, width, thickness));
    }
    CGContextSetFillColorWithColor(c, fallback);
}

static bool underlined(CTLineRef line)
{
    for (id object in (__bridge NSArray *)CTLineGetGlyphRuns(line))
        if ([((__bridge NSDictionary *)CTRunGetAttributes(
                (__bridge CTRunRef)object))[CT_KEY(kCTUnderlineStyleAttributeName)] intValue] != kCTUnderlineStyleNone)
            return true;
    return false;
}

/* Draws glyphs run by run: a colored run leaves its color in the context, which later context-colored runs would
 * inherit. Underlines then come from CTLineDraw with invisible glyphs, which is the only CoreText path that draws
 * them (skipping descenders). For context-colored runs it takes the stroke color, not the fill color. */
static void draw_line(CGContextRef c, CTLineRef line, double x, double baseline, CGColorRef color)
{
    NSArray *runs = (__bridge NSArray *)CTLineGetGlyphRuns(line);
    for (id object in runs)
    {
        CTRunRef run = (__bridge CTRunRef)object;
        NSDictionary *attributes = (__bridge NSDictionary *)CTRunGetAttributes(run);
        if ([attributes[CT_KEY(kCTForegroundColorFromContextAttributeName)] boolValue])
            CGContextSetFillColorWithColor(c, color);
        CGContextSetTextPosition(c, x, baseline);
        CTRunDraw(run, c, CFRangeMake(0, 0));
    }
    if (underlined(line))
    {
        CGContextSaveGState(c);
        CGContextSetFillColorWithColor(c, color);
        CGContextSetStrokeColorWithColor(c, color);
        CGContextSetTextDrawingMode(c, kCGTextInvisible);
        CGContextSetTextPosition(c, x, baseline);
        CTLineDraw(line, c);
        CGContextRestoreGState(c);
    }
    strike(c, line, x, baseline, color);
}

void gr_ct_draw_layout(CGContextRef c, gr_text_layout *layout, gr_point origin, gr_color color)
{
    ct_layout *l = ready(gr_text_layout_get_data(layout));
    CGColorRef fill = gr_cg_color(color);
    CGContextSaveGState(c);
    CGContextSetFillColorWithColor(c, fill);
    /* Replay runs with a y-down CTM; flip glyphs back upright. */
    CGContextSetTextMatrix(c, CGAffineTransformMakeScale(1, -1));
    /* Backing stores are transparent, where subpixel smoothing looks wrong. */
    CGContextSetShouldSmoothFonts(c, false);
    for (size_t i = 0; i < l->count; ++i)
    {
        const ct_line *line = &l->lines[i];
        double baseline = origin.y + line->top + line->ascent;
        if (line->line)
            draw_line(c, line->line, origin.x + line->pen, baseline, fill);
        if (line->token)
            draw_line(c, line->token, origin.x + line->token_x, baseline, fill);
    }
    CGContextRestoreGState(c);
    CGColorRelease(fill);
}

/* Text system */

static gr_text_layout *ct_layout_text(void *data, gr_attributed_text *text, const gr_text_layout_options *options)
{
    ct_system *system = data;
    ct_layout *l = zalloc(1, sizeof(*l));
    l->system = system;
    l->text = text;
    l->options = *options;
    l->lock = OS_UNFAIR_LOCK_INIT;
    atomic_init(&l->ready, false);
    gr_utf16 units = gr_attributed_text_get_utf16(text);
    l->string = units.count
                    ? CFStringCreateWithCharactersNoCopy(NULL, units.data, (CFIndex)units.count, kCFAllocatorNull)
                    : CFRetain(CFSTR(""));
    gr_text_layout *layout = gr_text_layout_new(system->system, text, options, &gr_ct_layout_vtable, l);
    if (!layout)
        ct_layout_destroy(l);
    return layout;
}

static void ct_system_destroy(void *data)
{
    ct_system *system = data;
    CFRelease(system->fonts);
    free(system);
}

gr_text_system *gr_macos_text_system_new(void)
{
    ct_system *system = zalloc(1, sizeof(*system));
    system->lock = OS_UNFAIR_LOCK_INIT;
    system->fonts =
        CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    gr_text_system_vtable vtable = {ct_layout_text, ct_system_destroy};
    system->system = gr_text_system_new(&vtable, system);
    return system->system;
}
