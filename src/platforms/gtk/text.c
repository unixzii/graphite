#include "internal.h"

#include <pango/pangocairo.h>

/* Pango text system.
 *
 * Indices are UTF-8 byte offsets, as in Pango. Spans are resolved here into disjoint segments (base style, then each
 * span's selected fields in order) because Pango picks the attribute with the latest start index, not the latest
 * inserted one. Each layout gets its own PangoContext, since base direction is a context property, with metric
 * hinting and glyph position rounding off so layouts do not depend on the output scale.
 *
 * Line limits use Pango's whole-layout mode: a positive height with end ellipsization keeps the lines that fit and
 * ellipsizes the last one, which then holds the rest of its paragraph. Without ellipsization extra lines are hidden
 * with a clip. Pango objects are not thread-safe, so every callback and draw holds the text system's mutex.
 */

typedef struct
{
    gr_text_system *handle; /* Weak: the handle owns this userdata. */
    GMutex lock;
    PangoFontMap *font_map;
    char *system_family;  /* GTK's interface font family, used for "system-ui". */
    GHashTable *font_ids; /* Face identity key -> glyph font identity; guarded by lock. */
    uint64_t next_font;
} pg_system;

typedef struct
{
    gr_text_range range;
    gr_rect bounds;
    double baseline, ascent, descent;
    size_t stop;        /* End of the line's text, before any paragraph separator. */
    size_t caret_limit; /* stop, the range end for soft breaks, or where elided text starts. */
    bool soft;          /* Wrapped without a hard break, with text following. */
    bool rtl;
} pg_line;

typedef struct
{
    pg_system *system;
    gr_attributed_text *text; /* Retained by the owning gr_text_layout. */
    gr_text_layout_options options;
    bool ready;
    char *utf8; /* NUL-terminated copy with embedded NULs replaced by spaces; Pango stops at NUL. */
    size_t length;
    PangoContext *context;
    PangoLayout *layout;
    double shift;       /* x offset applied to Pango coordinates. */
    double clip_height; /* > 0 hides lines cut by max_lines below the last visible line. */
    /* An ellipsis Pango did not add (the cut fell at a paragraph break or between unwrapped lines). */
    PangoLayout *token;
    gr_point token_at; /* Top-left in layout coordinates. */
    double token_width;
    double cutoff;    /* NAN, or the x beyond which the last line's glyphs are hidden to make room. */
    bool cutoff_left; /* The hidden side is left of cutoff (right-to-left lines). */
    bool token_colored;
    GdkRGBA token_color;
    pg_line *lines;
    size_t count;
    struct pg_glyphs *glyphs; /* Per line, built on first use. */
    const PangoLogAttr *attrs;
    int attr_count;
    gr_text_layout_metrics metrics;
} pg_layout;

/* A line's glyph and where it lives: run is borrowed from the layout (or the ellipsis token layout). */
typedef struct
{
    gr_text_glyph info;
    PangoGlyphItem *run;
    int index;    /* Glyph index within run's glyph string. */
    gr_point pen; /* Pen position of the glyph before offsets: layout coordinates on the baseline. */
    bool colored; /* The run has its own color. */
    GdkRGBA color;
} pg_glyph;

struct pg_glyphs
{
    pg_glyph *data;
    size_t count, capacity;
};

static double from_pango(int units)
{
    return (double)units / PANGO_SCALE;
}

static int to_pango(double value)
{
    return (int)lround(value * PANGO_SCALE);
}

/* Styles */

static const char *family_name(const pg_system *system, gr_string family, char *buffer, size_t size)
{
    if (family.count == 9 && !memcmp(family.data, "system-ui", 9))
        return system->system_family;
    size_t n = family.count < size - 1 ? family.count : size - 1;
    memcpy(buffer, family.data, n);
    buffer[n] = 0;
    return buffer;
}

static void apply_fields(gr_text_style *target, bool *has_color, const gr_text_span *span)
{
    const gr_text_style *s = &span->style;
    if (span->fields & GR_TEXT_STYLE_FAMILY)
        target->family = s->family;
    if (span->fields & GR_TEXT_STYLE_SIZE)
        target->size = s->size;
    if (span->fields & GR_TEXT_STYLE_WEIGHT)
        target->weight = s->weight;
    if (span->fields & GR_TEXT_STYLE_SLANT)
        target->slant = s->slant;
    if (span->fields & GR_TEXT_STYLE_COLOR)
    {
        target->color = s->color;
        *has_color = true;
    }
    if (span->fields & GR_TEXT_STYLE_DECORATIONS)
        target->decorations = s->decorations;
    if (span->fields & GR_TEXT_STYLE_LETTER_SPACING)
        target->letter_spacing = s->letter_spacing;
}

