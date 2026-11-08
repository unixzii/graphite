#include "internal.hpp"

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <icu.h>
#include <mutex>
#include <string>

/* DirectWrite text system.
 *
 * DirectWrite range setters override earlier calls on overlapping ranges, so spans apply natively in order. Colors
 * become drawing effects only while drawing, because D2D needs brushes from the drawing device. DirectWrite has no
 * line limit: a max height covering the first max_lines lines with an ellipsis trimming sign trims the last of them
 * and hides the rest (height 0); without an ellipsis the extra lines are clipped. Its cluster metrics split flags,
 * so caret stops come from ICU character breaks, and automatic direction uses ICU's first strong character (reading
 * direction is layout-wide in DirectWrite). IDWriteTextLayout is mutable, so a mutex per layout serializes it.
 */

namespace gr_win
{
namespace
{
struct TextSystem
{
    gr_text_system *handle = nullptr; // Weak: the handle owns this userdata.
    com_ptr<IDWriteFactory> factory;
    std::mutex fonts;
    std::unordered_map<std::string, uint64_t> font_ids; // Face identity key -> glyph font identity.
    uint64_t next_font = 0;
};

/* A font face identity for glyphs: font files (loader and reference key), face index, simulations (synthetic bold
 * or oblique) and variation axis values, without size. */
uint64_t font_id(TextSystem *s, IDWriteFontFace *face)
{
    std::string key;
    UINT32 count = 0;
    check_hresult(face->GetFiles(&count, nullptr));
    std::vector<IDWriteFontFile *> files(count);
    check_hresult(face->GetFiles(&count, files.data()));
    for (auto file : files)
    {
        void const *reference = nullptr;
        UINT32 size = 0;
        com_ptr<IDWriteFontFileLoader> loader;
        if (SUCCEEDED(file->GetReferenceKey(&reference, &size)) && SUCCEEDED(file->GetLoader(loader.put())))
        {
            key += std::to_string(reinterpret_cast<uintptr_t>(loader.get())) + ":";
            key.append(static_cast<char const *>(reference), size);
            key += "|";
        }
        file->Release();
    }
    key += std::to_string(face->GetIndex()) + "|" + std::to_string(face->GetSimulations());
    com_ptr<IDWriteFontFace5> variable;
    if (SUCCEEDED(face->QueryInterface(IID_PPV_ARGS(variable.put()))))
    {
        std::vector<DWRITE_FONT_AXIS_VALUE> axes(variable->GetFontAxisValueCount());
        if (!axes.empty() && SUCCEEDED(variable->GetFontAxisValues(axes.data(), static_cast<UINT32>(axes.size()))))
            for (auto const &axis : axes)
                key += "|" + std::to_string(axis.axisTag) + "=" + std::to_string(axis.value);
    }
    std::lock_guard<std::mutex> guard(s->fonts);
    auto [it, added] = s->font_ids.try_emplace(key, 0);
    if (added)
        it->second = ++s->next_font;
    return it->second;
}

struct Line
{
    gr_text_range range;
    gr_rect bounds;
    double baseline, ascent, descent;
    UINT32 start, end; // UTF-16 range, including separators and elided text.
    UINT32 stop;       // End of the text before any paragraph separator.
    UINT32 caret;      // Last caret position: stop, end for soft breaks, or where elided text starts.
    bool soft;
};

struct ColorSpan
{
    DWRITE_TEXT_RANGE range;
    gr_color color;
};

/* A glyph run copied from IDWriteTextLayout::Draw. */
struct GlyphRun
{
    com_ptr<IDWriteFontFace> face;
    float size = 0;
    BOOL sideways = FALSE;
    UINT32 level = 0;
    std::vector<UINT16> ids;
    std::vector<float> advances;
    std::vector<DWRITE_GLYPH_OFFSET> offsets;
    std::vector<UINT16> cluster_map; // Empty for the ellipsis, whose glyphs have no text.
    UINT32 text = 0;                 // UTF-16 position of the run's text.
    D2D1_POINT_2F origin{};          // Baseline origin, layout coordinates.
    bool ellipsis = false;
};

struct GlyphRef
{
    gr_text_glyph info;
    size_t run;
    UINT32 index;                  // Glyph index within the run.
    std::optional<gr_color> color; // From a color span or the ellipsis; otherwise the draw color.
};

struct TextLayout
{
    TextSystem *system = nullptr;
    gr_attributed_text *text = nullptr; // Retained by the owning gr_text_layout.
    gr_text_layout_options options{};
    std::mutex lock;
    bool ready = false;
    com_ptr<IDWriteTextLayout> layout;
    std::vector<ColorSpan> colors;
    std::vector<Line> lines;
    std::vector<UINT32> stops; // Grapheme cluster boundaries in UTF-16, including 0 and the end.
    double clip_height = 0;    // > 0 hides lines cut by max_lines below the last visible line.
    bool rtl = false;
    // An ellipsis DirectWrite did not add: the cut fell at a paragraph break or between unwrapped lines.
    com_ptr<IDWriteTextLayout> token;
    gr_point token_at{}; // Top-left in layout coordinates.
    double token_width = 0;
    double cutoff = NAN; // NAN, or the x beyond which the last line's glyphs are hidden.
    std::optional<gr_color> token_color;
    gr_text_layout_metrics metrics{};
    bool glyphs_ready = false;
    std::vector<GlyphRun> runs;
    std::vector<std::vector<GlyphRef>> glyphs; // Per visible line, in visual order.
};

std::wstring family_name(gr_string family)
{
    auto is = [&](char const *name)
    { return family.count == strlen(name) && !memcmp(family.data, name, family.count); };
    if (is("system-ui") || is("sans-serif"))
        return L"Segoe UI";
    if (is("serif"))
        return L"Times New Roman";
    if (is("monospace"))
        return L"Consolas";
    int n = MultiByteToWideChar(CP_UTF8, 0, family.data, static_cast<int>(family.count), nullptr, 0);
    std::wstring result(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, family.data, static_cast<int>(family.count), result.data(), n);
    return result;
}

DWRITE_FONT_STYLE font_style(gr_font_slant slant)
{
    return slant == GR_FONT_SLANT_ITALIC ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
}

UINT32 to16(TextLayout const *l, size_t index)
{
    return static_cast<UINT32>(gr_attributed_text_utf16_index(l->text, index));
}

size_t to8(TextLayout const *l, UINT32 index)
{
    return gr_attributed_text_utf8_index(l->text, index);
}

bool paragraph_break(uint16_t c)
{
    return c == '\n' || c == '\r' || c == 0x2029;
}

/* End of the paragraph containing index, just after its separator. */
UINT32 paragraph_end(gr_utf16 u, UINT32 index)
{
    for (UINT32 i = index; i < u.count; ++i)
        if (paragraph_break(u.data[i]))
            return i + (u.data[i] == '\r' && i + 1 < u.count && u.data[i + 1] == '\n' ? 2 : 1);
    return static_cast<UINT32>(u.count);
}

void apply_style(IDWriteTextLayout *layout, gr_text_style const &s, gr_text_style_fields fields, DWRITE_TEXT_RANGE r,
                 std::vector<ColorSpan> &colors)
{
    if (fields & GR_TEXT_STYLE_FAMILY)
        check_hresult(layout->SetFontFamilyName(family_name(s.family).c_str(), r));
    if (fields & GR_TEXT_STYLE_SIZE)
        check_hresult(layout->SetFontSize(static_cast<float>(s.size), r));
    if (fields & GR_TEXT_STYLE_WEIGHT)
        check_hresult(layout->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(s.weight), r));
    if (fields & GR_TEXT_STYLE_SLANT)
        check_hresult(layout->SetFontStyle(font_style(s.slant), r));
    if (fields & GR_TEXT_STYLE_DECORATIONS)
    {
        check_hresult(layout->SetUnderline((s.decorations & GR_TEXT_DECORATION_UNDERLINE) != 0, r));
    }
    if (fields & GR_TEXT_STYLE_LETTER_SPACING)
    {
        com_ptr<IDWriteTextLayout1> spaced;
        if (SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(spaced.put()))))
            check_hresult(spaced->SetCharacterSpacing(0, static_cast<float>(s.letter_spacing), 0, r));
    }
    if (fields & GR_TEXT_STYLE_COLOR)
        colors.push_back({r, s.color});
}

