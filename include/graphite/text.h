/**
 * Graphite text API.
 *
 * A text system shapes attributed text with the platform's text stack (CoreText, Pango, DirectWrite) and returns
 * an immutable text layout. Canvas records layouts; backends replay them natively. Text systems are independent
 * of Scenes: layouts can be measured before a Scene has a backend and created while a drawer runs.
 *
 * Indices are UTF-8 byte offsets into the attributed text. Ranges are half-open [start, end). Layout coordinates
 * are logical points with the origin at the top-left of the layout box and y down.
 *
 * Attributed texts, text systems and text layouts are immutable resources with atomic reference counts. They may
 * be shared, queried and released on any thread after synchronized publication.
 */
#ifndef GRAPHITE_TEXT_H
#define GRAPHITE_TEXT_H

#include "canvas.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gr_attributed_text gr_attributed_text;
typedef struct gr_text_system gr_text_system;
typedef struct gr_text_layout gr_text_layout;

/** UTF-8 code units; not NUL-terminated. */
GR_DECLARE_LIST(gr_string, char);

typedef struct
{
    size_t start, end;
} gr_text_range;

/* Text styles */

typedef enum
{
    GR_FONT_SLANT_NORMAL,
    GR_FONT_SLANT_ITALIC
} gr_font_slant;

typedef uint32_t gr_text_decorations;

enum
{
    GR_TEXT_DECORATION_UNDERLINE = 1u,
    GR_TEXT_DECORATION_STRIKETHROUGH = 2u
};

/** Selects the style members a span overrides. */
typedef uint32_t gr_text_style_fields;

enum
{
    GR_TEXT_STYLE_FAMILY = 1u,
    GR_TEXT_STYLE_SIZE = 2u,
    GR_TEXT_STYLE_WEIGHT = 4u,
    GR_TEXT_STYLE_SLANT = 8u,
    GR_TEXT_STYLE_COLOR = 16u,
    GR_TEXT_STYLE_DECORATIONS = 32u,
    GR_TEXT_STYLE_LETTER_SPACING = 64u,
    GR_TEXT_STYLE_ALL = 127u
};

/** family is a font family name or one of the generic names "system-ui", "sans-serif", "serif" and "monospace";
 * empty means "system-ui". Missing families and glyphs fall back natively. size is in points, > 0. weight is in
 * [1,1000] (400 regular, 700 bold) and is matched to the nearest available face. color is paint-only and applies
 * only through spans selecting GR_TEXT_STYLE_COLOR; text outside them uses the color supplied when drawing, so
 * changing that color does not need a new layout. Decorations use the text's color. letter_spacing is extra space
 * in points between adjacent grapheme clusters of a paragraph, none after its last one; it may be negative. Where
 * carets fall inside that space is platform-specific. */
typedef struct
{
    gr_string family;
    double size;
    uint16_t weight;
    gr_font_slant slant;
    gr_color color;
    gr_text_decorations decorations;
    double letter_spacing;
} gr_text_style;

/** Overrides the selected fields over range; unselected fields are ignored. Later spans win where spans overlap. */
typedef struct
{
    gr_text_range range;
    gr_text_style_fields fields;
    gr_text_style style;
} gr_text_span;

GR_DECLARE_LIST(gr_text_span_list, gr_text_span);

/** Returns "system-ui", 14 pt, weight 400, normal slant, opaque black, no decorations and no letter spacing. */
gr_text_style gr_text_style_default(void);

/* Attributed text */

/** Creates owned immutable text. Copies utf8, base and spans (including family strings). base supplies every field
 * except color, which it ignores. Returns NULL for invalid UTF-8 (NUL bytes are allowed), an invalid used style
 * value, unknown field bits, or a span range that is reversed, exceeds the text or does not start and end on code
 * point boundaries. Empty span ranges are ignored. */