static PangoFontDescription *font_description(const pg_system *system, const gr_text_style *style)
{
    char buffer[256];
    PangoFontDescription *d = pango_font_description_new();
    pango_font_description_set_family(d, family_name(system, style->family, buffer, sizeof(buffer)));
    pango_font_description_set_absolute_size(d, style->size * PANGO_SCALE);
    pango_font_description_set_weight(d, (PangoWeight)style->weight);
    pango_font_description_set_style(d, style->slant == GR_FONT_SLANT_ITALIC ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    return d;
}

static void insert(PangoAttrList *list, PangoAttribute *attribute, size_t start, size_t end)
{
    attribute->start_index = (guint)start;
    attribute->end_index = (guint)end;
    pango_attr_list_insert(list, attribute);
}

static int compare_size(const void *a, const void *b)
{
    size_t x = *(const size_t *)a, y = *(const size_t *)b;
    return x < y ? -1 : x > y;
}

/* Splits the text at span edges and gives each segment the attributes its resolved style needs. */
static PangoAttrList *attributes(const pg_system *system, const gr_attributed_text *text, size_t length)
{
    PangoAttrList *list = pango_attr_list_new();
    if (!length)
        return list;
    gr_text_style base = gr_attributed_text_get_base(text);
    gr_text_span_list spans = gr_attributed_text_get_spans(text);
    size_t *edges = g_new(size_t, 2 + 2 * spans.count), count = 0;
    edges[count++] = 0;
    edges[count++] = length;
    for (size_t i = 0; i < spans.count; ++i)
    {
        edges[count++] = spans.data[i].range.start;
        edges[count++] = spans.data[i].range.end;
    }
    qsort(edges, count, sizeof(*edges), compare_size);
    for (size_t e = 0; e + 1 < count; ++e)
    {
        size_t start = edges[e], end = edges[e + 1];
        if (start == end)
            continue;
        gr_text_style style = base;
        bool has_color = false;
        for (size_t i = 0; i < spans.count; ++i)
            if (spans.data[i].range.start <= start && spans.data[i].range.end >= end)
                apply_fields(&style, &has_color, &spans.data[i]);
        PangoFontDescription *font = font_description(system, &style);
        insert(list, pango_attr_font_desc_new(font), start, end);
        pango_font_description_free(font);
        if (has_color)
        {
            insert(list,
                   pango_attr_foreground_new((guint16)lround(style.color.r * 65535),
                                             (guint16)lround(style.color.g * 65535),
                                             (guint16)lround(style.color.b * 65535)),
                   start, end);
            insert(list, pango_attr_foreground_alpha_new((guint16)lround(style.color.a * 65535)), start, end);
        }
        if (style.decorations & GR_TEXT_DECORATION_UNDERLINE)
            insert(list, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE), start, end);
        if (style.decorations & GR_TEXT_DECORATION_STRIKETHROUGH)
            insert(list, pango_attr_strikethrough_new(TRUE), start, end);
        if (style.letter_spacing != 0)
            insert(list, pango_attr_letter_spacing_new(to_pango(style.letter_spacing)), start, end);
    }
    g_free(edges);
    return list;
}

/* Layout */

static size_t separator_length(const char *s, size_t length, size_t index)
{
    if (index >= length)
        return 0;
    if (s[index] == '\r')
        return index + 1 < length && s[index + 1] == '\n' ? 2 : 1;
    if (s[index] == '\n')
        return 1;
    if (index + 2 < length && !memcmp(s + index, "\xE2\x80\xA9", 3))
        return 3;
    return 0;
}

static double layout_flush(const gr_text_layout_options *o, bool rtl)
{
    if (o->align == GR_TEXT_ALIGN_CENTER)
        return 0.5;
    return (o->align == GR_TEXT_ALIGN_START) == !rtl ? 0 : 1;
}

static size_t byte_at(const pg_layout *l, int offset);
static int chars_before(const pg_layout *l, size_t index);

static bool line_has_ellipsis(PangoLayoutLine *line)
{
    for (GSList *run = line->runs; run; run = run->next)
        if (((PangoGlyphItem *)run->data)->item->analysis.flags & PANGO_ANALYSIS_FLAG_IS_ELLIPSIS)
            return true;
    return false;
}

/* x of the leading edge of the grapheme at index, in Pango coordinates; widths are negative for RTL graphemes. */
static double leading_edge(PangoLayout *layout, size_t index)
{
    PangoRectangle r;
    pango_layout_index_to_pos(layout, (int)index, &r);
    return from_pango(r.x);
}

/* Ends the last visible line with an ellipsis of the last run's font and color, eliding graphemes from its end
 * until the ellipsis fits inside the box. */
