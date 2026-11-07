# Status and roadmap

## Done

- **Core model**: scenes, layer handles with reference counting, dirty
  tracking with dependency propagation, the two-phase commit, `gr_commit_batch`
  and resolved values, pixel snapping, `Canvas` → `DisplayList`, and the
  recording backend and mirror tree tests.
- **macOS backend**: layer mapping, run-loop commit scheduling, CoreGraphics
  replay, view and layer hosting with backing-scale tracking, and `CARenderer`
  offscreen rendering.
- **GTK backend** (Linux): per-layer render-node caching, DisplayList → GSK
  nodes (no Cairo), frame-clock commits in the layout phase, and
  fractional-scale tracking. Passes the whole conformance suite with the
  Vulkan, GL and Cairo renderers, and has a real-window test.
- **Windows backend**: Windows.UI.Composition visuals from Win32 (child
  `HWND` or any `ContainerVisual`), Direct2D replay into drawing surfaces,
  shapes as `ShapeVisual`s, native group opacity, shadows and masks,
  DispatcherQueue commit scheduling and DPI tracking. Passes the whole
  conformance suite and has a real-window test.
- **UI rendering basics**: crisp edges at fractional scales (sizes and
  border widths snap to device pixels), shadow spread.
- **Layer blur** (`gr_layer_set_blur`) on all three backends. Core Animation
  measures σ in layer space, GTK and Windows in screen space.
- **Conformance suite**: 28 semantic, 8 measured and 18 text cases at 1×, 1.5×
  and 2×; macOS, GTK and Windows pass all of them. Shadow σ is calibrated
  for all three backends.
  The runner executes on the platform-required thread.
- **Examples**: one executable per scene (`showcase`, `ui`, `stress`, `glyphs`, `input`) on a
  shared native shell with `--selftest`, `--fps` and `--snapshot`.
- **Events**: every attachment returns a `gr_scene_host`. View, widget and HWND
  hosts deliver frames paced by the display, viewport, focus, pointer (mouse and
  pen), scroll and key events through one handler, and apply cursors. Text
  input (IME), touchscreens and gestures are next.

## Next

1. **Resource release notifications**: let backends evict native caches
   keyed by resource identity. All backends currently share image textures
   through per-layer reference counts. Caching paths or drawings across
   layers would need explicit notifications.
