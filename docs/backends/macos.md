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
- **Fractional edges are anti-aliased by Core Animation.** Without pixel
  snapping, an edge at x = 10.3 covers about 70 % of a column.
- Under geometry flipping, `CGImage` contents, `CAGradientLayer` unit space,
  `shadowOffset` and `maskedCorners` all behave y-down. The conformance
  suite verifies each of them.

## Known gaps

- Gradient paints on shape layers fills and strokes degrade to the first
  stop. Doing this properly needs a `CAGradientLayer` masked by the shape.
- No image magnification filter property yet (Core Animation defaults to
  linear filtering).
- `CGImage`s are shared per resource identity through per-node reference counts.
  There is no cross-layer cache for paths or drawings.
- Not yet verified: moving a window between displays with different scales.