static void add_token(pg_layout *l, double box)
{
    pg_line *line = &l->lines[l->count - 1];
    PangoLayoutLine *native = pango_layout_get_line_readonly(l->layout, (int)l->count - 1);
    PangoLayout *token = pango_layout_new(l->context);
    pango_layout_set_text(token, "\xE2\x80\xA6", -1);
    GSList *runs = native->runs, *last = runs ? g_slist_last(runs) : NULL;
    if (line->rtl && runs)
        last = runs; /* Visual order: the logical end of a right-to-left line is its first run. */
    if (last)
    {
        PangoItem *item = ((PangoGlyphItem *)last->data)->item;
        PangoFontDescription *font = pango_font_describe_with_absolute_size(item->analysis.font);
        pango_layout_set_font_description(token, font);
        pango_font_description_free(font);
        for (GSList *a = item->analysis.extra_attrs; a; a = a->next)
        {
            PangoAttribute *attribute = a->data;
            if (attribute->klass->type == PANGO_ATTR_FOREGROUND)
            {
                PangoColor c = ((PangoAttrColor *)attribute)->color;
                l->token_color = (GdkRGBA){c.red / 65535.f, c.green / 65535.f, c.blue / 65535.f, 1};
                l->token_colored = true;
            }
            else if (attribute->klass->type == PANGO_ATTR_FOREGROUND_ALPHA)
                l->token_color.alpha = ((PangoAttrInt *)attribute)->value / 65535.f;
        }
    }
    else
        pango_layout_set_font_description(token, pango_layout_get_font_description(l->layout));
    PangoRectangle logical;
    pango_layout_get_extents(token, NULL, &logical);
    double width = from_pango(logical.width);
    double left = line->bounds.origin.x, right = left + line->bounds.size.width;
    bool fits = line->rtl ? left - width >= -1e-6 : right + width <= box + 1e-6;
    double at = line->rtl ? left : right;
    if (!fits)
    {
        /* Walk grapheme boundaries back from the end until the ellipsis fits beside the kept text. */
        int k = chars_before(l, line->stop);
        for (;; --k)
        {
            size_t index = byte_at(l, k);
            if (index <= line->range.start)
            {
                at = line->rtl ? right : left;
                line->caret_limit = line->range.start;
                break;
            }
            if (!l->attrs[k].is_cursor_position)
                continue;
            double x = (index < line->stop ? leading_edge(l->layout, index) : (line->rtl ? left : right) - l->shift) +
                       l->shift;
            if (line->rtl ? x - width >= -1e-6 : x + width <= box + 1e-6)
            {
                at = x;
                line->caret_limit = index;
                break;
            }
        }
        l->cutoff = at;
        l->cutoff_left = line->rtl;
    }
    l->token = token;
    l->token_width = width;
    double baseline = from_pango(pango_layout_get_baseline(token));
    l->token_at = (gr_point){line->rtl ? at - width : at, line->baseline - baseline};
    if (line->rtl)
    {
        line->bounds.size.width = right - l->token_at.x;
        line->bounds.origin.x = l->token_at.x;
    }
    else
        line->bounds.size.width = at + width - left;
}