gr_attributed_text *gr_attributed_text_new(gr_string utf8, const gr_text_style *base, gr_text_span_list spans);

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_attributed_text *gr_attributed_text_ref(gr_attributed_text *text);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
void gr_attributed_text_unref(gr_attributed_text *text);

/** Returns the borrowed UTF-8 contents, valid while the text remains alive. */
gr_string gr_attributed_text_get_utf8(const gr_attributed_text *text);

/** Returns the base style. family borrows storage from the text and is never empty ("system-ui" replaces an empty
 * name); color is opaque black and unused. */
gr_text_style gr_attributed_text_get_base(const gr_attributed_text *text);

/** Returns the borrowed spans, valid while the text remains alive, in their original order without empty ranges or
 * spans selecting no fields. Families follow the base rules, and unselected fields hold the base style's values.
 * Applying the base style and then each span's selected fields in order resolves the style of any index. */
gr_text_span_list gr_attributed_text_get_spans(const gr_attributed_text *text);

/** Returns the immutable resource identity, unique for the process lifetime and never reused. */
uint64_t gr_attributed_text_get_id(const gr_attributed_text *text);

/* Layout options */

typedef enum
{
    GR_TEXT_WRAP_WORD, /**< Break between words; break inside a word only when it alone exceeds the width. */
    GR_TEXT_WRAP_CHAR, /**< Break between any grapheme clusters. */
    GR_TEXT_WRAP_NONE  /**< Break only at paragraph separators. */
} gr_text_wrap;

typedef enum
{
    GR_TEXT_ALIGN_START, /**< Left in left-to-right paragraphs, right in right-to-left ones. */
    GR_TEXT_ALIGN_CENTER,
    GR_TEXT_ALIGN_END
} gr_text_align;

typedef enum
{
    GR_TEXT_DIRECTION_AUTO, /**< Each paragraph follows its first strong character, else left-to-right. */
    GR_TEXT_DIRECTION_LTR,
    GR_TEXT_DIRECTION_RTL
} gr_text_direction;

typedef enum
{
    GR_TEXT_TRUNCATE_NONE,
    GR_TEXT_TRUNCATE_END /**< Ends the last visible line with an ellipsis when content is cut, also when the cut falls
                          *   at a paragraph break; text that does not leave room for it is elided. */
} gr_text_truncation;

/** Without max_width, lines break only at paragraph separators ("\n", "\r\n", "\r", U+2029) and the box is as wide
 * as the widest line. With it, the box is max_width wide (>= 0) and lines wrap and align inside it. max_lines 0 is
 * unlimited. Content is cut by max_lines, or by max_width on a line that cannot wrap. */
typedef struct
{
    bool has_max_width;
    double max_width;
    gr_text_wrap wrap;
    uint32_t max_lines;
    gr_text_truncation truncation;
    gr_text_align align;
    gr_text_direction direction;
} gr_text_layout_options;

/** Returns no width limit, word wrapping, unlimited lines, no truncation, start alignment and auto direction. */
gr_text_layout_options gr_text_layout_options_default(void);

/* Text systems */

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_text_system *gr_text_system_ref(gr_text_system *system);

/** Releases an owned reference; NULL is accepted. Layouts keep their text system alive. */
void gr_text_system_unref(gr_text_system *system);

/** Lays out text and returns an owned layout, which retains text. Returns NULL for a missing text, invalid options
 * or a native failure. Native work may be deferred until a query or draw needs it. */
gr_text_layout *gr_text_system_layout(gr_text_system *system, gr_attributed_text *text,
                                      const gr_text_layout_options *options);

/* Text layouts */

typedef struct
{
    gr_size size;          /**< Layout box; height covers every visible line. */
    size_t line_count;     /**< Visible lines; at least 1, including for empty text. */
    double first_baseline; /**< y of the first line's baseline. */
    double last_baseline;  /**< y of the last visible line's baseline. */
    gr_text_range visible; /**< Text not cut by max_lines or max_width; starts at 0. */
    bool truncated;        /**< Whether any content was cut, with or without an ellipsis. */
} gr_text_layout_metrics;