std::vector<DWRITE_HIT_TEST_METRICS> hit_range(IDWriteTextLayout *layout, UINT32 start, UINT32 length)
{
    UINT32 count = 0;
    std::vector<DWRITE_HIT_TEST_METRICS> result(8);
    HRESULT hr =
        layout->HitTestTextRange(start, length, 0, 0, result.data(), static_cast<UINT32>(result.size()), &count);
    if (hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))
    {
        result.resize(count);
        hr = layout->HitTestTextRange(start, length, 0, 0, result.data(), count, &count);
    }
    check_hresult(hr);
    result.resize(count);
    return result;
}

std::vector<UINT32> grapheme_stops(gr_utf16 u)
{
    std::vector<UINT32> stops{0};
    if (!u.count)
        return stops;
    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator *it = ubrk_open(UBRK_CHARACTER, nullptr, reinterpret_cast<UChar const *>(u.data),
                                   static_cast<int32_t>(u.count), &status);
    if (U_FAILURE(status))
    {
        for (UINT32 i = 1; i <= u.count; ++i)
            if (i == u.count || (u.data[i] & 0xFC00) != 0xDC00)
                stops.push_back(i);
        return stops;
    }
    for (int32_t b = ubrk_next(it); b != UBRK_DONE; b = ubrk_next(it))
        stops.push_back(static_cast<UINT32>(b));
    ubrk_close(it);
    return stops;
}

UINT32 previous_stop16(TextLayout const *l, UINT32 u);

/* Whether DirectWrite drew its trimming sign on line: a zero-length hit-test entry with a width. */
bool has_sign(IDWriteTextLayout *layout, Line const &line)
{
    for (auto const &h : hit_range(layout, line.start, std::max<UINT32>(line.end - line.start, 1)))
        if (std::abs(h.top - line.bounds.origin.y) <= 0.01 && !h.length && h.width > 0)
            return true;
    return false;
}

/* Ends the last visible line with an ellipsis in the font and color of its last grapheme, eliding graphemes from
 * its end until the ellipsis fits inside the box. */