static void build(pg_layout *l)
{
    const gr_text_layout_options *o = &l->options;
    PangoLayout *layout = pango_layout_new(l->context);
    l->layout = layout;
    gr_text_style base = gr_attributed_text_get_base(l->text);
    PangoFontDescription *font = font_description(l->system, &base);
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, l->utf8, (int)l->length);
    PangoAttrList *list = attributes(l->system, l->text, l->length);
    pango_layout_set_attributes(layout, list);
    pango_attr_list_unref(list);

    bool rtl = o->direction == GR_TEXT_DIRECTION_RTL;
    pango_layout_set_auto_dir(layout, o->direction == GR_TEXT_DIRECTION_AUTO);
    /* With auto direction Pango flips LEFT and RIGHT for right-to-left paragraphs; otherwise map them here. */
    PangoAlignment start = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT, end = rtl ? PANGO_ALIGN_LEFT : PANGO_ALIGN_RIGHT;
    pango_layout_set_alignment(layout, o->align == GR_TEXT_ALIGN_CENTER  ? PANGO_ALIGN_CENTER
                                       : o->align == GR_TEXT_ALIGN_START ? start
                                                                         : end);
    bool ellipsize = o->truncation == GR_TEXT_TRUNCATE_END;
    bool bounded = o->has_max_width && (o->wrap != GR_TEXT_WRAP_NONE || ellipsize);
    if (bounded)
    {
        pango_layout_set_width(layout, to_pango(o->max_width));
        pango_layout_set_wrap(layout, o->wrap == GR_TEXT_WRAP_CHAR ? PANGO_WRAP_CHAR : PANGO_WRAP_WORD_CHAR);
        /* Unwrapped lines with end truncation: Pango ellipsizes each paragraph to one line. */
        if (o->wrap == GR_TEXT_WRAP_NONE)
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    }

    bool cut = false, height_cut = false;
    int total = pango_layout_get_line_count(layout);
    if (o->max_lines && (size_t)total > o->max_lines)
    {
        cut = true;
        if (ellipsize && o->wrap != GR_TEXT_WRAP_NONE)
        {
            PangoRectangle last, next;
            pango_layout_line_get_extents(pango_layout_get_line_readonly(layout, (int)o->max_lines), NULL, &next);
            PangoLayoutIter *it = pango_layout_get_iter(layout);
            for (size_t i = 0; i + 1 < o->max_lines; ++i)
                pango_layout_iter_next_line(it);
            pango_layout_iter_get_line_extents(it, NULL, &last);
            pango_layout_iter_free(it);
            if (!bounded)
            {
                /* Without a width the ellipsis has to fit the natural width. */
                PangoRectangle logical;
                pango_layout_get_extents(layout, NULL, &logical);
                pango_layout_set_width(layout, logical.width + PANGO_SCALE / 4);
            }
            height_cut = true;
            pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_height(layout, last.y + last.height + next.height / 2);
        }
    }

    int count = pango_layout_get_line_count(layout);
    size_t visible = cut && (size_t)count > o->max_lines ? o->max_lines : (size_t)count;
    pg_line *lines = g_new0(pg_line, visible);
    PangoLayoutIter *it = pango_layout_get_iter(layout);
    double widest = 0;
    for (size_t i = 0; i < visible; ++i, pango_layout_iter_next_line(it))
    {
        PangoLayoutLine *line = pango_layout_iter_get_line_readonly(it);
        PangoRectangle logical;
        pango_layout_iter_get_line_extents(it, NULL, &logical);
        double baseline = from_pango(pango_layout_iter_get_baseline(it));
        size_t start = (size_t)line->start_index, stop = start + (size_t)line->length;
        PangoLayoutLine *next = (int)i + 1 < count ? pango_layout_get_line_readonly(layout, (int)i + 1) : NULL;
        size_t end = next ? (size_t)next->start_index : stop + separator_length(l->utf8, l->length, stop);
        pg_line *out = &lines[i];
        out->range = (gr_text_range){start, end};
        out->bounds = (gr_rect){{from_pango(logical.x), from_pango(logical.y)},
                                {from_pango(logical.width), from_pango(logical.height)}};
        out->baseline = baseline;
        out->ascent = baseline - out->bounds.origin.y;
        out->descent = out->bounds.origin.y + out->bounds.size.height - baseline;
        out->soft = i + 1 < visible && end == stop;
        out->stop = stop;
        out->caret_limit = out->soft ? end : stop;
        /* Elided text collapses onto the position where it starts, before the ellipsis. */
        for (GSList *run = line->runs; run; run = run->next)
        {
            const PangoGlyphItem *item = run->data;
            if (item->item->analysis.flags & PANGO_ANALYSIS_FLAG_IS_ELLIPSIS)
                out->caret_limit = (size_t)item->item->offset;
        }
        out->rtl = line->resolved_dir == PANGO_DIRECTION_RTL;
        widest = fmax(widest, out->bounds.size.width);
    }
    pango_layout_iter_free(it);

    /* Unbounded layouts align inside their widest line; move them to align inside max_width. */
    double box = bounded ? o->max_width : widest;
    if (o->has_max_width && !bounded)
    {
        l->shift = (o->max_width - widest) * layout_flush(o, visible && lines[0].rtl);
        box = o->max_width;
    }
    for (size_t i = 0; i < visible; ++i)
        lines[i].bounds.origin.x += l->shift;
    l->cutoff = NAN;
    l->lines = lines;
    l->count = visible;
    l->attrs = pango_layout_get_log_attrs_readonly(layout, &l->attr_count);
    if (cut && ellipsize && !line_has_ellipsis(pango_layout_get_line_readonly(layout, (int)visible - 1)))
        add_token(l, box);
    pg_line *first = &lines[0], *last = &lines[visible - 1];
    double height = last->bounds.origin.y + last->bounds.size.height;
    if (cut && !height_cut)
        l->clip_height = height;
    l->metrics = (gr_text_layout_metrics){
        .size = {box, height},
        .line_count = visible,
        .first_baseline = first->baseline,
        .last_baseline = last->baseline,
        .visible = {0, cut ? last->range.end : l->length},
        .truncated = cut || pango_layout_is_ellipsized(layout),
    };
}

/* Locks the system and builds the layout on first use; unlock with done. */
static pg_layout *begin(void *data)
{
    pg_layout *l = data;
    g_mutex_lock(&l->system->lock);
    if (!l->ready)
    {
        build(l);
        l->ready = true;
    }
    return l;
}

static void done(pg_layout *l)
{
    g_mutex_unlock(&l->system->lock);
}

/* Queries */

static gr_text_layout_metrics pg_metrics(void *data)
{
    pg_layout *l = begin(data);
    gr_text_layout_metrics m = l->metrics;
    done(l);
    return m;
}

static void pg_line_info(void *data, size_t index, gr_text_line *out)
{
    pg_layout *l = begin(data);
    const pg_line *line = &l->lines[index];
    *out = (gr_text_line){line->range, line->bounds, line->baseline, line->ascent, line->descent};
    done(l);
}

/* Byte offset of the character with the given offset. */
static size_t byte_at(const pg_layout *l, int offset)
{
    return (size_t)(g_utf8_offset_to_pointer(l->utf8, offset) - l->utf8);
}

/* Number of characters starting before index. */
static int chars_before(const pg_layout *l, size_t index)
{
    int count = 0;
    for (size_t i = 0; i < index && i < l->length; ++i)
        count += ((unsigned char)l->utf8[i] & 0xC0) != 0x80;
    return count;
}

static bool on_boundary(const pg_layout *l, size_t index)
{
    return index >= l->length || ((unsigned char)l->utf8[index] & 0xC0) != 0x80;
}

static size_t pg_next_stop(void *data, size_t index)
{
    pg_layout *l = begin(data);
    int k = chars_before(l, index) + (on_boundary(l, index) ? 1 : 0);
    while (k < l->attr_count - 1 && !l->attrs[k].is_cursor_position)
        ++k;
    size_t result = k >= l->attr_count - 1 ? l->length : byte_at(l, k);
    done(l);
    return result;
}

