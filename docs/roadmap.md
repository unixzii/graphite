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
  conformance suite and has a real-window test. Text (DirectWrite) is not
  started, like on the other backends.
- **UI rendering basics**: crisp edges at fractional scales (sizes and
  border widths snap to device pixels), shadow spread.
- **Conformance suite**: 26 semantic and 7 measured cases at 1×, 1.5× and 2×;
  macOS, GTK and Windows pass all of them. Shadow σ is calibrated for all
  three backends.
  The runner executes on the platform-required thread.
- **Platform-neutral demo** with two scenes (`showcase`, `ui`), `--selftest`,
  `--fps` and `--snapshot`.

## Next

1. **Resource release notifications**: let backends evict native caches
   keyed by resource identity. All backends currently share image textures
   through per-layer reference counts. Caching paths or drawings across
   layers would need explicit notifications.
2. **Text**: attributed text and opaque text layouts (measurement, line metrics,
   index/position queries, max lines, truncation) and Canvas text drawing.
   Design it against CoreText, Pango and DirectWrite side by side. See
   [testing](testing.md#text-future) for the test strategy.
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
   `CALayer.rasterizationScale`.
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
- Windows: device-loss recovery (`RenderingDeviceReplaced`), masks that
  extend past the masked layer's box, and testing on x64 hardware.
- A Web backend (DOM + Canvas2D), tier 2.
- Optional capabilities with per-platform fallback: conic gradients,
  content filters, blend modes, backdrop blur.
- A compositor-driven keyframe animation primitive (Core Animation and
  WinComp run animations off the main thread).

## Open questions

- Final minimum platform versions (proposed: macOS 14, Windows 11, GTK 4.22).
- Whether web text should use DOM layout or an independent layout engine.

## Text design direction

A backend layout operation will accept text and constraints and return an opaque,
reference-counted platform layout. Canvas will draw it; measurement, line data,
hit testing, caret and selection queries belong to the layout. Internal query
structures may be lazy. Core will not cache layouts; clients/backends own policy.
Paint-only attributes should not alter shaping/cache keys. Finalize index and
affinity semantics, ligature paint boundaries and threading before adding APIs.