typedef struct
{
    gr_text_range range; /**< Includes trailing whitespace and the paragraph separator, if any. */
    gr_rect bounds;      /**< Logical line box in layout coordinates, positioned by alignment. */
    double baseline;     /**< y of the baseline in layout coordinates. */
    double ascent, descent;
} gr_text_line;

/** Acquires an owned reference and returns the same pointer; NULL returns NULL. */
gr_text_layout *gr_text_layout_ref(gr_text_layout *layout);

/** Releases an owned reference; NULL is accepted. Final release destroys the object at a safe boundary. */
void gr_text_layout_unref(gr_text_layout *layout);

/** Returns the borrowed text, valid while the layout remains alive. */
gr_attributed_text *gr_text_layout_get_text(const gr_text_layout *layout);

/** Returns the borrowed text system, valid while the layout remains alive. */
gr_text_system *gr_text_layout_get_system(const gr_text_layout *layout);

/** Returns the options the layout was created with. */
gr_text_layout_options gr_text_layout_get_options(const gr_text_layout *layout);

/** Returns the immutable resource identity, unique for the process lifetime and never reused. */
uint64_t gr_text_layout_get_id(const gr_text_layout *layout);

/** Returns layout-wide metrics; the first call may perform native layout. */
gr_text_layout_metrics gr_text_layout_get_metrics(const gr_text_layout *layout);

/** Stores visible line index in *out; out is required. Index >= line_count returns ARGUMENT and zeroes *out. */
gr_result gr_text_layout_get_line(const gr_text_layout *layout, size_t index, gr_text_line *out);

/* Carets and hit testing */

/** At an index where two lines meet, downstream places the caret at the start of the later line and upstream at
 * the end of the earlier one. Elsewhere both give the same caret. */
typedef enum
{
    GR_TEXT_AFFINITY_DOWNSTREAM,
    GR_TEXT_AFFINITY_UPSTREAM
} gr_text_affinity;

typedef struct
{
    size_t index;
    gr_text_affinity affinity;
} gr_text_position;

/* Caret stops are the grapheme cluster boundaries in the visible range, plus its end. Calls taking an index move
 * one that is not a caret stop back to the previous stop. Truncated content has no caret stops; indices past the
 * visible range clamp to its end. */

/** Returns the caret stop after index, or the end of the visible range. */
size_t gr_text_layout_next_caret_stop(const gr_text_layout *layout, size_t index);

/** Returns the caret stop before index, or 0. */
size_t gr_text_layout_previous_caret_stop(const gr_text_layout *layout, size_t index);

/** Returns the caret position nearest to point in layout coordinates; points outside the box clamp to the nearest
 * line and its nearest end. Points on an ellipsis return the position where the elided text starts, and elided
 * positions share its caret. A nonfinite point returns {0, downstream}. */
gr_text_position gr_text_layout_hit_test(const gr_text_layout *layout, gr_point point);

/** Stores a zero-width caret rectangle spanning the line height in *out; out is required. In bidirectional text an
 * index can have two visual carets: downstream is the leading edge of the grapheme at the index, upstream the
 * trailing edge of the grapheme before it (in each grapheme's own direction). Where there is no such grapheme, the
 * other one is used. An unknown affinity returns ARGUMENT and zeroes *out. */
gr_result gr_text_layout_get_caret_rect(const gr_text_layout *layout, gr_text_position position, gr_rect *out);

/** Writes up to capacity rectangles covering range, at most one per line and direction run, in visual order;
 * rects may be NULL when capacity is 0. Returns the total number available, so a second call can size the buffer.
 * The range end is moved forward to a caret stop; an empty or truncated range has no rectangles. */
