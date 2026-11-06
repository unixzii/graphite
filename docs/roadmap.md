# Status and roadmap

## Done

- **Core model**: scenes, layer handles with reference counting, dirty
  tracking with dependency propagation, the two-phase commit, `CommitBatch`
  and resolved values, pixel snapping, `Canvas` → `DisplayList`, and the
  `testing::Recorder` / `MirrorTree`.
- **macOS backend**: layer mapping, run-loop commit scheduling, CoreGraphics
  replay, view and layer hosting with backing-scale tracking, and `CARenderer`
  offscreen rendering.
- **GTK backend** (Linux): per-layer render-node caching, DisplayList → GSK
  nodes (no Cairo), frame-clock commits in the layout phase, and
  fractional-scale tracking. Passes the whole conformance suite with the
  Vulkan, GL and Cairo renderers, and has a real-window test.
- **Conformance suite**: 24 semantic and 6 measured cases at 1×, 1.5× and 2×;
  macOS and GTK pass all of them. Shadow σ is calibrated for both backends.
  `run_main` serves main-thread-bound backends.
- **Platform-neutral demo** with `--selftest` and `--snapshot`.

## Next

1. **Resource release notifications**: let backends evict native caches
   keyed by `ResourceId`. Both backends currently share image textures
   through per-layer reference counts. Caching paths or drawings across
   layers would need explicit notifications.
2. **Text**: `AttributedString`, `TextLayout` (measurement, line metrics,
   index/position queries, max lines, truncation) and `Canvas::draw_text`.
   Design it against CoreText and Pango side by side. See
   [testing](testing.md#text-future) for the test strategy.
3. **Tier C review**: generate a side-by-side page from
   `target/conformance/*`, and keep same-backend golden snapshots.

## Later

- Headless Linux testing (for example `mutter --headless`) and CI.
- Conformance cases for gradient-painted shapes (GTK supports them; Core
  Animation does not yet).
- Gradient paints on `ShapeLayer` for Core Animation (a gradient layer masked
  by the shape).
- An image sampling (magnification/minification filter) property.
- Hit testing as a pure geometric query in core.
- A Windows backend (Windows.UI.Composition + Direct2D + DirectWrite).
- A Web backend (DOM + Canvas2D), tier 2.
- Optional capabilities with per-platform fallback: conic gradients,
  content filters, blend modes, backdrop blur.
- A compositor-driven keyframe animation primitive (Core Animation and
  WinComp run animations off the main thread).
- A C ABI (`cbindgen`) for non-Rust consumers.

## Open questions

- Final minimum platform versions (proposed: macOS 14, Windows 11, GTK 4.22).
- Whether text on the web should use the DOM or a Rust layout engine.
