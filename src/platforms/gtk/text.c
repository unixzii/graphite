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
    char *system_family; /* GTK's interface font family, used for "system-ui". */
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
    double clip_height; /* > 0 hides lines cut by max_lines without an ellipsis. */
    pg_line *lines;
    size_t count;
    const PangoLogAttr *attrs;
    int attr_count;
    gr_text_layout_metrics metrics;
} pg_layout;

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
    pg_line *first = &lines[0], *last = &lines[visible - 1];
    double height = last->bounds.origin.y + last->bounds.size.height;
    if (cut && !height_cut)
        l->clip_height = height;
    l->lines = lines;
    l->count = visible;
    l->attrs = pango_layout_get_log_attrs_readonly(layout, &l->attr_count);
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
    double x;
    if (upstream && line->soft && index == line->range.end && index > line->range.start)
    {
        /* The trailing edge of the last grapheme on the earlier line; widths are negative for RTL. */
        const char *previous = g_utf8_find_prev_char(l->utf8, l->utf8 + index);
        PangoRectangle grapheme;
        pango_layout_index_to_pos(l->layout, (int)(previous - l->utf8), &grapheme);
        x = from_pango(grapheme.x + grapheme.width);
    }
    else
    {
        PangoRectangle strong, weak;
        pango_layout_get_cursor_pos(l->layout, (int)index, &strong, &weak);
        bool interior = index > line->range.start && index < line->caret_limit;
        x = from_pango(upstream && interior ? weak.x : strong.x);
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
        size_t a = range.start > line->range.start ? range.start : line->range.start;
        size_t b = range.end < line->stop ? range.end : line->stop;
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

static void pg_layout_destroy(void *data)
{
    pg_layout *l = data;
    if (l->layout)
        g_object_unref(l->layout);
    g_object_unref(l->context);
    g_free(l->lines);
    g_free(l->utf8);
    g_free(l);
}

const gr_text_layout_vtable gr_pango_layout_vtable = {
    pg_metrics,  pg_line_info,  pg_next_stop,   pg_previous_stop,
    pg_hit_test, pg_caret_rect, pg_range_rects, pg_layout_destroy,
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
    gtk_snapshot_append_layout(s, l->layout, &rgba);
    if (l->clip_height > 0)
        gtk_snapshot_pop(s);
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
    g_free(system->system_family);
    g_mutex_clear(&system->lock);
    g_free(system);
}

gr_text_system *gr_gtk_text_system_new(void)
{
    pg_system *system = g_new0(pg_system, 1);
    g_mutex_init(&system->lock);
    system->font_map = pango_cairo_font_map_new();
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