void add_token(TextLayout *l, double box)
{
    auto &line = l->lines.back();
    auto layout = l->layout.get();
    UINT32 at = line.stop > line.start ? line.stop - 1 : line.start;
    UINT32 length = 0;
    check_hresult(layout->GetFontFamilyNameLength(at, &length));
    std::wstring family(length + 1, L'\0');
    check_hresult(layout->GetFontFamilyName(at, family.data(), length + 1));
    float size = 0;
    DWRITE_FONT_WEIGHT weight{};
    DWRITE_FONT_STYLE style{};
    check_hresult(layout->GetFontSize(at, &size));
    check_hresult(layout->GetFontWeight(at, &weight));
    check_hresult(layout->GetFontStyle(at, &style));
    com_ptr<IDWriteTextFormat> format;
    check_hresult(l->system->factory->CreateTextFormat(family.c_str(), nullptr, weight, style,
                                                       DWRITE_FONT_STRETCH_NORMAL, size, L"", format.put()));
    check_hresult(l->system->factory->CreateTextLayout(L"\u2026", 1, format.get(), FLT_MAX, FLT_MAX, l->token.put()));
    for (auto const &span : l->colors)
        if (span.range.startPosition <= at && at < span.range.startPosition + span.range.length)
            l->token_color = span.color;
    DWRITE_TEXT_METRICS tm;
    check_hresult(l->token->GetMetrics(&tm));
    DWRITE_LINE_METRICS lm;
    UINT32 lines = 0;
    check_hresult(l->token->GetLineMetrics(&lm, 1, &lines));
    double width = tm.widthIncludingTrailingWhitespace;
    double left = line.bounds.origin.x, right = left + line.bounds.size.width;
    bool rtl = l->rtl;
    double edge = rtl ? left : right;
    if (rtl ? left - width < -1e-6 : right + width > box + 1e-6)
    {
        /* Walk grapheme boundaries back from the end until the ellipsis fits beside the kept text. */
        edge = rtl ? right : left;
        line.caret = line.start;
        for (UINT32 stop = line.stop; stop > line.start; stop = previous_stop16(l, stop))
        {
            float x = 0, y = 0;
            DWRITE_HIT_TEST_METRICS h;
            check_hresult(layout->HitTestTextPosition(stop, stop == line.stop, &x, &y, &h));
            if (stop == line.stop)
                x = static_cast<float>(rtl ? left : right);
            if (rtl ? x - width >= -1e-6 : x + width <= box + 1e-6)
            {
                edge = x;
                line.caret = stop;
                break;
            }
        }
        l->cutoff = edge;
    }
    l->token_width = width;
    l->token_at = {rtl ? edge - width : edge, line.baseline - lm.baseline};
    if (rtl)
    {
        line.bounds.size.width = right - l->token_at.x;
        line.bounds.origin.x = l->token_at.x;
    }
    else
        line.bounds.size.width = edge + width - left;
}

void build(TextLayout *l)
{
    auto const &o = l->options;
    gr_utf16 u = gr_attributed_text_get_utf16(l->text);
    auto n = static_cast<UINT32>(u.count);
    auto factory = l->system->factory.get();
    gr_text_style base = gr_attributed_text_get_base(l->text);
    com_ptr<IDWriteTextFormat> format;
    check_hresult(factory->CreateTextFormat(
        family_name(base.family).c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(base.weight), font_style(base.slant),
        DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(base.size), L"", format.put()));
    bool &rtl = l->rtl;
    rtl = o.direction == GR_TEXT_DIRECTION_RTL;
    if (o.direction == GR_TEXT_DIRECTION_AUTO && n)
        rtl = ubidi_getBaseDirection(reinterpret_cast<UChar const *>(u.data), static_cast<int32_t>(n)) == UBIDI_RTL;
    check_hresult(format->SetReadingDirection(rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT
                                                  : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT));
    check_hresult(format->SetTextAlignment(o.align == GR_TEXT_ALIGN_CENTER  ? DWRITE_TEXT_ALIGNMENT_CENTER
                                           : o.align == GR_TEXT_ALIGN_START ? DWRITE_TEXT_ALIGNMENT_LEADING
                                                                            : DWRITE_TEXT_ALIGNMENT_TRAILING));
    bool wraps = o.has_max_width && o.wrap != GR_TEXT_WRAP_NONE;
    check_hresult(format->SetWordWrapping(wraps ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP));
    bool ellipsize = o.truncation == GR_TEXT_TRUNCATE_END;
    if (ellipsize)
    {
        com_ptr<IDWriteInlineObject> sign;
        check_hresult(factory->CreateEllipsisTrimmingSign(format.get(), sign.put()));
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        check_hresult(format->SetTrimming(&trimming, sign.get()));
    }
    float max_width = o.has_max_width ? static_cast<float>(o.max_width) : 0;
    /* CreateTextLayout rejects a NULL string even when it is empty. */
    auto chars = n ? reinterpret_cast<wchar_t const *>(u.data) : L"";
    check_hresult(factory->CreateTextLayout(chars, n, format.get(), max_width, FLT_MAX, l->layout.put()));
    auto layout = l->layout.get();
    DWRITE_TEXT_RANGE all{0, n};
    if (base.letter_spacing != 0)
        apply_style(layout, base, GR_TEXT_STYLE_LETTER_SPACING, all, l->colors);
    if (base.decorations)
        apply_style(layout, base, GR_TEXT_STYLE_DECORATIONS, all, l->colors);
    gr_text_span_list spans = gr_attributed_text_get_spans(l->text);
    bool spaced = base.letter_spacing != 0;
    for (size_t i = 0; i < spans.count; ++i)
    {
        auto const &span = spans.data[i];
        UINT32 start = to16(l, span.range.start), end = to16(l, span.range.end);
        apply_style(layout, span.style, span.fields, {start, end - start}, l->colors);
        spaced |= (span.fields & GR_TEXT_STYLE_LETTER_SPACING) != 0;
    }
    l->stops = grapheme_stops(u);
    if (spaced)
    {
        /* Spacing goes between graphemes: none after the last grapheme of each paragraph. */
        com_ptr<IDWriteTextLayout1> spacing;
        if (SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(spacing.put()))))
            for (UINT32 start = 0; start < n;)
            {
                UINT32 end = paragraph_end(u, start), stop = end;
                while (stop > start && paragraph_break(u.data[stop - 1]))
                    --stop;
                if (stop > start)
                {
                    UINT32 last = previous_stop16(l, stop);
                    check_hresult(spacing->SetCharacterSpacing(0, 0, 0, {last, stop - last}));
                }
                start = end;
            }
    }

    /* Unbounded layouts align inside their widest line, which DirectWrite needs as the layout width. */
    DWRITE_TEXT_METRICS tm;
    check_hresult(layout->GetMetrics(&tm));
    if (!o.has_max_width)
        check_hresult(layout->SetMaxWidth(tm.width));

    UINT32 count = 0;
    layout->GetLineMetrics(nullptr, 0, &count);
    std::vector<DWRITE_LINE_METRICS> metrics(count);
    check_hresult(layout->GetLineMetrics(metrics.data(), count, &count));
    bool cut = o.max_lines && count > o.max_lines;
    size_t visible = cut ? o.max_lines : count;
    double height = 0;
    for (size_t i = 0; i < visible; ++i)
        height += metrics[i].height;
    /* DirectWrite only trims a line it cuts, so a cut at a paragraph break gets a manual ellipsis instead. */
    bool at_break = cut && metrics[visible - 1].newlineLength > 0;
    bool trim_height = cut && ellipsize && o.wrap != GR_TEXT_WRAP_NONE && !at_break;
    if (trim_height)
    {
        check_hresult(layout->SetMaxHeight(static_cast<float>(height + metrics[visible].height / 2)));
        check_hresult(layout->GetLineMetrics(metrics.data(), count, &count));
    }
    else if (cut)
        l->clip_height = height;

    bool trimmed = false;
    double top = 0, widest = 0;
    UINT32 position = 0;
    for (size_t i = 0; i < visible; ++i)
    {
        auto const &m = metrics[i];
        Line line{};
        line.start = position;
        line.end = position + m.length;
        position = line.end;
        line.stop = line.end - m.newlineLength;
        bool last_cut = cut && i + 1 == visible;
        line.soft = i + 1 < visible && m.newlineLength == 0;
        if (last_cut && trim_height)
        {
            /* The ellipsized last line owns the rest of its paragraph. */
            line.end = paragraph_end(u, line.start);
            line.stop = line.end;
            while (line.stop > line.start && paragraph_break(u.data[line.stop - 1]))
                --line.stop;
        }
        line.caret = line.soft ? line.end : line.stop;
        double left = DBL_MAX, right = -DBL_MAX;
        /* trailingWhitespaceLength already counts the paragraph separator. */
        UINT32 shown = m.isTrimmed ? line.stop - line.start : m.length - m.trailingWhitespaceLength;
        if (m.isTrimmed)
            trimmed = true;
        for (auto const &h : hit_range(layout, line.start, std::max<UINT32>(shown, 1)))
        {
            if (std::abs(h.top - top) > 0.01)
                continue;
            if (m.isTrimmed && h.isTrimmed && h.length)
                line.caret = std::min(line.caret, h.textPosition);
            if (shown && h.width > 0)
            {
                left = std::min(left, static_cast<double>(h.left));
                right = std::max(right, static_cast<double>(h.left + h.width));
            }
        }
        if (left > right)
        {
            float x, y;
            DWRITE_HIT_TEST_METRICS h;
            check_hresult(layout->HitTestTextPosition(line.start, FALSE, &x, &y, &h));
            left = right = x;
        }
        line.bounds = {{left, top}, {right - left, m.height}};
        line.baseline = top + m.baseline;
        line.ascent = m.baseline;
        line.descent = m.height - m.baseline;
        line.range = {to8(l, line.start), to8(l, line.end)};
        widest = std::max(widest, right - left);
        top += m.height;
        l->lines.push_back(line);
    }
    double box = o.has_max_width ? o.max_width : widest;
    if (cut && ellipsize && !has_sign(layout, l->lines.back()))
        add_token(l, box);
    auto const &first = l->lines.front(), &last = l->lines.back();
    l->metrics.size = {box, top};
    l->metrics.line_count = visible;
    l->metrics.first_baseline = first.baseline;
    l->metrics.last_baseline = last.baseline;
    l->metrics.visible = {0, cut ? last.range.end : gr_attributed_text_get_utf8(l->text).count};
    l->metrics.truncated = cut || trimmed;
}

