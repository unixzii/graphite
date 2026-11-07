#include "internal.hpp"

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
};

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
        check_hresult(layout->SetStrikethrough((s.decorations & GR_TEXT_DECORATION_STRIKETHROUGH) != 0, r));
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
    check_hresult(format->SetWordWrapping(!wraps                        ? DWRITE_WORD_WRAPPING_NO_WRAP
                                          : o.wrap == GR_TEXT_WRAP_CHAR ? DWRITE_WORD_WRAPPING_CHARACTER
                                                                        : DWRITE_WORD_WRAPPING_WRAP));
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
    dw_metrics, dw_line, dw_next_stop, dw_previous_stop, dw_hit_test, dw_caret_rect, dw_range_rects, dw_layout_destroy,
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
