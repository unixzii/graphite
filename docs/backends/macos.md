# Core Animation backend

Implementation, hosting API and build instructions: [backend guide](../../src/platforms/macos/README.md).
Public declarations: [`macos.h`](../../include/graphite/macos.h).

## Lessons learned (keep these in mind)

- **AppKit resets `geometryFlipped` on a layer-hosting view's own layer.**
  The flipped host layer is therefore a sublayer of the view's layer.
  `tests/macos.m` guards this in a real window.
- **`CARenderer` only sees a layer tree after a transaction commit that
  follows `setLayer`.** `gr_macos_host_render` flushes after attaching.
- **`CARenderer`'s Metal texture is bottom-up.** Row 0 is the bottom of the
  screen, so read-back flips rows.
- **`shadowRadius` is the Gaussian σ.** This was measured by
  `shadow_blur_matches_sigma` at 3.93–4.02 for σ = 4.
- **`CAFilter`'s `inputRadius` is the Gaussian σ in layer space.**
  `layer_blur_matches_sigma` measures 3.67–3.85 for σ = 4; scale transforms
  on the layer or its ancestors scale the blur.
- **Fractional edges are anti-aliased by Core Animation.** An edge at
  x = 10.3 covers about 70 % of a column.
- Under geometry flipping, `CGImage` contents, `CAGradientLayer` unit space,
  `shadowOffset` and `maskedCorners` all behave y-down. The conformance
  suite verifies each of them.

## Text

`gr_macos_text_system_new` (`src/platforms/macos/text.m`, tested by the
conformance suite's text cases) lays text out with CoreText:

- Styles become private per-field attributes on a `CFAttributedString`, in span
  order, so later spans win; each attribute run then gets the `CTFont` its
  fields resolve to. Fonts are cached per text system. Generic families use
  the system designs (SF, New York, SF Mono). The italic trait in a descriptor
  is ignored for system fonts, so italics go through
  `CTFontCreateCopyWithSymbolicTraits`.
- Each paragraph gets an explicit base direction. `AUTO` uses the first
  letter (marks, digits and punctuation are skipped) and a table of
  right-to-left blocks, so alignment and CoreText's bidi agree.
- Lines come from a `CTTypesetter`, one paragraph at a time, which keeps
  `max_lines` and ellipses under our control. CoreText breaks between CR and
  LF, so the pair is rejoined. A zero-length range passed to
  `CTTypesetterCreateLine` means "to the end", so empty lines have no `CTLine`.
- **Ellipses are built by hand**: the visible clusters plus a separate token
  line. `CTLineCreateTruncatedLine` maps the token run onto the elided string
  range and returns caret offset 0 for elided indices.
- **Draw glyphs run by run.** After a run with its own color, `CTLineDraw`
  leaves that color in the context, and later runs using
  `kCTForegroundColorFromContextAttributeName` inherit it. Replay resets the
  fill color before each context-colored run, flips the text matrix for the
  y-down CTM and turns off font smoothing (backing stores are transparent).
- **Underlines are CoreText's own, from a second `CTLineDraw` with invisible
  glyphs.** Only `CTLineDraw` draws underlines (`CTRunDraw` never does), and
  they skip descenders (g, y, p). For context-colored runs the underline takes
  the context's **stroke** color, not its fill color, so both are set to the
  draw color first; otherwise those underlines come out black.
- Glyph font identities key on the PostScript name, `CTFontCopyVariation` and
  the font matrix (synthetic oblique). Size matters only through variation
  axes: SF's optical size axis makes 14 pt and 28 pt different faces.
- **Glyph ranges draw with `CTRunDraw` over glyph subranges**, not
  `CTFontDrawGlyphs`. Under the replay's y-down transform, `CTFontDrawGlyphs`
  (with either a flipped text matrix or a flipped CTM) and `CTRunDraw` with a
  flipped CTM place bitmap (sbix) emoji one device pixel off; only `CTRunDraw`
  with the flipped text matrix matches whole-layout drawing.
- Selection rectangles come from `CTLineEnumerateCaretOffsets`, merged per
  line, so a range across a direction change yields one rectangle per visual
  piece. Carets use the same grapheme edges: `CTLineGetOffsetForStringIndex`'s
  primary offset is the opposite edge from the affinity's at a direction
  change.
- Letter spacing is `kCTTrackingAttributeName`, which already goes between
  graphemes only.
- Layout is built on first use under an `os_unfair_lock` and is immutable
  afterwards, so queries are safe from any thread.

## Known gaps

- Text: no font loading from bytes, locale, font features or line height
  yet. Text drawn into a layer is rasterized at the backing scale, so it blurs
  under scale transforms (see the roadmap's rasterization scale gap).
- No image magnification filter property yet (Core Animation defaults to
  linear filtering).
- `CGImage`s are shared per resource identity through per-node reference counts.
  There is no cross-layer cache for paths or drawings.
- Not yet verified: moving a window between displays with different scales.