size_t gr_text_layout_get_range_rects(const gr_text_layout *layout, gr_text_range range, gr_rect *rects,
                                      size_t capacity);

/* Glyphs */

/** Glyph indices within one line's glyph list. Half-open. */
typedef struct
{
    size_t start, end;
} gr_glyph_range;

typedef uint32_t gr_text_glyph_flags;

enum
{
    GR_TEXT_GLYPH_RTL = 1u,     /**< Belongs to a right-to-left run. */
    GR_TEXT_GLYPH_ELLIPSIS = 2u /**< Part of a truncation ellipsis; cluster is the elided text. */
};

/** A positioned glyph of a line. Two glyphs with equal font and glyph have the same outline, scaled by font_size;
 * with equal font_size too they draw the same shape, apart from position and color. Fonts compare only between
 * layouts of one text system. Glyphs are not text: one character can take different glyphs (contextual forms,
 * ligatures) and different characters can share one, so compare clusters to compare text. */
typedef struct
{
    gr_point position;     /**< Glyph origin on the baseline, layout coordinates, glyph offsets applied. */
    double advance;        /**< Visual advance, including letter spacing. */
    gr_rect bounds;        /**< Ink bounds in layout coordinates; zero-sized for blank glyphs. */
    gr_text_range cluster; /**< Text the glyph renders, shared by every glyph of a cluster (ligatures, emoji). */
    gr_text_glyph_flags flags;
    uint64_t font;    /**< Font face identity: face, variation axis values and synthetic bold or oblique, not size;
                       *   never 0. Stable for the text system's lifetime. */
    double font_size; /**< Em size in points. */
    uint32_t glyph;   /**< Glyph index in font. */
} gr_text_glyph;

/** Writes up to capacity glyphs of visible line index in visual order (left to right); glyphs may be NULL when
 * capacity is 0. Returns the line's glyph count, or 0 for an invalid line, so a second call can size the buffer.
 * Paragraph separators have no glyphs; elided text has none either, while its ellipsis is listed where it is drawn.
 * The list is fixed for the layout, so glyph indices are stable. */
size_t gr_text_layout_get_glyphs(const gr_text_layout *layout, size_t line, gr_text_glyph *glyphs, size_t capacity);

/* Drawing */

/** Records drawing layout with its box origin at origin and refs layout. color (straight alpha) paints text outside
 * color spans; global alpha applies on top. Rejects nonfinite origin or invalid color. */
gr_result gr_canvas_draw_text_layout(gr_canvas *canvas, gr_text_layout *layout, gr_point origin, gr_color color);

/** Records drawing glyphs [glyphs.start, glyphs.end) of one line of layout, with the layout box origin at origin, so
 * each glyph lands at origin + its position; place a glyph elsewhere by adjusting origin or the canvas transform.
 * Draws glyphs only, without underline or strikethrough, so for text without decorations drawing every glyph range
 * of every line at one origin matches gr_canvas_draw_text_layout. color and alpha work as there. Refs layout;
 * rejects a line or range outside the layout, nonfinite origin or invalid color. */
gr_result gr_canvas_draw_text_glyphs(gr_canvas *canvas, gr_text_layout *layout, size_t line, gr_glyph_range glyphs,
                                     gr_point origin, gr_color color);

/* Text system implementation */

/* Platforms provide owned text systems (gr_macos_text_system_new and its GTK and Windows counterparts) built on
 * these constructors. Core validates arguments, retains texts and systems, and serializes nothing: every callback
 * may run on any thread, concurrently with the others on the same object, so implementations synchronize any
 * lazily computed native state. Callbacks must not throw or call back into the same object's public API.
 *
 * Implementations apply attributed text natively: the base style, then each span's selected fields in order, so
 * later spans win. Text outside color spans takes the color supplied when drawing.
 *
 * Replay: GR_DRAW_TEXT_LAYOUT ops carry {layout, origin, color} and GR_DRAW_TEXT_GLYPHS ops {layout, line, glyphs,
 * origin, color}, with a range validated against get_glyphs when recorded. A backend draws layouts whose vtable it
 * recognizes through gr_text_layout_get_vtable, using gr_text_layout_get_data, and skips the others. */