static size_t pg_previous_stop(void *data, size_t index)
{
    pg_layout *l = begin(data);
    int k = chars_before(l, index) - 1;
    while (k > 0 && !l->attrs[k].is_cursor_position)
        --k;
    size_t result = k > 0 ? byte_at(l, k) : 0;
    done(l);
    return result;
}

static size_t line_at_y(const pg_layout *l, double y)
{
    for (size_t i = 0; i < l->count; ++i)
        if (y < l->lines[i].bounds.origin.y + l->lines[i].bounds.size.height)
            return i;
    return l->count - 1;
}

static gr_text_position pg_hit_test(void *data, gr_point point)
{
    pg_layout *l = begin(data);
    const pg_line *line = &l->lines[line_at_y(l, point.y)];
    double left = line->bounds.origin.x, right = left + line->bounds.size.width;
    size_t position;
    if (point.x < left || point.x >= right)
    {
        /* Outside the line: its logical end lies right in left-to-right lines. */
        bool at_end = (point.x >= right) == !line->rtl;
        position = at_end ? line->caret_limit : line->range.start;
    }
    else
    {
        int index = 0, trailing = 0;
        double y = line->bounds.origin.y + line->bounds.size.height / 2;
        pango_layout_xy_to_index(l->layout, to_pango(point.x - l->shift), to_pango(y), &index, &trailing);
        position = (size_t)(g_utf8_offset_to_pointer(l->utf8 + index, trailing) - l->utf8);
    }
    if (position < line->range.start)
        position = line->range.start;
    if (position > line->caret_limit)
        position = line->caret_limit;
    bool upstream = line->soft && position == line->range.end;
    done(l);
    return (gr_text_position){position, upstream ? GR_TEXT_AFFINITY_UPSTREAM : GR_TEXT_AFFINITY_DOWNSTREAM};
}

static const pg_line *line_for_position(const pg_layout *l, gr_text_position p)
{
    for (size_t i = 0; i < l->count; ++i)
    {
        const pg_line *line = &l->lines[i];
        if (p.index < line->range.end ||
            (p.index == line->range.end && line->soft && p.affinity == GR_TEXT_AFFINITY_UPSTREAM))
            return line;
    }
    return &l->lines[l->count - 1];
}

static gr_rect pg_caret_rect(void *data, gr_text_position position)
{
    pg_layout *l = begin(data);
    const pg_line *line = line_for_position(l, position);
    size_t index = position.index < line->caret_limit ? position.index : line->caret_limit;
    bool upstream = position.affinity == GR_TEXT_AFFINITY_UPSTREAM;
    bool has_previous = index > line->range.start, has_next = index < line->stop;
    double x;
    if (l->token && line == &l->lines[l->count - 1] && index >= line->caret_limit)
        /* At the ellipsis: its edge facing the kept text. */
        x = (line->rtl ? l->token_at.x + l->token_width : l->token_at.x) - l->shift;
    else if (has_previous && (upstream || !has_next))
    {
        /* The trailing edge of the grapheme before index; widths are negative for RTL graphemes. */
        int k = chars_before(l, index) - 1;
        while (k > 0 && !l->attrs[k].is_cursor_position)
            --k;
        PangoRectangle grapheme;
        pango_layout_index_to_pos(l->layout, (int)byte_at(l, k), &grapheme);
        x = from_pango(grapheme.x + grapheme.width);
    }
    else if (has_next)
        x = leading_edge(l->layout, index);
    else
    {
        PangoRectangle strong;
        pango_layout_get_cursor_pos(l->layout, (int)index, &strong, NULL);
        x = from_pango(strong.x);
    }
    gr_rect rect = {{x + l->shift, line->bounds.origin.y}, {0, line->bounds.size.height}};
    done(l);
    return rect;
}

static size_t pg_range_rects(void *data, gr_text_range range, gr_rect *rects, size_t capacity)
{
    pg_layout *l = begin(data);
    size_t total = 0;
    for (size_t i = 0; i < l->count; ++i)
    {
        const pg_line *line = &l->lines[i];
        bool tokened = l->token && i + 1 == l->count;
        size_t end = tokened ? line->caret_limit : line->stop;
        size_t a = range.start > line->range.start ? range.start : line->range.start;
        size_t b = range.end < end ? range.end : end;
        if (tokened && range.end > line->caret_limit && range.start < line->range.end)
        {
            if (total < capacity)
                rects[total] =
                    (gr_rect){{l->token_at.x, line->bounds.origin.y}, {l->token_width, line->bounds.size.height}};
            ++total;
        }
        if (a >= b)
            continue;
        int *ranges = NULL, n = 0;
        pango_layout_line_get_x_ranges(pango_layout_get_line_readonly(l->layout, (int)i), (int)a, (int)b, &ranges, &n);
        for (int k = 0; k < n;)
        {
            double lo = from_pango(ranges[2 * k]), hi = from_pango(ranges[2 * k + 1]);
            for (++k; k < n && from_pango(ranges[2 * k]) <= hi + 1e-3; ++k)
                hi = fmax(hi, from_pango(ranges[2 * k + 1]));
            if (hi <= lo)
                continue;
            if (total < capacity)
                rects[total] = (gr_rect){{lo + l->shift, line->bounds.origin.y}, {hi - lo, line->bounds.size.height}};
            ++total;
        }
        g_free(ranges);
    }
    done(l);
    return total;
}