2. **Text**: layout, measurement, drawing, carets and selection work on all
   three backends (M1 and M2 below). Remaining text work is under
   [Text](#text): consistency gaps between backends, then the "Later" list.
3. **Tier C review**: generate a side-by-side page from
   `target/conformance/*`, and keep same-backend golden snapshots.

## Rendering basic UI elements: remaining gaps

Found while reviewing what toolkits need from the renderer (text aside),
roughly in priority order:

1. **Rasterization scale of drawn contents.** The Core Animation backend
   rasterizes drawings at the backing scale only, ignoring scale transforms
   on the layer and its ancestors, so zoom and scale animations blur them
   (GSK draws them as vector nodes and is not affected). Resolve an
   effective scale in core, or offer a `rasterization_scale` override like
   `CALayer.rasterizationScale`. The same effective scale would let GTK and
   Windows measure layer blur σ in layer space, as Core Animation does.
2. **Image sampling.** A magnification/minification filter property:
   nearest for pixel art and QR codes, trilinear (mipmaps) for downscaled
   thumbnails and icons. Native on Core Animation, GSK and WinComp.
3. **Gradient paints on shape layers for Core Animation.** They degrade to
   the first stop today (a gradient layer masked by the shape would fix it),
   while GTK draws them; add conformance cases once both agree.
4. **Inset shadows**, for recessed fields and pressed states. GSK has a
   native node; Core Animation needs an emulation (a shadow cast by a large
   rectangle with the rounded box cut out, clipped to the box).
5. **Nine-slice images** (`contentsCenter`), for stretchable skinned assets.
6. **Large drawn contents.** Partial redisplay (`set_needs_display_in_rect`)
   and tiling for very large drawn layers: a single bitmap has a size limit
   in Core Animation (around 8 000 pt tall at 2×). Backends already decide
   when to draw; this adds a dirty / requested rect to `gr_draw_info` so they
   can draw tiles (`CATiledLayer` on Core Animation).
7. **Image decoding** from encoded bytes through the platform decoders
   (ImageIO, gdk-pixbuf/glycin, WIC, `createImageBitmap`). Until then,
   applications decode images before creating resources.

Not planned: **hit testing and coordinate conversion** on the layer tree.
Layers are the output of a toolkit; the toolkit routes input with its own
widget tree, which knows what is interactive and where. This only needs
revisiting if compositor-driven animations arrive, because the on-screen
position of an animating layer would then live in the compositor (like
`CALayer.presentation()`).

## Later

- Headless Linux testing (for example `mutter --headless`) and CI.
- Windows: device-loss recovery (`RenderingDeviceReplaced`) and testing on
  x64 hardware.
- A Web backend (DOM + Canvas2D), tier 2.
- Optional capabilities with per-platform fallback: conic gradients,
  other content filters, blend modes, backdrop blur.
- A compositor-driven keyframe animation primitive (Core Animation and
  WinComp run animations off the main thread).

## Open questions

- Final minimum platform versions (proposed: macOS 14, Windows 11, GTK 4.22).
- Whether web text should use DOM layout or an independent layout engine.

## Text

The API is [text.h](../include/graphite/text.h). Core implements it (attributed
text, the text system and layout wrappers, and Canvas recording), and a fake
text system tests it. Each backend provides a native text system covering M1
and M2 below: CoreText (`gr_macos_text_system_new`), Pango
(`gr_gtk_text_system_new`) and DirectWrite (`gr_windows_text_system_new`).

### Design

- A **text system** (`gr_text_system`) is a platform object separate from the
  Scene backend, so text can be measured before a Scene has a backend and laid
  out while a drawer runs (inside `apply`). Platforms create it
  (`gr_macos_text_system_new` and its GTK and Windows counterparts) on top of
  the core constructor `gr_text_system_new(vtable, userdata)`.
- `gr_text_system_layout` takes an immutable `gr_attributed_text` and
  `gr_text_layout_options` and returns a `gr_text_layout`. The platform builds
  that layout with
  `gr_text_layout_new(system, text, options, vtable, userdata)`. Core handles
  reference counting, identities and argument validation, and forwards queries
  to the vtable.
- **Lazy queries**: metrics, lines, carets and hit testing all go through the
  vtable, and core caches nothing. A caller that only draws never pays for
  queries it does not make. Platforms may defer native layout until the first
  query or draw.
- **Indices** are UTF-8 byte offsets. Core validates UTF-8 and code-point
  boundaries, keeps spans in order for platforms to apply natively (later spans
  win), and offers UTF-16 conversion for CoreText and DirectWrite. Caret stops
  are grapheme cluster boundaries; affinity picks which side of a line break a
  caret is on.
- **Paint-only color**: `gr_canvas_draw_text_layout` takes a default color for
  runs without their own, so a color change does not need a new layout. Only
  spans set colors (the base style's color is ignored), so the style has a
  single optionality mechanism: the field mask. Each
  platform supports this natively:
  - CoreText: `kCTForegroundColorFromContextAttributeName`
  - GTK: `gtk_snapshot_append_layout`
  - DirectWrite: the default fill brush
- **Threading**: texts, systems and layouts are immutable resources that any
  thread may use. Vtable callbacks can run concurrently, so each
  implementation synchronizes its lazy native state.
- **Replay**: a backend draws layouts whose vtable it recognizes and skips
  others.

### M1: layout, measurement and drawing

- Core (done): attributed text (validation, span copies, UTF-16 mapping),
  the text system and layout wrappers with caret-stop normalization, the
  `GR_DRAW_TEXT_LAYOUT` op with Canvas recording, and `tests/text.c` with a
  fake text system.
- Styles: family (including generic names), size, weight, slant, optional
  color, underline/strikethrough, letter spacing.
- Options: max width, wrap mode, max lines, end ellipsis, alignment, base
  direction.
- Queries: metrics (size, line count, first/last baseline, visible range,
  truncated) and per-line range, bounds, baseline, ascent and descent.
- Native work per platform:
  - **macOS** (done): see [the Core Animation backend notes](backends/macos.md#text).
  - **GTK** (done): see [the GTK backend notes](backends/gtk.md#text).
  - **Windows** (done): see [the Windows backend notes](backends/windows.md#text).
- Demo: the `ui` scene shows real labels and a wrapped, styled paragraph with
  Chinese and emoji (done on all three backends).

### M2: carets and hit testing

- Caret stops (next/previous), hit testing, caret rectangles with affinity, and
  range rectangles for selections (done on all three backends). Bidirectional text is the hard part: split
  carets and visual-order range rectangles need care on every platform.
- Native APIs:
  - CoreText: `CTLineGetStringIndexForPosition` and
    `CTLineGetOffsetForStringIndex`
  - Pango: `pango_layout_xy_to_index`, `pango_layout_get_cursor_pos` and
    `pango_layout_line_get_x_ranges`
  - DirectWrite: `HitTestPoint`, `HitTestTextPosition` and `HitTestTextRange`

### Glyphs (done)

`gr_text_layout_get_glyphs` lists a line's glyphs in visual order with their
position, advance, ink bounds, cluster and flags (right-to-left, ellipsis), and
`gr_canvas_draw_text_glyphs` draws a glyph range of one line with the layout
box at any origin, so glyphs can be animated or placed one by one. Each glyph
carries its glyph index, em size and a font face identity (face, variation
axis values and synthetic bold or oblique, not size) that compares across
layouts of one text system, so equal glyphs can be matched between layouts.
Glyph drawing leaves out underline and strikethrough. Per-glyph positions in
one call (text on a path) and glyph outlines would come next, through a font
object reachable from the font identity.

### Testing

The conformance suite's `text` cases (`conformance/text.c`) run against every
backend that provides a text system. Fonts differ per platform, so they assert
relations and invariants rather than exact metrics: wrapping inside the width,
line ranges covering the text, `max_lines` with the truncated/visible metrics,
ellipsis hit testing and the ellipsis at paragraph breaks, alignment by
paragraph direction, monotonic carets that round-trip through hit testing,
caret affinity at direction changes, RTL hit testing, grapheme cluster stops,
bidi selection pieces, letter spacing between graphemes, paint-only color,
weight, italic and strikethrough, concurrent queries, drawing (upright, inside
its box, no color leaks, underline), and glyphs (clusters for emoji and RTL,
ellipsis glyphs, glyph ranges redrawing the layout pixel for pixel, placement
by origin). Writing these found and fixed
backend disagreements: macOS and GTK placed bidi carets on the opposite
edges, GTK and Windows dropped the ellipsis when the cut fell at a paragraph
break, Windows added spacing after the last grapheme and measured lines
ending in separators too short.

Register a bundled test font internally to reduce variance. Use same-backend
golden snapshots for rasterization. See also [testing](testing.md#text-tests).

### Later

- Line height and paragraph spacing (each platform defines them differently).
- Locale and language (CJK glyph selection), font features, variable font
  axes.
- Loading fonts from bytes or files, and font fallback control.
- Inline attachments (images or views in text), and paths from text.
- Gradient and other paint fills for text: clip-to-text on CoreGraphics, mask
  nodes on GSK, brushes on D2D.
- Visual (left/right) caret movement in bidi text, word and line boundaries,
  justification, start/middle truncation.
- Text drawing performance: shared glyph caches across layers, and the
  rasterization scale gap above (text blurs under zoom on Core Animation).

### Open questions

- `GR_TEXT_DIRECTION_AUTO` resolves per paragraph. DirectWrite only has a
  layout-wide reading direction, so Windows uses the first strong character
  of the whole text; mixed-direction paragraphs need splitting or emulation.
- Whether ellipsis truncation should keep trailing whitespace in the visible
  range, which matters for consistent `visible` ranges.
- How much Pango locking costs under concurrent use, and whether layouts
  should instead be pinned to a thread on GTK.
- How a backend should treat a layout from a different text system: skip it,
  as drafted, or fall back to paths.
