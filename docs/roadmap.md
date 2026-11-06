# Status and roadmap

## Done

- **Core model**: scenes, layer handles with reference counting, dirty
  tracking with dependency propagation, the two-phase commit, `CommitBatch`
  and resolved values, pixel snapping, `Canvas` → `DisplayList`, and the
  `testing::Recorder` / `MirrorTree`.
- **macOS backend**: layer mapping, run-loop commit scheduling, CoreGraphics
  replay, view and layer hosting with backing-scale tracking, and `CARenderer`
  offscreen rendering.
- **Conformance suite**: 22 semantic and 5 measured cases at 1× and 2×; macOS
  passes all of them. Shadow σ is calibrated for Core Animation.
- **Platform-neutral demo** with `--selftest` and `--snapshot`.

## Next

1. **GTK backend** (`crates/graphite-gtk`), aligned with macOS through the
   conformance suite. It needs `brew install gtk4` locally on macOS, but must
   also be run on Linux, where Wayland defaults to the Vulkan renderer while
   macOS GTK uses GL. Work items:
   - per-layer render-node caching;
   - DisplayList → GSK nodes, with a Cairo → texture fallback;
   - `gdk_surface_get_scale`;
   - calibrating shadow σ.
2. **Resource release notifications**: let backends evict native caches
   keyed by `ResourceId` (GSK will want to cache `GskPath` and `GdkTexture`).
   macOS avoids this today with per-node reference counts.
3. **Text**: `AttributedString`, `TextLayout` (measurement, line metrics,
   index/position queries, max lines, truncation) and `Canvas::draw_text`.
   Design it against CoreText and Pango side by side once GTK runs. See
   [testing](testing.md#text-future) for the test strategy.
4. **Tier C review**: generate a side-by-side page from
   `target/conformance/*`, and keep same-backend golden snapshots.

## Later

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