/* Locks the layout and builds it on first use. */
struct Ready
{
    TextLayout *l;
    std::lock_guard<std::mutex> guard;

    explicit Ready(void *data) : l(static_cast<TextLayout *>(data)), guard(l->lock)
    {
        if (!l->ready)
        {
            try
            {
                build(l);
            }
            catch (...)
            {
                report("DirectWrite text layout");
                if (l->lines.empty())
                    l->lines.push_back({});
                l->metrics.line_count = l->lines.size();
            }
            l->ready = true;
        }
    }

    TextLayout *operator->() const
    {
        return l;
    }
};

gr_text_layout_metrics dw_metrics(void *data)
{
    Ready l(data);
    return l->metrics;
}

void dw_line(void *data, size_t index, gr_text_line *out)
{
    Ready l(data);
    auto const &line = l->lines[index];
    *out = {line.range, line.bounds, line.baseline, line.ascent, line.descent};
}

size_t dw_next_stop(void *data, size_t index)
{
    Ready l(data);
    UINT32 u = to16(l.l, index);
    auto it = std::upper_bound(l->stops.begin(), l->stops.end(), u);
    return it == l->stops.end() ? gr_attributed_text_get_utf8(l->text).count : to8(l.l, *it);
}

size_t dw_previous_stop(void *data, size_t index)
{
    Ready l(data);
    UINT32 u = to16(l.l, index);
    /* An index inside a code point rounds down to its start, which may itself be a stop. */
    bool inside = to8(l.l, u) < index;
    auto it = inside ? std::upper_bound(l->stops.begin(), l->stops.end(), u)
                     : std::lower_bound(l->stops.begin(), l->stops.end(), u);
    return it == l->stops.begin() ? 0 : to8(l.l, *(it - 1));
}

Line const &line_at_y(TextLayout const *l, double y)
{
    for (auto const &line : l->lines)
        if (y < line.bounds.origin.y + line.bounds.size.height)
            return line;
    return l->lines.back();
}

UINT32 previous_stop16(TextLayout const *l, UINT32 u)
{
    auto it = std::lower_bound(l->stops.begin(), l->stops.end(), u);
    return it == l->stops.begin() ? 0 : *(it - 1);
}

