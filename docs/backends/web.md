# Web (DOM) backend

**Status: renders layers, drawings and text, and delivers events.**
`src/platforms/web` (see its [README](../../src/platforms/web/README.md))
renders layer properties, shadows, blur, shape, gradient and image contents,
drawn contents, masks and text, passes every conformance case in Chromium except
the thread-safety case, and delivers frame, viewport, focus, pointer, scroll and
key events. Every example runs in the browser. IME and touch are not
implemented yet. This page records the design
and what a browser spike measured. The spike ran Chromium (headless shell
158), WebKit and Firefox through Playwright on macOS in November 2026. It
compared each approach with a reference rendering in the same browser.

Graphite runs in the browser as WebAssembly built with Emscripten. Layers
become DOM elements styled with CSS, and the browser composites them. This is
a retained compositor like Core Animation, so the backend follows the macOS
backend's structure: native objects realized once and updated incrementally.

## Core in WebAssembly

The core compiles with Emscripten without changes, warnings included
(`-Werror`). C11 atomics need nothing in a single-threaded build. The unit
tests run under node, single-threaded or with `-pthread`; the
[README](../../src/platforms/web/README.md) has the commands.

A minimal scene with a backend links to 21 KB of wasm (9.7 KB gzipped) at
`-Os -flto`. The backend itself is single-threaded, because the text system
needs the DOM (see [Text](#text)).

## Commit and bridge

- Each host has one animation-frame tick. Frame requests and commit requests
  both schedule it; it delivers a requested `GR_EVENT_FRAME` and then runs
  `gr_scene_commit`, so changes made in a frame commit in the same animation
  frame. This is the platform commit point, before the browser's style and
  layout.
- `apply` encodes the batch as a stream of doubles in linear memory (opcodes
  and operands are documented in `internal.h`) and makes one call into
  JavaScript. JavaScript then applies it in the commit-contract order: realize,
  both children passes, updates, release.
- Paths, paints and strokes are sent inline. Images cross by identity with a
  pointer to their pixels. JavaScript converts each image once into a source
  canvas, and shares it per identity through use counts until resource
  release notifications exist.
- A drawn layer is drawn again only when its display version or drawing
  bounds changed since the last drawing sent; the backend keeps that key per
  layer.

## Layers

Each layer is a small fixed group of elements with inline styles:

```
div.layer       transform: matrix(...) (origin 0 0), opacity, filter: blur(σpx)
├─ svg          shadow: the shadow shape, filled and blurred; outside the clip and mask
└─ div          group: the mask, as clip-path or mask-image
   ├─ div.body  clip-path: the rounded box, when masks_to_bounds
   │  ├─ svg    background: the rounded box, filled
   │  ├─ content image, shape, gradient or drawing
   │  └─ div    sublayers, translated by -bounds.origin
   └─ svg       border: the rounded box minus its inset, inside the box and above everything
```

- **Geometry**: the resolved snapped transform goes into `transform`.
  Everything painted is SVG geometry, `clip-path: path()` or a transformed
  element, never a CSS box size: browsers store box sizes and border widths in
  fixed point (1/64 px in Chromium), which moves snapped edges off the
  device-pixel grid at fractional scales (see
  [Lessons learned](#lessons-learned)). Local-space content and sublayers are
  translated by the bounds offset.
- **Opacity** is CSS group opacity, as Graphite requires.
- **Masks** apply to the group, so they leave the shadow alone. A path mask is
  `clip-path: path()` in box space. A gradient mask is `mask-image` with an SVG
  data URL of the gradient over the box. The group then takes the box's size,
  the one CSS box size in the tree: Chromium ignores a mask on an empty element
  and WebKit clips the mask to the border box.
- **Shadows** are an SVG path of the resolved shadow shape
  (`gr_resolved_new_shadow_path`),
  offset and blurred with `filter: blur(σpx)`. They are not `box-shadow`:
  CSS clips a box shadow away under its own box, but in Graphite the whole
  shape casts the shadow, also beneath a transparent background. WebKit's
  `box-shadow` blur also measures σ ≈ blur / 2.4 instead of the specified
  blur / 2.
- **Layer blur** is `filter: blur(σpx)` on the layer element, which also covers
  its shadow, border and sublayers. Like Core Animation, σ is in layer space
  and scales with transforms.
- **Shape layers** are an SVG `path` with the stroke attributes, not clipped
  to the bounds. Stroke start and end trim with `pathLength="1"` and a dash
  array; trimming a dashed stroke is not implemented yet.
- **Gradient layers** are an SVG rect of the box size filled with a gradient in
  `gradientUnits="objectBoundingBox"`. CSS gradients cannot express Graphite's
  unit-space start and end points or radii that scale with width and height.
- **Images** are converted once per image identity: premultiplied RGBA is
  unpremultiplied into `ImageData` on a source canvas. Each use is a canvas
  element with one CSS pixel per image pixel, scaled onto its rect by a
  transform.

## Drawn contents

A display list replays into the layer's contents container, a div clipped to
the drawing bounds (as backing stores clip on the other backends). Each op
adds whatever element suits it; there is no single canvas or SVG surface:

- `SAVE` / `RESTORE`: restore returns to the wrapper element that was current
  at the matching save.
- `TRANSFORM`: a zero-size wrapper div with `transform: matrix(...)` and
  `transform-origin: 0 0`.
- `CLIP_RECT` / `CLIP_PATH`: a zero-size wrapper div with
  `clip-path: path([evenodd,] '...')`. Nested wrappers intersect.
- `FILL_PATH` / `STROKE_PATH`: an SVG `path` in an inline `svg` of size 1 with
  `overflow: visible`. Gradient paints become `userSpaceOnUse` definitions
  in that svg. Consecutive path ops in the same wrapper share one svg, each
  path carrying its own opacity.
- A solid fill of an axis-aligned rectangle could be a div, but only when its
  edges land on the device-pixel grid under the current transform. This is not
  implemented; every path is SVG.
- `IMAGE`: an image canvas transformed onto the destination rect, as for image
  contents.
- `TEXT_LAYOUT` / `TEXT_GLYPHS`: HTML, as described under [Text](#text). Layouts
  from other text systems are skipped, as text.h asks. The display lists of an
  apply stay alive until the runtime has replayed them, because their text ops
  reference layouts that a drawer may already have released.
- `ALPHA` multiplies into the `opacity` of each element that the following
  ops add. It never goes on a wrapper, because Canvas alpha applies per drawing
  op, not to the group.

The backend caches the replayed subtree by display version and drawing
bounds. The result is vector DOM, so backing-scale and transform changes need
no redraw. The rasterization-scale gap that the roadmap lists for Core
Animation does not apply here.

Measured against Canvas2D replaying the same ops (Canvas2D's model is close to
CoreGraphics) at 1× and 2× in all three engines:

- Pixels with a channel difference over 96: at most 0.01 %, only on
  anti-aliased edges. The scenes covered fills with even-odd and nonzero
  rules, linear and radial gradients with translucent stops, every cap and
  join, dashes with phase and hairlines. They also covered nested transforms
  with multiplied alpha, rect and path clips before and after transforms, and
  scaled, rotated and clipped images.
- Text between two fills keeps its order, transform and clip.
- Replaying everything into one SVG instead gives the same pixels, apart from
  0.02–0.03 % image resampling differences.
- 5 000 paths take 33, 50 and 42 ms to the second frame in Chromium, WebKit
  and Firefox. Canvas2D takes 50, 34 and 59 ms. One svg per path takes
  50, 84 and 85 ms, which is why consecutive paths share an svg.

## Text

`gr_web_text_system_new` (`text.c`, `text.js`) lays text out with the
browser's own text stack, so system fonts, font fallback, shaping, bidi and
emoji all work natively. A layout is computed on its first query or draw in a
hidden measurement element, and drawing places the same line blocks at the
measured positions: the browser never breaks lines while drawing, so what was
measured is what is drawn. `text.c` only converts between the core's types and
the runtime's number arrays; the layout and every query live in `text.js`, with
UTF-16 indices inside and UTF-8 offsets at the boundary.

1. **Line breaks.** When the text can wrap, it goes into one block of the
   layout's width (`white-space: pre-wrap`, `overflow-wrap: break-word`, and
   `unicode-bidi: plaintext` for automatic direction). For each grapheme cluster
   (`Intl.Segmenter`), a `Range` gives its box, and a cluster starts a new line
   when its top passes the current line's bottom minus half its own height.
   Paragraph separators always end lines; without wrapping, lines are the
   paragraphs.
2. **Truncation** is done by hand, because browsers report neither what an
   ellipsis cut nor where it went. Lines past `max_lines` are dropped. With an
   end ellipsis, the last line owns the rest of its paragraph and keeps the
   longest grapheme prefix that still fits with "…" (binary search on a probe
   element), also when the cut falls at a paragraph break; a line that cannot
   wrap and is wider than the layout is cut the same way.
3. **Line boxes.** Each line is laid out again as its own block with
   `white-space: pre`, `width: max-content` and its paragraph's direction
   (explicit, or its first strong character with a table of right-to-left
   blocks, as on macOS). Trailing whitespace and the separator are left out,
   so they never shift aligned lines. A zero-size inline-block marker gives the
   baseline, and the stacked blocks give the line boxes. The block's font,
   which sets the line's minimum height, is the style where the line starts,
   as native text systems size lines by their fonts.

Queries use cluster boxes, measured per line when first needed:

- Caret stops are grapheme boundaries. A downstream caret is the leading edge
  of the grapheme at the index (in its own direction), an upstream caret the
  trailing edge of the one before it; at a soft line break, upstream picks the
  earlier line. Trailing whitespace hangs at the line's end with no width, and
  elided text shares the ellipsis's leading edge.
- Grapheme directions come from their characters: strong right-to-left and
  left-to-right letters, with neutrals taking the direction of the strong
  characters around them when both agree, else the paragraph's.
- Hit testing picks the line by y, then the cluster under x and its nearer
  edge; outside the line, the visually first or last cluster. Past the end of a
  soft-wrapped line it returns the line's end upstream; at a hard break, the
  index before the separator.
- Range rectangles are the selected cluster boxes of each line, merged where
  they touch, in visual order: one per line and visual piece.
- Glyphs are approximations, because the DOM exposes no glyph indices or faces.
  Each grapheme cluster is one glyph at its box's left edge on the baseline,
  with its box's width as advance and ink bounds from Canvas `measureText`.
  `font` hashes the CSS font (family, weight, slant, size) and marks emoji as
  coming from a fallback font; `glyph` hashes the cluster's text. The ellipsis
  is a glyph flagged as such, covering the elided text.

Drawing (`TEXT_LAYOUT` ops) places each line block at its box, with one span
per style run, the paint color for runs without their own, and CSS
decorations. A glyph range (`TEXT_GLYPHS`) draws its whole line exactly the same
way but without decorations, clipped (`clip-path`) to the chosen glyphs' ink
boxes: shaping, kerning and Arabic joining stay those of the whole line, and a
neighbor's overhang stays out. Letter spacing goes on every grapheme but the
last of each paragraph, which Graphite leaves without it.

Measuring splits text into elements only where the layout changes (font and
letter spacing): every element boundary can move later text by a layout unit,
and color is paint-only. Drawing also splits at color changes, which can move
glyphs by up to 1/64 px against the measurement.

Results in Chromium: the 17 text conformance cases that apply pass at 1×, 1.5×
and 2×; the thread-safety case is skipped (see below). `ui`, `glyphs`, `mines`
and `input` draw real text in Chromium, WebKit and Firefox. Earlier spike
measurements of the same approach, against the browser laying out the same
paragraph itself at 2×:

- Latin wrapping, mixed weights, sizes, slants and underlines, Chinese with
  emoji ZWJ sequences and flags, letter spacing, words broken inside and end
  alignment all draw identically: 0.00 % of pixels differ in each engine.
- Bidi paragraphs break into the same lines in every engine. They draw
  identically in Chromium; WebKit (1.96 %) and Firefox (9.95 %) differ through
  deliberate semantics: browsers count preserved spaces before a forced break
  when centering (WebKit also places them inside right-to-left lines), and
  Firefox resolves `text-align: start` under `unicode-bidi: plaintext` with the
  element's direction rather than each paragraph's.
- The hand-made ellipsis matches `-webkit-line-clamp` within 0.1 %.
- Layout cost per label is 0.18, 0.5 and 0.37 ms in Chromium, WebKit and
  Firefox. A wrapped paragraph of about 230 characters costs 0.8, 5.3 and
  1.8 ms; WebKit is slow at one `Range` per cluster, and finding breaks by
  binary search would remove most of that.

Text systems may be used from any thread elsewhere, but this one needs the
DOM, so it only works on the main thread, and the conformance harness declares
`GR_CONF_THREADS` unsupported. Moving wasm into a worker would need an
independent layout engine (for example HarfBuzz with bundled fonts).

## Hosting and events

- `gr_web_attach_to_element(scene, target)` creates the host element inside
  an application-provided container and returns an interactive
  `gr_scene_host`. Like Emscripten's html5 functions, target is a name
  registered in `specialHTMLTargets` (an exact element) or a CSS selector.
- Frame times are animation-frame timestamps on the `performance.now()` clock,
  which is also `now`; the refresh interval is estimated from consecutive
  frames.
- `GR_EVENT_VIEWPORT` comes from a `ResizeObserver`, and the attach reports it
  synchronously. A `devicePixelRatio` change updates the Scene's backing scale
  and reports a viewport; it is caught by a resolution media query, window
  `resize` and a check at each tick, because emulated scale changes fire
  neither of the first two.
- Pointer events from mice and pens map directly: pressure, buttons (also
  chorded presses, which arrive as moves), pointer capture during drags,
  crossings deferred until the last button is up, and click counts (500 ms,
  5 points). A press focuses the host (`tabindex="0"`). Touch is not reported
  yet.
- `wheel` pixel deltas are points and line or page deltas lines; there are no
  phases. Browsers do not tell wheels and touchpads apart.
- Keys: `KeyboardEvent.code` uses the W3C names that `gr_key_code` uses, and
  named `key` values map onto `gr_key`. The unmodified character comes from the
  keyboard layout map where the browser offers one (Chromium), else from the
  lowercased key value. Composition keys report `GR_KEY_PROCESS`.
- A consumed event prevents its default action (scrolling, focus moves, the
  context menu after a consumed secondary press). Cursors are CSS `cursor`.
- Text input (IME) would come later, through `EditContext` or a hidden text
  field.

## Testing

There is no offscreen readback on the Web. `graphite.conformance.web`
(`conformance/web.c` and `web.js`) runs the shared suite in headless Chromium
through `tests/web/run.mjs`:

- Each attached Scene gets a 100 × 100 container registered in
  `specialHTMLTargets`. A capture shows only that container and asks the
  runner for a screenshot (`Page.captureScreenshot` over CDP, on a transparent
  background), which the runner decodes and premultiplies in node.
- `gr_conf_harness.capture` is synchronous and screenshots are not, so the
  program is built with JSPI and the capture suspends until the runner
  answers. The scale changes through `Emulation.setDeviceMetricsOverride`.
- Captures land in Emscripten's in-memory file system; at exit the runner
  copies them to `target/conformance/web`.
- The harness uses the Web text system and declares `GR_CONF_THREADS`
  unsupported, so the text thread-safety case reports as skipped.

Result: 52 passed and 1 skipped at 1×, 1.5× and 2×. Only Chromium runs the
suite, because the runner uses CDP.

## Lessons learned

- **Element boundaries move text.** Chromium sizes each inline element to whole
  layout units (1/64 px), so splitting a run into two spans that differ only
  in color made "Paint only" 0.016 px wider. Measurement therefore splits only
  where the layout changes.
- **Glyph ranges cannot be drawn by making the rest transparent** without
  splitting spans (see above), and positioning each cluster on its own breaks
  shaping. Drawing the whole line and clipping to the chosen glyphs' ink keeps
  every glyph where the layout drew it; clipping to advances instead let a
  neighbor's overhang in (W into g).
- **Emscripten turns regular expression properties of library objects into
  empty objects.** Library objects are copied as source and functions survive,
  so character classes in `text.js` are functions.
- **A line's strut is its block's font.** With the base style's 14 px font on
  every line block, 11 px text got 14 px lines; lines now take the font where
  they start.

- **Do not call into C through `callUserCallback` from event listeners.** After
  the callback it exits the runtime when nothing keeps it alive, and DOM events
  also fire while C is calling in: `focus()` dispatches `focus` synchronously,
  and the first viewport report happens during attach. In a program built with
  `EXIT_RUNTIME` that ended the runtime in the middle of `main`. The runtime's
  own wrapper only handles Emscripten's exit and unwind exceptions.
- **Emulated `devicePixelRatio` changes fire no media query `change` and no
  `resize`** (CDP `Emulation.setDeviceMetricsOverride` in Chromium); only the
  value changes. The tick checks it before every frame.

- **CSS box sizes are fixed point.** Chromium stores widths, heights and border
  widths in 1/64 px. At 1.5×, a snapped size such as 40.6667 pt truncates to
  60.98 device pixels, so `edges_and_borders_are_crisp` found partly covered
  pixels at the edges of a CSS box with a CSS border. Transforms and SVG
  geometry are floating point and stay crisp, so nothing painted relies on box
  sizes.

- **SVG `<text>` is not a faithful replay in WebKit.** Word spacing differs
  from HTML (6–18 % of pixels), while Chromium and Firefox match. Text
  stays HTML.
- **WebKit's `Range.getClientRects` adds a zero-width rect at the end of the
  previous line** for the first cluster after a line break. Use the rects with
  a nonzero width.
- **WebKit's cluster boxes are taller than the line pitch** (18.8 against 18
  px), so a line's boxes overlap the next line's. Line detection therefore
  cannot test "below the previous bottom".
- **Divs snap their edges to device pixels.** A rectangle at fractional
  coordinates drawn as a div differs from Canvas2D and SVG by hundreds of
  pixels, with no anti-aliasing. SVG matches Canvas2D exactly.
- **`clip-path: path()` on a zero-size wrapper clips everything inside it,**
  including content far outside the wrapper's box, in all three engines.
- **`filter: blur(σpx)` is a Gaussian with σ in CSS px.** For σ = 8, the
  1σ crossings measure 7.7–7.9 at 1× and 2×. Under `scale(2)`, σ = 4
  measures 7.8 at 2×. WebKit measured 9.4 at 1×, so small transformed blurs
  are less precise there.
- **WebKit skips drawing a `<foreignObject>` that has both an x/y offset and
  `opacity`.** This does not affect the design above, which puts no text in
  SVG. Position and fade a wrapping `<g>` if foreignObject is ever needed.

## Known gaps

- **No offscreen rendering.**
- No text input (IME) or touch yet.
- The text system works on the main thread only, glyph identities are
  approximations (one glyph per grapheme cluster), and grapheme directions
  come from a simplified bidi resolution.
- Trimming a dashed shape stroke.
- Not yet verified: incremental update performance, carets and hit testing,
  conformance in WebKit and Firefox, Safari itself (rather than Playwright's
  WebKit), and fonts on Windows and Linux browsers.

## Plan

1. **Toolchain** (done): an Emscripten configuration in CMake, the backend
   skeleton with a browser test, and CI running both.
2. **Rendering** (done): host attachment, rAF commits, the
   command stream, layers, drawn contents, and Playwright-driven conformance.
3. **Events and examples** (done apart from IME and touch): input, frame,
   viewport and focus events, and a Web shell under `examples/shared/platform`
   with the examples driven by animation frames rather than a blocking loop.
4. **Text** (done): the DOM text system (layout, metrics, truncation, carets,
   hit testing, range rectangles and approximated glyphs) and its replay.
5. **Gaps**: IME and performance work.