/** Called only with validated arguments. layout returns a layout made by gr_text_layout_new with this system, the
 * same text and the same options, or NULL on native failure; returning another system's or text's layout aborts.
 * destroy is called once, possibly on any thread, after the final system reference is released; layouts hold system
 * references, so none of them is alive by then. */
typedef struct
{
    gr_text_layout *(*layout)(void *system, gr_attributed_text *text, const gr_text_layout_options *options);
    void (*destroy)(void *system);
} gr_text_system_vtable;

/** All callbacks required. Copies vtable and returns an owned system; takes ownership of system userdata only on
 * success. */
gr_text_system *gr_text_system_new(const gr_text_system_vtable *vtable, void *system);

/** Core does not cache results, and validates and normalizes every call through get_metrics first, so memoize
 * metrics after native layout. Callbacks receive: line indices below line_count; for next_caret_stop, any index
 * below the visible end, returning the first caret stop after it; for previous_caret_stop, any index in
 * (0, visible end], returning the last caret stop before it; finite points; positions at caret stops with a known
 * affinity; ranges with caret stop ends start < end within the visible range. get_range_rects receives NULL rects
 * when capacity is 0, and get_glyphs a line below line_count and NULL glyphs when capacity is 0. destroy is called
 * once, possibly on any thread, after the final layout reference is released. */
typedef struct
{
    gr_text_layout_metrics (*get_metrics)(void *layout);
    void (*get_line)(void *layout, size_t index, gr_text_line *out);
    size_t (*next_caret_stop)(void *layout, size_t index);
    size_t (*previous_caret_stop)(void *layout, size_t index);
    gr_text_position (*hit_test)(void *layout, gr_point point);
    gr_rect (*get_caret_rect)(void *layout, gr_text_position position);
    size_t (*get_range_rects)(void *layout, gr_text_range range, gr_rect *rects, size_t capacity);
    size_t (*get_glyphs)(void *layout, size_t line, gr_text_glyph *glyphs, size_t capacity);
    void (*destroy)(void *layout);
} gr_text_layout_vtable;

/** All callbacks required. Copies vtable and options, refs system and text, and returns an owned layout; takes
 * ownership of layout userdata only on success. Returns NULL for missing arguments or invalid options. The vtable
 * address identifies the implementation for replay, so pass a static vtable. */
gr_text_layout *gr_text_layout_new(gr_text_system *system, gr_attributed_text *text,
                                   const gr_text_layout_options *options, const gr_text_layout_vtable *vtable,
                                   void *layout);

/** Returns the vtable address passed to gr_text_layout_new. */
const gr_text_layout_vtable *gr_text_layout_get_vtable(const gr_text_layout *layout);

/** Returns the borrowed layout userdata passed to gr_text_layout_new. */
void *gr_text_layout_get_data(const gr_text_layout *layout);

/** UTF-16 code units for CoreText and DirectWrite. */
GR_DECLARE_LIST(gr_utf16, uint16_t);

/** Returns the borrowed UTF-16 contents, built on first use and valid while the text remains alive. */
gr_utf16 gr_attributed_text_get_utf16(const gr_attributed_text *text);

/** Converts a code point boundary UTF-8 offset to a UTF-16 offset; other offsets round down to a boundary first. */
size_t gr_attributed_text_utf16_index(const gr_attributed_text *text, size_t utf8_index);

/** Converts a UTF-16 offset to a UTF-8 offset; an offset inside a surrogate pair rounds down. */
size_t gr_attributed_text_utf8_index(const gr_attributed_text *text, size_t utf16_index);

#ifdef __cplusplus
}
#endif

#endif