gr_text_position dw_hit_test(void *data, gr_point point)
{
    Ready l(data);
    if (!l->layout)
        return {0, GR_TEXT_AFFINITY_DOWNSTREAM};
    auto const &line = line_at_y(l.l, point.y);
    double left = line.bounds.origin.x, right = left + line.bounds.size.width;
    bool rtl = l->layout && l->layout->GetReadingDirection() == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    UINT32 position;
    if (point.x < left || point.x >= right)
        position = (point.x >= right) == !rtl ? line.caret : line.start;
    else
    {
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS h{};
        double y = line.bounds.origin.y + line.bounds.size.height / 2;
        check_hresult(
            l->layout->HitTestPoint(static_cast<float>(point.x), static_cast<float>(y), &trailing, &inside, &h));
        position = h.textPosition + (trailing ? h.length : 0);
    }
    position = std::clamp(position, line.start, line.caret);
    size_t index = to8(l.l, position);
    bool upstream = line.soft && index == line.range.end;
    return {index, upstream ? GR_TEXT_AFFINITY_UPSTREAM : GR_TEXT_AFFINITY_DOWNSTREAM};
}

Line const &line_for_position(TextLayout const *l, gr_text_position p)
{
    for (auto const &line : l->lines)
        if (p.index < line.range.end ||
            (p.index == line.range.end && line.soft && p.affinity == GR_TEXT_AFFINITY_UPSTREAM))
            return line;
    return l->lines.back();
}

gr_rect dw_caret_rect(void *data, gr_text_position position)
{
    Ready l(data);
    if (!l->layout)
        return {{0, 0}, {0, 0}};
    auto const &line = line_for_position(l.l, position);
    UINT32 index = std::min(to16(l.l, position.index), line.caret);
    bool upstream = position.affinity == GR_TEXT_AFFINITY_UPSTREAM;
    bool has_previous = index > line.start, has_next = index < line.stop;
    float x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS h;
    if (l->token && &line == &l->lines.back() && index >= line.caret)
        /* At the ellipsis: its edge facing the kept text. */
        x = static_cast<float>(l->rtl ? l->token_at.x + l->token_width : l->token_at.x);
    else if (has_previous && (upstream || !has_next))
        /* The trailing edge of the grapheme before index. */
        check_hresult(l->layout->HitTestTextPosition(previous_stop16(l.l, index), TRUE, &x, &y, &h));
    else
        check_hresult(l->layout->HitTestTextPosition(index, FALSE, &x, &y, &h));
    return {{x, line.bounds.origin.y}, {0, line.bounds.size.height}};
}

size_t dw_range_rects(void *data, gr_text_range range, gr_rect *rects, size_t capacity)
{
    Ready l(data);
    if (!l->layout)
        return 0;
    UINT32 start = to16(l.l, range.start), end = to16(l.l, range.end);
    size_t total = 0;
    for (auto const &line : l->lines)
    {
        bool tokened = l->token && &line == &l->lines.back();
        UINT32 a = std::max(start, line.start), b = std::min(end, tokened ? line.caret : line.stop);
        if (tokened && end > line.caret && start < line.end)
        {
            if (total < capacity)
                rects[total] = {{l->token_at.x, line.bounds.origin.y}, {l->token_width, line.bounds.size.height}};
            ++total;
        }
        if (a >= b)
            continue;
        std::vector<std::pair<double, double>> spans;
        for (auto const &h : hit_range(l->layout.get(), a, b - a))
            if (std::abs(h.top - line.bounds.origin.y) <= 0.01 && h.width > 0)
                spans.emplace_back(h.left, h.left + h.width);
        std::sort(spans.begin(), spans.end());
        for (size_t k = 0; k < spans.size();)
        {
            auto merged = spans[k++];
            while (k < spans.size() && spans[k].first <= merged.second + 1e-3)
                merged.second = std::max(merged.second, spans[k++].second);
            if (total < capacity)
                rects[total] = {{merged.first, line.bounds.origin.y},
                                {merged.second - merged.first, line.bounds.size.height}};
            ++total;
        }
    }
    return total;
}

/* Glyphs */

/* Records the glyph runs IDWriteTextLayout::Draw produces; the ellipsis sign is an inline object drawing through it. */
struct Collector final : IDWriteTextRenderer
{
    std::vector<GlyphRun> runs;
    bool ellipsis = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) || iid == __uuidof(IDWriteTextRenderer))
        {
            *out = static_cast<IDWriteTextRenderer *>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        return 1;
    }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *, BOOL *out) override
    {
        *out = TRUE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *, DWRITE_MATRIX *out) override
    {
        *out = {1, 0, 0, 1, 0, 0};
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *, FLOAT *out) override
    {
        *out = 1;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE, DWRITE_GLYPH_RUN const *run,
                                           DWRITE_GLYPH_RUN_DESCRIPTION const *description, IUnknown *) override
    {
        GlyphRun copy;
        copy.face.copy_from(run->fontFace);
        copy.size = run->fontEmSize;
        copy.sideways = run->isSideways;
        copy.level = run->bidiLevel;
        copy.ids.assign(run->glyphIndices, run->glyphIndices + run->glyphCount);
        copy.advances.assign(run->glyphAdvances, run->glyphAdvances + run->glyphCount);
        if (run->glyphOffsets)
            copy.offsets.assign(run->glyphOffsets, run->glyphOffsets + run->glyphCount);
        else
            copy.offsets.assign(run->glyphCount, DWRITE_GLYPH_OFFSET{});
        if (!ellipsis && description && description->clusterMap)
        {
            copy.cluster_map.assign(description->clusterMap, description->clusterMap + description->stringLength);
            copy.text = description->textPosition;
        }
        copy.origin = {x, y};
        copy.ellipsis = ellipsis;
        runs.push_back(std::move(copy));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void *, FLOAT, FLOAT, DWRITE_UNDERLINE const *, IUnknown *) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const *, IUnknown *) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawInlineObject(void *context, FLOAT x, FLOAT y, IDWriteInlineObject *object,
                                               BOOL sideways, BOOL rtl, IUnknown *effect) override
    {
        bool outer = ellipsis;
        ellipsis = true;
        HRESULT hr = object->Draw(context, this, x, y, sideways, rtl, effect);
        ellipsis = outer;
        return hr;
    }
};

