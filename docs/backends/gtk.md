# GTK / GSK backend

Implementation, hosting API and build instructions: [backend guide](../../src/platforms/gtk/README.md).
Public declarations: [`gtk.h`](../../include/graphite/gtk.h).

## Lessons learned

- **The NGL renderer is gone in GTK 4.22.** `GskNglRenderer` refuses to
  realize ("Please use the GL renderer instead"); use `GskGLRenderer`.
- **Blur radius = 2σ for both blur and outset shadow nodes.** Calibrated by
  `shadow_blur_matches_sigma` (outset: σ measured 4.03 for σ = 4) and
  `shadow_blur_matches_sigma_without_background` (blur: 3.8 at the 1σ point
  and 3.5 at the 2σ point; GSK's blur kernel has slightly short tails).
- **virgl breaks GSK's GL blur node.** In the QEMU/virgl VM the GL renderer
  draws blur nodes wrongly. At 1× and 2× the child comes out unblurred and
  hard-edged; at the desktop's 1.333 it is spread far too wide and cut off
  at the node bounds. The same nodes are correct with Vulkan and with GL on
  llvmpipe, and outset shadow nodes are correct everywhere. GTK picks GL for
  windows there ("Not using Vulkan: no dmabuf support").
- **Venus (virtio Vulkan) sometimes reads back a previous frame.** Offscreen
  `render_texture` + download on Venus occasionally returns stale contents
  (about one run in two of the suite has a failure; `GSK_DEBUG=sync` reduces
  this but does not fix it). Plain color nodes do not trigger it; lavapipe
  never does (15 of 15 clean runs).
- **Never pass a raw matrix to `GskTransform`.** `gsk_transform_matrix` is
  categorized as possibly 3D. The Cairo renderer cannot draw such nodes and
  paints them hot pink (`#FF69B4`), and the GPU renderers take slower paths.
  the affine conversion helper decomposes affine matrices into translate · rotate ·
  skew · scale (checked by a debug assertion); the `skew_transforms_apply`
  case covers the skew.
- **Vulkan windows flicker on Venus.** GTK itself refuses Vulkan there ("no
  dmabuf support") and falls back to GL; forcing `GSK_RENDERER=vulkan`
  bypasses that check. Logs show nothing wrong, the self-test commits every
  frame, and the same node trees render steadily with GL. Together with the
  stale offscreen frames above, this points at the driver. Compare with a
  pure GTK app (`GSK_RENDERER=vulkan gtk4-widget-factory`) or with lavapipe
  (`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`).
- **Download before unrealizing.** A GPU texture's contents belong to the
  renderer's device. Unrealizing first produced empty frames.
- **A busy frame clock hides scheduling bugs.** Right after a window maps,
  frames keep coming, so a missing `request_phase` still commits. The widget
  test waits for the clock to go idle before its second change.
- **Tick callbacks run before the first layout phase.** On the first tick
  the initial scene is not committed yet; the demo self-test starts checking
  from the second tick.
- `gdk_texture_download` gives B8G8R8A8; use `GdkTextureDownloader` with
  `GDK_MEMORY_R8G8B8A8_PREMULTIPLIED`. Rows are top-down.
- GTK operations must run on its main thread after initialization.

## Text

`gr_gtk_text_system_new` (`src/platforms/gtk/text.c`, tested by the
conformance suite's text cases) lays text out with Pango and draws it with
`gtk_snapshot_append_layout`:

- **Pango picks the attribute with the latest start index**, not the one
  inserted last, so spans are resolved here into disjoint segments (base, then
  each span's selected fields in order) before becoming attributes.
- Each layout gets its own `PangoContext` from the system's
  `pango_cairo_font_map_new`, because base direction is a context property.
  Metric hinting and glyph position rounding are off, so layouts do not depend
  on the output scale. Sizes are absolute (logical pixels).
- With `auto_dir` off, Pango does not flip `LEFT`/`RIGHT` for right-to-left
  text, so start and end alignment are mapped for explicit directions.
- **Line limits:** a negative `pango_layout_set_height` limits lines per
  *paragraph*. A positive height with end ellipsization limits the whole
  layout and ellipsizes the last line, which then holds the rest of its
  paragraph; the height is set halfway into the first hidden line. Without an
  ellipsis, the extra lines are hidden by a clip when drawing.
- Pango wraps whenever a width is set, so unwrapped text without an ellipsis
  is laid out without a width and shifted to align inside `max_width`. Unwrapped
  text with an ellipsis uses per-paragraph ellipsization.
- **Pango stops at NUL**, so embedded NULs become spaces (same byte length).
- Hit testing past a wrapped line's end returns the trailing space, not the
  line end, so points outside a line are resolved from its direction.
- Pango maps the ellipsis onto the elided range; its run is found through
  `PANGO_ANALYSIS_FLAG_IS_ELLIPSIS`, and positions inside it collapse onto the
  elision start like on CoreText.
- **Pango only ellipsizes a paragraph it cuts.** When the cut falls at a
  paragraph break (or between unwrapped lines), the last line gets a separate
  "…" layout in its last run's font and color; graphemes are elided from the
  line's end, by clipping the second of two draws, until it fits.
- **Carets come from grapheme edges**, not `pango_layout_get_cursor_pos`: its
  strong and weak cursors follow the paragraph direction, while affinity
  follows the grapheme (`pango_layout_index_to_pos` gives negative widths
  for right-to-left graphemes).
- Pango line lengths exclude paragraph separators; line ranges extend to the
  next line's start to include them.
- Pango is not thread-safe: a mutex per text system serializes layout,
  queries and drawing. GSK still renders the resulting text nodes later on
  GTK's main thread, sharing fonts with any concurrent layout.
- Line heights are taller than CoreText's (about 17 pt vs 15.5 pt at 12.5 pt
  sans), so layouts that budget height by line count should measure first.

## Known gaps

- Text: mixed-direction paragraphs in unwrapped text without an ellipsis share
  one alignment shift.
- Shadows that can show under their layer are a blur of the filled shape,
  re-rendered whenever their region is damaged.
- No headless setup yet (for example `mutter --headless`); the tests need a
  live session.
- Not yet verified: X11, integer-scale monitors, and moving windows between
  monitors with different scales.
