#include "internal.h"

static const char system_ui[] = "system-ui";

/* Attributed text */

struct gr_attributed_text
{
    atomic_size_t refs;
    uint64_t id;
    gr_string utf8;
    size_t utf16_count;
    atomic_uintptr_t utf16; /* uint16_t array built on first use, then immutable. */
    char *families;
    gr_text_style base;
    gr_text_span_list spans;
};

/* Returns the length of the valid UTF-8 sequence at s[i], or 0. */
static size_t utf8_sequence(const unsigned char *s, size_t count, size_t i)
{
    unsigned char c = s[i];
    if (c < 0x80)
        return 1;
    size_t length;
    uint32_t code, min;
    if (c >= 0xC2 && c <= 0xDF)
    {
        length = 2;
        min = 0x80;
    }
    else if ((c & 0xF0) == 0xE0)
    {
        length = 3;
        min = 0x800;
    }
    else if (c >= 0xF0 && c <= 0xF4)
    {
        length = 4;
        min = 0x10000;
    }
    else
        return 0;
    code = c & (0x7Fu >> length);
    if (count - i < length)
        return 0;
    for (size_t k = 1; k < length; ++k)
    {
        if ((s[i + k] & 0xC0) != 0x80)
            return 0;
        code = code << 6 | (s[i + k] & 0x3Fu);
    }
    if (code < min || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        return 0;
    return length;
}

/* Sequence length from the lead byte of validated UTF-8. */
static size_t utf8_length(unsigned char lead)
{
    return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
}

static bool valid_utf8(gr_string s, size_t *utf16_count)
{
    if (s.count && !s.data)
        return false;
    const unsigned char *bytes = (const unsigned char *)s.data;
    size_t units = 0;
    for (size_t i = 0; i < s.count;)
    {
        size_t length = utf8_sequence(bytes, s.count, i);
        if (!length)
            return false;
        units += length == 4 ? 2 : 1;
        i += length;
    }
    if (utf16_count)
        *utf16_count = units;
    return true;
}

static bool is_boundary(gr_string s, size_t index)
{
    return index == s.count || (index < s.count && ((unsigned char)s.data[index] & 0xC0) != 0x80);
}

static bool valid_style(const gr_text_style *style, gr_text_style_fields fields)
{
    if ((fields & GR_TEXT_STYLE_FAMILY) && !valid_utf8(style->family, NULL))
        return false;
    if ((fields & GR_TEXT_STYLE_SIZE) && (!isfinite(style->size) || style->size <= 0))
        return false;
    if ((fields & GR_TEXT_STYLE_WEIGHT) && (style->weight < 1 || style->weight > 1000))
        return false;
    if ((fields & GR_TEXT_STYLE_SLANT) && (style->slant < GR_FONT_SLANT_NORMAL || style->slant > GR_FONT_SLANT_ITALIC))
        return false;
    if ((fields & GR_TEXT_STYLE_COLOR) && !gr_valid_color(style->color))
        return false;
    if ((fields & GR_TEXT_STYLE_DECORATIONS) &&
        (style->decorations & ~(gr_text_decorations)(GR_TEXT_DECORATION_UNDERLINE | GR_TEXT_DECORATION_STRIKETHROUGH)))
        return false;
    if ((fields & GR_TEXT_STYLE_LETTER_SPACING) && !isfinite(style->letter_spacing))
        return false;
    return true;
}

static bool active_span(const gr_text_span *span)
{
    return span->fields && span->range.start < span->range.end;
}

/* Copies a family into storage at *offset, mapping empty to "system-ui". */
static gr_string copy_family(gr_string family, char *storage, size_t *offset)
{
    if (!family.count)
        return (gr_string){system_ui, sizeof(system_ui) - 1};
    char *copy = storage + *offset;
    memcpy(copy, family.data, family.count);
    *offset += family.count;
    return (gr_string){copy, family.count};
}

/* Returns base with the span's selected fields applied; family must already be copied. */
static gr_text_style overlay(const gr_text_style *base, const gr_text_span *span, gr_string family)
{
    gr_text_style result = *base;
    const gr_text_style *s = &span->style;
    if (span->fields & GR_TEXT_STYLE_FAMILY)
        result.family = family;
    if (span->fields & GR_TEXT_STYLE_SIZE)
        result.size = s->size;
    if (span->fields & GR_TEXT_STYLE_WEIGHT)
        result.weight = s->weight;
    if (span->fields & GR_TEXT_STYLE_SLANT)
        result.slant = s->slant;
    if (span->fields & GR_TEXT_STYLE_COLOR)
        result.color = s->color;
    if (span->fields & GR_TEXT_STYLE_DECORATIONS)
        result.decorations = s->decorations;
    if (span->fields & GR_TEXT_STYLE_LETTER_SPACING)
        result.letter_spacing = s->letter_spacing;
    return result;
}

gr_text_style gr_text_style_default(void)
{
    return (gr_text_style){.family = {system_ui, sizeof(system_ui) - 1},
                           .size = 14,
                           .weight = 400,
                           .slant = GR_FONT_SLANT_NORMAL,
                           .color = {0, 0, 0, 1}};
}

gr_attributed_text *gr_attributed_text_new(gr_string utf8, const gr_text_style *base, gr_text_span_list spans)
{
    size_t utf16_count;
    if (!base || !valid_utf8(utf8, &utf16_count) || !valid_style(base, GR_TEXT_STYLE_ALL & ~GR_TEXT_STYLE_COLOR) ||
        (spans.count && !spans.data))
        return NULL;
    size_t family_bytes = base->family.count, active = 0;
    for (size_t i = 0; i < spans.count; ++i)
    {
        const gr_text_span *span = &spans.data[i];
        if ((span->fields & ~(gr_text_style_fields)GR_TEXT_STYLE_ALL) || span->range.start > span->range.end ||
            !is_boundary(utf8, span->range.start) || !is_boundary(utf8, span->range.end))
            return NULL;
        if (!active_span(span))
            continue;
        if (!valid_style(&span->style, span->fields))
            return NULL;
        ++active;
        if (span->fields & GR_TEXT_STYLE_FAMILY)
        {
            if (span->style.family.count > SIZE_MAX - family_bytes)
                abort();
            family_bytes += span->style.family.count;
        }
    }
    gr_attributed_text *t = gr_alloc(1, sizeof(*t));
    atomic_init(&t->refs, 1);
    atomic_init(&t->utf16, (uintptr_t)0);
    t->id = gr_next_id();
    t->utf8 = (gr_string){gr_copy(utf8.data, utf8.count, 1), utf8.count};
    t->utf16_count = utf16_count;
    t->families = gr_alloc(family_bytes, 1);
    size_t offset = 0;
    t->base = *base;
    t->base.family = copy_family(base->family, t->families, &offset);
    t->base.color = (gr_color){0, 0, 0, 1};
    gr_text_span *copies = gr_alloc(active, sizeof(*copies));
    size_t count = 0;
    for (size_t i = 0; i < spans.count; ++i)
    {
        const gr_text_span *span = &spans.data[i];
        if (!active_span(span))
            continue;
        gr_string family = t->base.family;
        if (span->fields & GR_TEXT_STYLE_FAMILY)
            family = copy_family(span->style.family, t->families, &offset);
        copies[count++] = (gr_text_span){span->range, span->fields, overlay(&t->base, span, family)};
    }
    t->spans = (gr_text_span_list){copies, count};
    return t;
}

gr_attributed_text *gr_attributed_text_ref(gr_attributed_text *t)
{
    if (t)
        gr_shared_ref(&t->refs);
    return t;
}

void gr_attributed_text_unref(gr_attributed_text *t)
{
    if (!t || !gr_shared_unref(&t->refs))
        return;
    free((void *)atomic_load_explicit(&t->utf16, memory_order_acquire));
    free((void *)t->spans.data);
    free(t->families);
    free((void *)t->utf8.data);
    free(t);
}

gr_string gr_attributed_text_get_utf8(const gr_attributed_text *t)
{
    return t->utf8;
}

uint64_t gr_attributed_text_get_id(const gr_attributed_text *t)
{
    return t->id;
}

gr_text_style gr_attributed_text_get_base(const gr_attributed_text *t)
{
    return t->base;
}

gr_text_span_list gr_attributed_text_get_spans(const gr_attributed_text *t)
{
    return t->spans;
}

gr_utf16 gr_attributed_text_get_utf16(const gr_attributed_text *t)
{
    gr_attributed_text *mutable_text = (gr_attributed_text *)t;
    uintptr_t built = atomic_load_explicit(&mutable_text->utf16, memory_order_acquire);
    if (!built && t->utf16_count)
    {
        uint16_t *units = gr_alloc(t->utf16_count, sizeof(*units));
        const unsigned char *s = (const unsigned char *)t->utf8.data;
        size_t u = 0;
        for (size_t i = 0; i < t->utf8.count;)
        {
            size_t length = utf8_length(s[i]);
            uint32_t code = length == 1 ? s[i] : s[i] & (0x7Fu >> length);
            for (size_t k = 1; k < length; ++k)
                code = code << 6 | (s[i + k] & 0x3Fu);
            if (code >= 0x10000)
            {
                code -= 0x10000;
                units[u++] = (uint16_t)(0xD800 + (code >> 10));
                units[u++] = (uint16_t)(0xDC00 + (code & 0x3FF));
            }
            else
                units[u++] = (uint16_t)code;
            i += length;
        }
        uintptr_t expected = 0;
        if (atomic_compare_exchange_strong_explicit(&mutable_text->utf16, &expected, (uintptr_t)units,
                                                    memory_order_acq_rel, memory_order_acquire))
            built = (uintptr_t)units;
        else
        {
            free(units);
            built = expected;
        }
    }
    return (gr_utf16){(const uint16_t *)built, t->utf16_count};
}

size_t gr_attributed_text_utf16_index(const gr_attributed_text *t, size_t utf8_index)
{
    if (utf8_index >= t->utf8.count)
        return t->utf16_count;
    if (t->utf16_count == t->utf8.count)
        return utf8_index;
    const unsigned char *s = (const unsigned char *)t->utf8.data;
    size_t u = 0;
    for (size_t i = 0; i < utf8_index;)
    {
        size_t length = utf8_length(s[i]);
        if (i + length > utf8_index)
            break;
        u += length == 4 ? 2 : 1;
        i += length;
    }
    return u;
}

size_t gr_attributed_text_utf8_index(const gr_attributed_text *t, size_t utf16_index)
{
    if (utf16_index >= t->utf16_count)
        return t->utf8.count;
    if (t->utf16_count == t->utf8.count)
        return utf16_index;
    const unsigned char *s = (const unsigned char *)t->utf8.data;
    size_t u = 0, i = 0;
    while (i < t->utf8.count)
    {
        size_t length = utf8_length(s[i]);
        size_t width = length == 4 ? 2 : 1;
        if (u + width > utf16_index)
            break;
        u += width;
        i += length;
    }
    return i;
}

/* Layout options */

gr_text_layout_options gr_text_layout_options_default(void)
{
    return (gr_text_layout_options){.wrap = GR_TEXT_WRAP_WORD,
                                    .truncation = GR_TEXT_TRUNCATE_NONE,
                                    .align = GR_TEXT_ALIGN_START,
                                    .direction = GR_TEXT_DIRECTION_AUTO};
}

static bool valid_options(const gr_text_layout_options *o)
{
    return o && (!o->has_max_width || (isfinite(o->max_width) && o->max_width >= 0)) && o->wrap >= GR_TEXT_WRAP_WORD &&
           o->wrap <= GR_TEXT_WRAP_NONE && o->truncation >= GR_TEXT_TRUNCATE_NONE &&
           o->truncation <= GR_TEXT_TRUNCATE_END && o->align >= GR_TEXT_ALIGN_START && o->align <= GR_TEXT_ALIGN_END &&
           o->direction >= GR_TEXT_DIRECTION_AUTO && o->direction <= GR_TEXT_DIRECTION_RTL;
}

/* Text systems */

struct gr_text_system
{
    atomic_size_t refs;
    gr_text_system_vtable vtable;
    void *userdata;
};

gr_text_system *gr_text_system_new(const gr_text_system_vtable *vtable, void *userdata)
{
    if (!vtable || !vtable->layout || !vtable->destroy)
        return NULL;
    gr_text_system *s = gr_alloc(1, sizeof(*s));
    atomic_init(&s->refs, 1);
    s->vtable = *vtable;
    s->userdata = userdata;
    return s;
}

gr_text_system *gr_text_system_ref(gr_text_system *s)
{
    if (s)
        gr_shared_ref(&s->refs);
    return s;
}

void gr_text_system_unref(gr_text_system *s)
{
    if (!s || !gr_shared_unref(&s->refs))
        return;
    s->vtable.destroy(s->userdata);
    free(s);
}

/* Text layouts */

struct gr_text_layout
{
    atomic_size_t refs;
    uint64_t id;
    gr_text_system *system;
    gr_attributed_text *text;
    gr_text_layout_options options;
    const gr_text_layout_vtable *identity;
    gr_text_layout_vtable vtable;
    void *userdata;
};

gr_text_layout *gr_text_system_layout(gr_text_system *s, gr_attributed_text *text,
                                      const gr_text_layout_options *options)
{
    if (!text || !valid_options(options))
        return NULL;
    gr_text_layout *l = s->vtable.layout(s->userdata, text, options);
    if (l && (l->system != s || l->text != text))
        abort();
    return l;
}

gr_text_layout *gr_text_layout_new(gr_text_system *system, gr_attributed_text *text,
                                   const gr_text_layout_options *options, const gr_text_layout_vtable *vtable,
                                   void *userdata)
{
    if (!system || !text || !valid_options(options) || !vtable || !vtable->get_metrics || !vtable->get_line ||
        !vtable->next_caret_stop || !vtable->previous_caret_stop || !vtable->hit_test || !vtable->get_caret_rect ||
        !vtable->get_range_rects || !vtable->get_glyphs || !vtable->destroy)
        return NULL;
    gr_text_layout *l = gr_alloc(1, sizeof(*l));
    atomic_init(&l->refs, 1);
    l->id = gr_next_id();
    l->system = gr_text_system_ref(system);
    l->text = gr_attributed_text_ref(text);
    l->options = *options;
    l->identity = vtable;
    l->vtable = *vtable;
    l->userdata = userdata;
    return l;
}

gr_text_layout *gr_text_layout_ref(gr_text_layout *l)
{
    if (l)
        gr_shared_ref(&l->refs);
    return l;
}

void gr_text_layout_unref(gr_text_layout *l)
{
    if (!l || !gr_shared_unref(&l->refs))
        return;
    l->vtable.destroy(l->userdata);
    gr_attributed_text_unref(l->text);
    gr_text_system_unref(l->system);
    free(l);
}

gr_attributed_text *gr_text_layout_get_text(const gr_text_layout *l)
{
    return l->text;
}

gr_text_system *gr_text_layout_get_system(const gr_text_layout *l)
{
    return l->system;
}

gr_text_layout_options gr_text_layout_get_options(const gr_text_layout *l)
{
    return l->options;
}

uint64_t gr_text_layout_get_id(const gr_text_layout *l)
{
    return l->id;
}

const gr_text_layout_vtable *gr_text_layout_get_vtable(const gr_text_layout *l)
{
    return l->identity;
}

void *gr_text_layout_get_data(const gr_text_layout *l)
{
    return l->userdata;
}

gr_text_layout_metrics gr_text_layout_get_metrics(const gr_text_layout *l)
{
    return l->vtable.get_metrics(l->userdata);
}

gr_result gr_text_layout_get_line(const gr_text_layout *l, size_t index, gr_text_line *out)
{
    if (!out)
        return GR_ERROR_ARGUMENT;
    *out = (gr_text_line){0};
    if (index >= l->vtable.get_metrics(l->userdata).line_count)
        return GR_ERROR_ARGUMENT;
    l->vtable.get_line(l->userdata, index, out);
    return GR_OK;
}

/* Carets and hit testing */

static size_t visible_end(const gr_text_layout *l)
{
    size_t end = l->vtable.get_metrics(l->userdata).visible.end;
    return end < l->text->utf8.count ? end : l->text->utf8.count;
}

/* Moves index back to a caret stop. Stops have no stop strictly between them and their previous stop's successor. */
static size_t floor_stop(const gr_text_layout *l, size_t index, size_t end)
{
    if (index >= end)
        return end;
    if (!index)
        return 0;
    size_t previous = l->vtable.previous_caret_stop(l->userdata, index);
    return l->vtable.next_caret_stop(l->userdata, previous) == index ? index : previous;
}

size_t gr_text_layout_next_caret_stop(const gr_text_layout *l, size_t index)
{
    size_t end = visible_end(l);
    if (index >= end)
        return end;
    size_t next = l->vtable.next_caret_stop(l->userdata, index);
    return next < end ? next : end;
}

size_t gr_text_layout_previous_caret_stop(const gr_text_layout *l, size_t index)
{
    size_t stop = floor_stop(l, index, visible_end(l));
    return stop ? l->vtable.previous_caret_stop(l->userdata, stop) : 0;
}

gr_text_position gr_text_layout_hit_test(const gr_text_layout *l, gr_point point)
{
    if (!gr_valid_point(point))
        return (gr_text_position){0, GR_TEXT_AFFINITY_DOWNSTREAM};
    return l->vtable.hit_test(l->userdata, point);
}

gr_result gr_text_layout_get_caret_rect(const gr_text_layout *l, gr_text_position position, gr_rect *out)
{
    if (!out)
        return GR_ERROR_ARGUMENT;
    *out = (gr_rect){{0, 0}, {0, 0}};
    if (position.affinity < GR_TEXT_AFFINITY_DOWNSTREAM || position.affinity > GR_TEXT_AFFINITY_UPSTREAM)
        return GR_ERROR_ARGUMENT;
    position.index = floor_stop(l, position.index, visible_end(l));
    *out = l->vtable.get_caret_rect(l->userdata, position);
    return GR_OK;
}

size_t gr_text_layout_get_range_rects(const gr_text_layout *l, gr_text_range range, gr_rect *rects, size_t capacity)
{
    if (!rects)
        capacity = 0;
    if (range.start >= range.end)
        return 0;
    size_t end = visible_end(l);
    size_t start = floor_stop(l, range.start, end);
    size_t stop = end;
    if (range.end < end)
    {
        stop = floor_stop(l, range.end, end);
        if (stop != range.end)
            stop = l->vtable.next_caret_stop(l->userdata, range.end);
    }
    if (stop > end)
        stop = end;
    if (start >= stop)
        return 0;
    return l->vtable.get_range_rects(l->userdata, (gr_text_range){start, stop}, capacity ? rects : NULL, capacity);
}

/* Glyphs */

size_t gr_text_layout_get_glyphs(const gr_text_layout *l, size_t line, gr_text_glyph *glyphs, size_t capacity)
{
    if (!glyphs)
        capacity = 0;
    if (line >= l->vtable.get_metrics(l->userdata).line_count)
        return 0;
    return l->vtable.get_glyphs(l->userdata, line, capacity ? glyphs : NULL, capacity);
}