/* Builds every visible line's glyphs; the caller holds the layout's lock. */
void build_glyphs(TextLayout *l)
{
    if (l->glyphs_ready)
        return;
    l->glyphs_ready = true;
    l->glyphs.assign(l->lines.size(), {});
    if (!l->layout)
        return;
    Collector collector;
    check_hresult(l->layout->Draw(nullptr, &collector, 0, 0));
    if (l->token)
    {
        collector.ellipsis = true;
        check_hresult(
            l->token->Draw(nullptr, &collector, static_cast<float>(l->token_at.x), static_cast<float>(l->token_at.y)));
    }
    l->runs = std::move(collector.runs);
    gr_utf16 u = gr_attributed_text_get_utf16(l->text);
    for (size_t r = 0; r < l->runs.size(); ++r)
    {
        auto const &run = l->runs[r];
        /* Runs belong to the visible line with their baseline; clipped lines have none. */
        size_t li = l->lines.size();
        for (size_t i = 0; i < l->lines.size(); ++i)
            if (std::abs(l->lines[i].baseline - run.origin.y) < 0.5)
                li = i;
        if (li == l->lines.size())
            continue;
        auto const &line = l->lines[li];
        bool tokened = l->token && li + 1 == l->lines.size();
        gr_text_range elided = {to8(l, line.caret), line.range.end};
        auto n = static_cast<UINT32>(run.ids.size());
        /* Clusters: maximal text spans mapping to one first glyph, owning glyphs up to the next cluster's. */
        std::vector<std::pair<UINT32, UINT32>> clusters(n, {0, 0});
        auto length = static_cast<UINT32>(run.cluster_map.size());
        for (UINT32 p = 0; p < length;)
        {
            UINT32 q = p + 1;
            while (q < length && run.cluster_map[q] == run.cluster_map[p])
                ++q;
            UINT32 first = run.cluster_map[p], last = q < length ? run.cluster_map[q] : n;
            for (UINT32 g = std::min(first, last); g < std::max(first, last) && g < n; ++g)
                clusters[g] = {run.text + p, run.text + q};
            p = q;
        }
        DWRITE_FONT_METRICS font;
        run.face->GetMetrics(&font);
        double scale = run.size / font.designUnitsPerEm;
        uint64_t face = font_id(l->system, run.face.get());
        bool rtl = run.level & 1;
        double pen = 0;
        for (UINT32 i = 0; i < n; ++i)
        {
            double advance = run.advances[i];
            double x = rtl ? run.origin.x - pen - advance - run.offsets[i].advanceOffset
                           : run.origin.x + pen + run.offsets[i].advanceOffset;
            double y = run.origin.y - run.offsets[i].ascenderOffset;
            pen += advance;
            UINT32 at = clusters[i].first;
            if (!run.ellipsis && ((at < u.count && paragraph_break(u.data[at])) || (tokened && at >= line.caret)))
                continue;
            DWRITE_GLYPH_METRICS m{};
            run.face->GetDesignGlyphMetrics(&run.ids[i], 1, &m, run.sideways);
            double width =
                std::max(0.0, (static_cast<double>(m.advanceWidth) - m.leftSideBearing - m.rightSideBearing) * scale);
            double height =
                std::max(0.0, (static_cast<double>(m.advanceHeight) - m.topSideBearing - m.bottomSideBearing) * scale);
            gr_rect bounds = {{x + m.leftSideBearing * scale, y - (m.verticalOriginY - m.topSideBearing) * scale},
                              {width, height}};
            if (!width || !height)
                bounds.size = {0, 0};
            gr_text_range cluster = run.ellipsis ? elided : gr_text_range{to8(l, at), to8(l, clusters[i].second)};
            gr_text_glyph_flags flags = (rtl ? GR_TEXT_GLYPH_RTL : 0u) | (run.ellipsis ? GR_TEXT_GLYPH_ELLIPSIS : 0u);
            /* Runs are collected without drawing effects, so one run can hold several span colors. */
            std::optional<gr_color> color = run.ellipsis ? l->token_color : std::nullopt;
            if (!run.ellipsis)
                for (auto const &span : l->colors)
                    if (span.range.startPosition <= at && at < span.range.startPosition + span.range.length)
                        color = span.color;
            l->glyphs[li].push_back(
                {{{x, y}, advance, bounds, cluster, flags, face, run.size, run.ids[i]}, r, i, color});
        }
    }
    for (auto &line : l->glyphs)
        std::stable_sort(line.begin(), line.end(),
                         [](GlyphRef const &a, GlyphRef const &b) { return a.info.position.x < b.info.position.x; });
}

size_t dw_glyphs(void *data, size_t line, gr_text_glyph *glyphs, size_t capacity)
{
    Ready l(data);
    try
    {
        build_glyphs(l.l);
    }
    catch (...)
    {
        report("DirectWrite glyphs");
    }
    if (line >= l->glyphs.size())
        return 0;
    auto const &list = l->glyphs[line];
    for (size_t i = 0; i < list.size() && i < capacity; ++i)
        glyphs[i] = list[i].info;
    return list.size();
}

/* Draws a glyph run with color fonts (emoji) layer by layer, as DrawTextLayout does. Windows 11's emoji are COLRv1
 * paint trees with gradients; without the paint path they would come out as flat COLRv0 layers. */