/* Glyphs */

static bool run_color(const PangoItem *item, GdkRGBA *out)
{
    bool colored = false;
    *out = (GdkRGBA){0, 0, 0, 1};
    for (GSList *a = item->analysis.extra_attrs; a; a = a->next)
    {
        PangoAttribute *attribute = a->data;
        if (attribute->klass->type == PANGO_ATTR_FOREGROUND)
        {
            PangoColor c = ((PangoAttrColor *)attribute)->color;
            out->red = c.red / 65535.f;
            out->green = c.green / 65535.f;
            out->blue = c.blue / 65535.f;
            colored = true;
        }
        else if (attribute->klass->type == PANGO_ATTR_FOREGROUND_ALPHA)
            out->alpha = ((PangoAttrInt *)attribute)->value / 65535.f;
    }
    return colored;
}

/* A font face identity for glyphs: the HarfBuzz face (cached per file and index by the font map), design variation
 * coordinates and synthetic bold and slant, without size. The caller holds the system lock. */
static uint64_t font_id(pg_system *s, PangoFont *font)
{
    hb_font_t *hb = pango_font_get_hb_font(font);
    GString *key = g_string_new(NULL);
    g_string_append_printf(key, "%p", (void *)hb_font_get_face(hb));
    unsigned int count = 0;
    const float *coords = hb_font_get_var_coords_design(hb, &count);
    for (unsigned int i = 0; i < count; ++i)
        g_string_append_printf(key, ",%g", coords[i]);
    /* Synthetic bold is a fraction of the em, so it does not depend on size. */
    float x = 0, y = 0;
    hb_bool_t in_place = 0;
    hb_font_get_synthetic_bold(hb, &x, &y, &in_place);
    g_string_append_printf(key, "|%g,%g,%d|%g", x, y, in_place, hb_font_get_synthetic_slant(hb));
    gpointer found = g_hash_table_lookup(s->font_ids, key->str);
    uint64_t id;
    if (found)
    {
        id = *(uint64_t *)found;
        g_string_free(key, TRUE);
    }
    else
    {
        uint64_t *value = g_new(uint64_t, 1);
        *value = id = ++s->next_font;
        g_hash_table_insert(s->font_ids, g_string_free(key, FALSE), value);
    }
    return id;
}

/* Appends run's drawable glyphs, its first glyph's pen at x (layout coordinates) on baseline. Elided glyphs (at or
 * after limit, when a manual ellipsis hides them) are skipped; ellipsis glyphs cover elided. */
static void append_run_glyphs(pg_layout *l, PangoGlyphItem *run, double x, double baseline, size_t limit, bool ellipsis,
                              gr_text_range elided, struct pg_glyphs *out)
{
    PangoItem *item = run->item;
    PangoGlyphString *glyphs = run->glyphs;
    bool rtl = item->analysis.level % 2;
    ellipsis |= (item->analysis.flags & PANGO_ANALYSIS_FLAG_IS_ELLIPSIS) != 0;
    GdkRGBA color;
    bool colored = run_color(item, &color);
    uint64_t face = font_id(l->system, item->analysis.font);
    PangoFontDescription *description = pango_font_describe_with_absolute_size(item->analysis.font);
    double size = from_pango(pango_font_description_get_size(description));
    pango_font_description_free(description);
    size_t item_end = (size_t)(item->offset + item->length);
    double pen = x;
    for (int g = 0; g < glyphs->num_glyphs; ++g)
    {
        PangoGlyphInfo *info = &glyphs->glyphs[g];
        double advance = from_pango(info->geometry.width);
        size_t start = (size_t)(item->offset + glyphs->log_clusters[g]), end = item_end;
        for (int k = 0; k < glyphs->num_glyphs; ++k)
        {
            size_t other = (size_t)(item->offset + glyphs->log_clusters[k]);
            if (other > start && other < end)
                end = other;
        }
        bool hidden = info->glyph == PANGO_GLYPH_EMPTY || (!ellipsis && start >= limit);
        if (!hidden)
        {
            if (out->count == out->capacity)
            {
                out->capacity = out->capacity ? out->capacity * 2 : 16;
                out->data = g_renew(pg_glyph, out->data, out->capacity);
            }
            double gx = pen + from_pango(info->geometry.x_offset), gy = baseline + from_pango(info->geometry.y_offset);
            PangoRectangle ink;
            pango_font_get_glyph_extents(item->analysis.font, info->glyph, &ink, NULL);
            gr_rect bounds = {{gx + from_pango(ink.x), gy + from_pango(ink.y)},
                              {from_pango(ink.width), from_pango(ink.height)}};
            gr_text_range cluster = ellipsis ? elided : (gr_text_range){start, end};
            gr_text_glyph_flags flags = (rtl ? GR_TEXT_GLYPH_RTL : 0) | (ellipsis ? GR_TEXT_GLYPH_ELLIPSIS : 0);
            out->data[out->count++] = (pg_glyph){{{gx, gy}, advance, bounds, cluster, flags, face, size, info->glyph},
                                                 run,
                                                 g,
                                                 {pen, baseline},
                                                 colored,
                                                 color};
        }
        pen += advance;
    }
}