void draw_run(ID2D1DeviceContext *dc, IDWriteFactory *factory, D2D1_POINT_2F at, DWRITE_GLYPH_RUN const &run,
              ID2D1Brush *brush)
{
    com_ptr<IDWriteFactory4> colors;
    com_ptr<ID2D1DeviceContext4> images;
    com_ptr<IDWriteFactory8> paints;
    com_ptr<ID2D1DeviceContext7> painter;
    factory->QueryInterface(IID_PPV_ARGS(colors.put()));
    dc->QueryInterface(IID_PPV_ARGS(images.put()));
    factory->QueryInterface(IID_PPV_ARGS(paints.put()));
    dc->QueryInterface(IID_PPV_ARGS(painter.put()));
    if (colors && images)
    {
        auto formats = DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE | DWRITE_GLYPH_IMAGE_FORMATS_CFF |
                       DWRITE_GLYPH_IMAGE_FORMATS_COLR | DWRITE_GLYPH_IMAGE_FORMATS_SVG |
                       DWRITE_GLYPH_IMAGE_FORMATS_PNG | DWRITE_GLYPH_IMAGE_FORMATS_JPEG |
                       DWRITE_GLYPH_IMAGE_FORMATS_TIFF | DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8;
        com_ptr<IDWriteColorGlyphRunEnumerator1> layers;
        HRESULT hr;
        if (paints && painter)
            hr = paints->TranslateColorGlyphRun(at, &run, nullptr, formats | DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE,
                                                painter->GetPaintFeatureLevel(), DWRITE_MEASURING_MODE_NATURAL, nullptr,
                                                0, layers.put());
        else
            hr = colors->TranslateColorGlyphRun(at, &run, nullptr, formats, DWRITE_MEASURING_MODE_NATURAL, nullptr, 0,
                                                layers.put());
        if (SUCCEEDED(hr))
        {
            BOOL more = FALSE;
            while (SUCCEEDED(layers->MoveNext(&more)) && more)
            {
                DWRITE_COLOR_GLYPH_RUN1 const *layer = nullptr;
                check_hresult(layers->GetCurrentRun(&layer));
                D2D1_POINT_2F origin{layer->baselineOriginX, layer->baselineOriginY};
                switch (layer->glyphImageFormat)
                {
                    case DWRITE_GLYPH_IMAGE_FORMATS_PNG:
                    case DWRITE_GLYPH_IMAGE_FORMATS_JPEG:
                    case DWRITE_GLYPH_IMAGE_FORMATS_TIFF:
                    case DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8:
                        images->DrawColorBitmapGlyphRun(layer->glyphImageFormat, origin, &layer->glyphRun,
                                                        layer->measuringMode,
                                                        D2D1_COLOR_BITMAP_GLYPH_SNAP_OPTION_DEFAULT);
                        break;
                    case DWRITE_GLYPH_IMAGE_FORMATS_SVG:
                        images->DrawSvgGlyphRun(origin, &layer->glyphRun, brush, nullptr, 0, layer->measuringMode);
                        break;
                    case DWRITE_GLYPH_IMAGE_FORMATS_COLR_PAINT_TREE:
                        painter->DrawPaintGlyphRun(origin, &layer->glyphRun, brush, 0, layer->measuringMode);
                        break;
                    default:
                        if (layer->paletteIndex == 0xFFFF)
                            dc->DrawGlyphRun(origin, &layer->glyphRun, brush, layer->measuringMode);
                        else
                        {
                            com_ptr<ID2D1SolidColorBrush> paint;
                            check_hresult(dc->CreateSolidColorBrush(layer->runColor, paint.put()));
                            dc->DrawGlyphRun(origin, &layer->glyphRun, paint.get(), layer->measuringMode);
                        }
                }
            }
            return;
        }
        if (hr != DWRITE_E_NOCOLOR)
            check_hresult(hr);
    }
    dc->DrawGlyphRun(at, &run, brush, DWRITE_MEASURING_MODE_NATURAL);
}

void dw_layout_destroy(void *data)
{
    delete static_cast<TextLayout *>(data);
}

gr_text_layout *dw_layout_text(void *data, gr_attributed_text *text, gr_text_layout_options const *options)
{
    auto system = static_cast<TextSystem *>(data);
    auto l = new TextLayout;
    l->system = system;
    l->text = text;
    l->options = *options;
    gr_text_layout *layout = gr_text_layout_new(system->handle, text, options, &dwrite_layout_vtable, l);
    if (!layout)
        delete l;
    return layout;
}

void dw_system_destroy(void *data)
{
    delete static_cast<TextSystem *>(data);
}
} // namespace

const gr_text_layout_vtable dwrite_layout_vtable = {
    dw_metrics,    dw_line,        dw_next_stop, dw_previous_stop,  dw_hit_test,
    dw_caret_rect, dw_range_rects, dw_glyphs,    dw_layout_destroy,
};

void draw_text_layout(ID2D1DeviceContext *dc, gr_text_layout *layout, gr_point origin, gr_color color, float alpha)
{
    Ready l(gr_text_layout_get_data(layout));
    if (!l->layout)
        return;
    D2D1_BRUSH_PROPERTIES props{alpha, D2D1::Matrix3x2F::Identity()};
    auto solid = [&](gr_color c)
    {
        com_ptr<ID2D1SolidColorBrush> brush;
        auto value = d2d_color(c);
        check_hresult(dc->CreateSolidColorBrush(&value, &props, brush.put()));
        return brush;
    };
    /* Brushes belong to this device, so span colors are drawing effects only for this draw. */
    for (auto const &span : l->colors)
        check_hresult(l->layout->SetDrawingEffect(solid(span.color).get(), span.range));
    auto mode = dc->GetTextAntialiasMode();
    /* Surfaces are transparent, where ClearType cannot blend. */
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (l->clip_height > 0)
        dc->PushAxisAlignedClip(D2D1::RectF(-1e5f, -1e5f, 1e5f, static_cast<float>(origin.y + l->clip_height)),
                                D2D1_ANTIALIAS_MODE_ALIASED);
    auto fill = solid(color);
    auto options = D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT;
    if (std::isnan(l->cutoff))
        dc->DrawTextLayout(point(origin), l->layout.get(), fill.get(), options);
    else
    {
        /* Lines above the last one, then the last line's kept side of the cutoff. */
        auto const &last = l->lines.back();
        auto top = static_cast<float>(origin.y + last.bounds.origin.y);
        auto bottom = static_cast<float>(origin.y + last.bounds.origin.y + last.bounds.size.height);
        auto cut = static_cast<float>(origin.x + l->cutoff);
        dc->PushAxisAlignedClip(D2D1::RectF(-1e5f, -1e5f, 1e5f, top), D2D1_ANTIALIAS_MODE_ALIASED);
        dc->DrawTextLayout(point(origin), l->layout.get(), fill.get(), options);
        dc->PopAxisAlignedClip();
        dc->PushAxisAlignedClip(l->rtl ? D2D1::RectF(cut, top, 1e5f, bottom) : D2D1::RectF(-1e5f, top, cut, bottom),
                                D2D1_ANTIALIAS_MODE_ALIASED);
        dc->DrawTextLayout(point(origin), l->layout.get(), fill.get(), options);
        dc->PopAxisAlignedClip();
    }
    if (l->clip_height > 0)
        dc->PopAxisAlignedClip();
    if (l->token)
        dc->DrawTextLayout(point({origin.x + l->token_at.x, origin.y + l->token_at.y}), l->token.get(),
                           l->token_color ? solid(*l->token_color).get() : fill.get(), options);
    dc->SetTextAntialiasMode(mode);
    if (!l->colors.empty())
        check_hresult(l->layout->SetDrawingEffect(nullptr, {0, UINT32_MAX}));
}

void draw_text_glyphs(ID2D1DeviceContext *dc, gr_text_layout *layout, size_t line, gr_glyph_range range,
                      gr_point origin, gr_color color, float alpha)
{
    Ready l(gr_text_layout_get_data(layout));
    build_glyphs(l.l);
    if (line >= l->glyphs.size())
        return;
    auto const &list = l->glyphs[line];
    size_t end = std::min(range.end, list.size());
    if (range.start >= end)
        return;
    auto solid = [&](gr_color c)
    {
        com_ptr<ID2D1SolidColorBrush> brush;
        check_hresult(dc->CreateSolidColorBrush(d2d_color(c), brush.put()));
        return brush;
    };
    /* Bitmap emoji ignore brush opacity, so global alpha applies through a layer. */
    if (alpha < 1)
    {
        D2D1_LAYER_PARAMETERS1 p{{-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX},
                                 nullptr,
                                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                 D2D1::Matrix3x2F::Identity(),
                                 alpha,
                                 nullptr,
                                 D2D1_LAYER_OPTIONS1_NONE};
        dc->PushLayer(&p, nullptr);
    }
    auto mode = dc->GetTextAntialiasMode();
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    auto fill = solid(color);
    /* DrawTextLayout snaps each run's baseline to whole device pixels under axis-aligned transforms; match it so a
     * glyph drawn alone rasterizes like the same glyph in the layout. */
    D2D1_MATRIX_3X2_F transform;
    dc->GetTransform(&transform);
    auto snap = [&](D2D1_POINT_2F p)
    {
        if (transform._12 != 0 || transform._21 != 0 || transform._22 == 0)
            return p;
        float y = std::round(transform._22 * p.y + transform._32);
        p.y = (y - transform._32) / transform._22;
        return p;
    };
    for (size_t i = range.start; i < end;)
    {
        /* Glyphs adjacent within one run draw together, in either direction. */
        auto const &first = list[i];
        UINT32 lo = first.index, hi = first.index;
        auto same_color = [&](GlyphRef const &g)
        {
            if (g.color.has_value() != first.color.has_value())
                return false;
            return !g.color || !memcmp(&*g.color, &*first.color, sizeof(gr_color));
        };
        for (++i; i < end && list[i].run == first.run && same_color(list[i]) &&
                  (list[i].index == hi + 1 || list[i].index + 1 == lo);
             ++i)
        {
            lo = std::min(lo, list[i].index);
            hi = std::max(hi, list[i].index);
        }
        auto const &run = l->runs[first.run];
        double before = 0;
        for (UINT32 g = 0; g < lo; ++g)
            before += run.advances[g];
        bool rtl = run.level & 1;
        D2D1_POINT_2F at{static_cast<float>(origin.x + run.origin.x + (rtl ? -before : before)),
                         static_cast<float>(origin.y + run.origin.y)};
        DWRITE_GLYPH_RUN sub{run.face.get(),    run.size,         hi - lo + 1,  &run.ids[lo],
                             &run.advances[lo], &run.offsets[lo], run.sideways, run.level};
        draw_run(dc, l->system->factory.get(), snap(at), sub, first.color ? solid(*first.color).get() : fill.get());
    }
    dc->SetTextAntialiasMode(mode);
    if (alpha < 1)
        dc->PopLayer();
}
} // namespace gr_win

extern "C" gr_text_system *gr_windows_text_system_new(void)
{
    auto system = new gr_win::TextSystem;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(system->factory.put()))))
    {
        delete system;
        return nullptr;
    }
    gr_text_system_vtable vtable = {gr_win::dw_layout_text, gr_win::dw_system_destroy};
    system->handle = gr_text_system_new(&vtable, system);
    return system->handle;
}