/* Appends the glyphs of the line iter is on, visiting its runs in visual order. */
static void append_line_glyphs(pg_layout *l, PangoLayoutIter *it, double shift, double dy, size_t limit, bool ellipsis,
                               gr_text_range elided, struct pg_glyphs *out)
{
    PangoLayoutLine *line = pango_layout_iter_get_line_readonly(it);
    double baseline = from_pango(pango_layout_iter_get_baseline(it)) + dy;
    do
    {
        PangoGlyphItem *run = pango_layout_iter_get_run_readonly(it);
        if (!run)
            continue;
        PangoRectangle logical;
        pango_layout_iter_get_run_extents(it, NULL, &logical);
        append_run_glyphs(l, run, from_pango(logical.x) + shift, baseline, limit, ellipsis, elided, out);
    } while (pango_layout_iter_next_run(it) && pango_layout_iter_get_line_readonly(it) == line);
}

/* Builds every line's glyphs; the caller holds the system lock. */
static void build_glyphs(pg_layout *l)
{
    if (l->glyphs)
        return;
    l->glyphs = g_new0(struct pg_glyphs, l->count);
    PangoLayoutIter *it = pango_layout_get_iter(l->layout);
    for (size_t i = 0; i < l->count; ++i)
    {
        const pg_line *line = &l->lines[i];
        bool tokened = l->token && i + 1 == l->count;
        gr_text_range elided = {line->caret_limit, line->range.end};
        size_t limit = tokened ? line->caret_limit : SIZE_MAX;
        append_line_glyphs(l, it, l->shift, 0, limit, false, elided, &l->glyphs[i]);
        if (tokened)
        {
            PangoLayoutIter *token = pango_layout_get_iter(l->token);
            /* append_line_glyphs adds the token's baseline; place its box top-left at token_at. */
            append_line_glyphs(l, token, l->token_at.x, l->token_at.y, SIZE_MAX, true, elided, &l->glyphs[i]);
            pango_layout_iter_free(token);
        }
        if (i + 1 < l->count)
            while (pango_layout_iter_get_line_readonly(it) == pango_layout_get_line_readonly(l->layout, (int)i) &&
                   pango_layout_iter_next_line(it))
                ;
    }
    pango_layout_iter_free(it);
}

static size_t pg_glyphs(void *data, size_t line, gr_text_glyph *glyphs, size_t capacity)
{
    pg_layout *l = begin(data);
    build_glyphs(l);
    const struct pg_glyphs *list = &l->glyphs[line];
    for (size_t i = 0; i < list->count && i < capacity; ++i)
        glyphs[i] = list->data[i].info;
    size_t count = list->count;
    done(l);
    return count;
}

void gr_pango_draw_glyphs(GtkSnapshot *s, gr_text_layout *layout, size_t line, gr_glyph_range range, gr_point origin,
                          gr_color color)
{
    pg_layout *l = begin(gr_text_layout_get_data(layout));
    build_glyphs(l);
    const struct pg_glyphs *list = &l->glyphs[line];
    size_t end = range.end < list->count ? range.end : list->count;
    GdkRGBA fill = gr_gdk_color(color);
    for (size_t i = range.start; i < end;)
    {
        /* Glyphs adjacent within one run become one text node, keeping their geometry. */
        const pg_glyph *first = &list->data[i];
        int n = 1;
        while (i + (size_t)n < end && list->data[i + n].run == first->run &&
               list->data[i + n].index == first->index + n)
            ++n;
        PangoGlyphString *glyphs = pango_glyph_string_new();
        pango_glyph_string_set_size(glyphs, n);
        memcpy(glyphs->glyphs, first->run->glyphs->glyphs + first->index, (size_t)n * sizeof(*glyphs->glyphs));
        memcpy(glyphs->log_clusters, first->run->glyphs->log_clusters + first->index, (size_t)n * sizeof(int));
        graphene_point_t at = {(float)(origin.x + first->pen.x), (float)(origin.y + first->pen.y)};
        GskRenderNode *node =
            gsk_text_node_new(first->run->item->analysis.font, glyphs, first->colored ? &first->color : &fill, &at);
        if (node)
        {
            gtk_snapshot_append_node(s, node);
            gsk_render_node_unref(node);
        }
        pango_glyph_string_free(glyphs);
        i += (size_t)n;
    }
    done(l);
}

static void pg_layout_destroy(void *data)
{
    pg_layout *l = data;
    for (size_t i = 0; l->glyphs && i < l->count; ++i)
        g_free(l->glyphs[i].data);
    g_free(l->glyphs);
    if (l->layout)
        g_object_unref(l->layout);
    if (l->token)
        g_object_unref(l->token);
    g_object_unref(l->context);
    g_free(l->lines);
    g_free(l->utf8);
    g_free(l);
}

const gr_text_layout_vtable gr_pango_layout_vtable = {
    pg_metrics,    pg_line_info,   pg_next_stop, pg_previous_stop,  pg_hit_test,
    pg_caret_rect, pg_range_rects, pg_glyphs,    pg_layout_destroy,
};

/* Drawing */

void gr_pango_draw_layout(GtkSnapshot *s, gr_text_layout *layout, gr_point origin, gr_color color)
{
    pg_layout *l = begin(gr_text_layout_get_data(layout));
    gtk_snapshot_save(s);
    graphene_point_t at = {(float)(origin.x + l->shift), (float)origin.y};
    gtk_snapshot_translate(s, &at);
    if (l->clip_height > 0)
    {
        /* Generous horizontally: only the cut lines below should disappear. */
        graphene_rect_t clip = GRAPHENE_RECT_INIT(-1e5f, -1e5f, 2e5f, (float)(1e5 + l->clip_height));
        gtk_snapshot_push_clip(s, &clip);
    }
    GdkRGBA rgba = gr_gdk_color(color);
    if (isnan(l->cutoff))
        gtk_snapshot_append_layout(s, l->layout, &rgba);
    else
    {
        /* Lines above the last one, then the last line's kept side of the cutoff. */
        const pg_line *last = &l->lines[l->count - 1];
        float top = (float)last->bounds.origin.y, cut = (float)(l->cutoff - l->shift);
        graphene_rect_t above = GRAPHENE_RECT_INIT(-1e5f, -1e5f, 2e5f, 1e5f + top);
        graphene_rect_t kept = l->cutoff_left
                                   ? GRAPHENE_RECT_INIT(cut, top, 1e5f, (float)last->bounds.size.height)
                                   : GRAPHENE_RECT_INIT(-1e5f, top, 1e5f + cut, (float)last->bounds.size.height);
        gtk_snapshot_push_clip(s, &above);
        gtk_snapshot_append_layout(s, l->layout, &rgba);
        gtk_snapshot_pop(s);
        gtk_snapshot_push_clip(s, &kept);
        gtk_snapshot_append_layout(s, l->layout, &rgba);
        gtk_snapshot_pop(s);
    }
    if (l->clip_height > 0)
        gtk_snapshot_pop(s);
    if (l->token)
    {
        graphene_point_t token = {(float)(l->token_at.x - l->shift), (float)l->token_at.y};
        gtk_snapshot_translate(s, &token);
        gtk_snapshot_append_layout(s, l->token, l->token_colored ? &l->token_color : &rgba);
    }
    gtk_snapshot_restore(s);
    done(l);
}

/* Text system */

static gr_text_layout *pg_layout_text(void *data, gr_attributed_text *text, const gr_text_layout_options *options)
{
    pg_system *system = data;
    gr_string utf8 = gr_attributed_text_get_utf8(text);
    pg_layout *l = g_new0(pg_layout, 1);
    l->system = system;
    l->text = text;
    l->options = *options;
    l->length = utf8.count;
    l->utf8 = g_malloc(utf8.count + 1);
    if (utf8.count)
        memcpy(l->utf8, utf8.data, utf8.count);
    l->utf8[utf8.count] = 0;
    for (size_t i = 0; i < utf8.count; ++i)
        if (!l->utf8[i])
            l->utf8[i] = ' ';

    g_mutex_lock(&system->lock);
    l->context = pango_font_map_create_context(system->font_map);
    g_mutex_unlock(&system->lock);
    pango_context_set_round_glyph_positions(l->context, FALSE);
    cairo_font_options_t *font_options = cairo_font_options_create();
    cairo_font_options_set_hint_metrics(font_options, CAIRO_HINT_METRICS_OFF);
    cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_GRAY);
    pango_cairo_context_set_font_options(l->context, font_options);
    cairo_font_options_destroy(font_options);
    if (options->direction != GR_TEXT_DIRECTION_AUTO)
        pango_context_set_base_dir(l->context, options->direction == GR_TEXT_DIRECTION_RTL ? PANGO_DIRECTION_RTL
                                                                                           : PANGO_DIRECTION_LTR);

    gr_text_layout *layout = gr_text_layout_new(system->handle, text, options, &gr_pango_layout_vtable, l);
    if (!layout)
        pg_layout_destroy(l);
    return layout;
}

static void pg_system_destroy(void *data)
{
    pg_system *system = data;
    g_object_unref(system->font_map);
    g_hash_table_destroy(system->font_ids);
    g_free(system->system_family);
    g_mutex_clear(&system->lock);
    g_free(system);
}

gr_text_system *gr_gtk_text_system_new(void)
{
    pg_system *system = g_new0(pg_system, 1);
    g_mutex_init(&system->lock);
    system->font_map = pango_cairo_font_map_new();
    system->font_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    system->system_family = g_strdup("sans-serif");
    GtkSettings *settings = gtk_settings_get_default();
    if (settings)
    {
        char *name = NULL;
        g_object_get(settings, "gtk-font-name", &name, NULL);
        if (name)
        {
            PangoFontDescription *d = pango_font_description_from_string(name);
            const char *family = pango_font_description_get_family(d);
            if (family && *family)
            {
                g_free(system->system_family);
                system->system_family = g_strdup(family);
            }
            pango_font_description_free(d);
            g_free(name);
        }
    }
    gr_text_system_vtable vtable = {pg_layout_text, pg_system_destroy};
    system->handle = gr_text_system_new(&vtable, system);
    return system->handle;
}
