# Platform notes

These are facts about each platform's rendering stack that shaped the
design. Unless marked otherwise, they were checked against official
documentation (learn.microsoft.com, docs.gtk.org and the GTK NEWS file,
developer.apple.com and SDK headers) in November 2026. Items marked
*(undocumented)* come from source code or observation and must be verified
by measurement.

## Chosen technologies

| Platform | Compositing (retained) | Immediate drawing | Text (planned) | Host |
|---|---|---|---|---|
| macOS | Core Animation (`CALayer`) | CoreGraphics | CoreText | layer-hosting `NSView`, or any `CALayer` |
| Windows 11 | Windows.UI.Composition, used from Win32 via `DesktopWindowTarget` | Direct2D into `CompositionDrawingSurface` | DirectWrite | child `HWND` |
| Linux | GTK 4: a custom `GtkWidget` emitting `GskRenderNode`s | GSK path nodes; Cairo rasterized into textures as a fallback | Pango (`GskTextNode`) | `GtkWidget` |
| Web (tier 2) | DOM + CSS compositing | Canvas2D (`OffscreenCanvas`) | DOM or canvas text | `HTMLElement` |

Proposed minimum versions (not final): macOS 14, Windows 11, GTK 4.22.
Ubuntu 24.04 ships GTK 4.14, Ubuntu 26.04 ships 4.22, Debian 13 ships 4.18,
and Homebrew ships 4.24.

## Capability matrix

| Capability | macOS | Windows 11 | GTK 4.22+ | Web | In graphite |
|---|---|---|---|---|---|
| Uniform corner radius + masked corners + clip | ✅ | ✅ `RectangleClip` (build 20348) | ✅ rounded clip | ✅ | core |
| Per-corner radii | ❌ | ✅ | ✅ (elliptical) | ✅ | not supported |
| Continuous ("squircle") corners | ✅ | ❌ | ❌ | ❌ | not supported |
| Alpha mask of a subtree (not nested) | ✅ | ✅ `VisualSurface` + `MaskBrush`, costly | ✅ mask node | 🟡 | core |
| Luminance mask | ❌ | 🟡 effect | ✅ | ✅ | not supported |
| Outer shadow | ✅ | ✅ `DropShadow` | ✅ shadow / outset shadow nodes | ✅ | core |
| Linear and radial gradients | ✅ | ✅ | ✅ | ✅ | core |
| Conic gradient | ✅ (CG: macOS 14) | ❌ (neither Composition nor D2D) | ✅ | ✅ | later, optional |
| Content filters (blur, color matrix) | 🟡 CIFilter, forces in-process rendering | ✅ effects | ✅ | ✅ | later, optional |
| Blend modes | 🟡 CIFilter, in-process | 🟡 21 of 26 modes | ✅ | ✅ | later, optional |
| Backdrop blur inside the tree | ❌ no public layer API | ✅ `CompositionBackdropBrush` | ✅ copy/paste nodes (4.22) | ✅ | later, optional |
| preserve-3d | 🟡 `CATransformLayer` only | *(undocumented)* not flattened, no sorting | ❌ | ✅ | not supported |
| Path boolean ops | ✅ (macOS 13) | ✅ D2D | ❌ | ❌ | to be done in core |
| Animations off the main thread | ✅ out-of-process | ✅ DWM | ❌ | 🟡 transform/opacity only | deferred |
| Wide gamut / HDR | ✅ | 🟡 FP16 surfaces only | 🟡 opt-in color management | 🟡 | out of scope |

## macOS

- `CALayer` has a single `cornerRadius` with `maskedCorners`; there are no
  per-corner or elliptical radii, even in the macOS 27 SDK.
  `NSViewCornerConfiguration` (AppKit 27) applies to views only.
- `filters`, `backgroundFilters` and `compositingFilter` need CIFilter and
  `layerUsesCoreImageFilters`, which moves the tree to in-process rendering.
  `backgroundFilters` is ignored on the root layer. `NSVisualEffectView` and
  `NSGlassEffectView` are views and cannot live inside a layer-hosting view.
  `CABackdropLayer` is private.
- `mask`: alpha only, no nesting, and the mask must have no superlayer.
- With `masksToBounds`, a layer's own shadow is clipped; graphite's backend
  splits outer and body layers to avoid this.
- `render(in:)` ignores 3D, filters and masks. Use `CARenderer` with a Metal
  texture for tests. `CGWindowListCreateImage` is obsolete since macOS 15.
- CoreText has no max-lines API and supports COLR v0, sbix and SVG color
  fonts, but not COLR v1. Subpixel anti-aliasing was removed in 10.14.
- EDR APIs moved quickly: `wantsExtendedDynamicRangeContent` (14) is
  deprecated in 26 in favor of `preferredDynamicRange`.
- See [the macOS backend](backends/macos.md) for lessons learned while
  implementing it.

## Windows

- Windows.UI.Composition from Win32 needs a `DispatcherQueue` on the thread
  (`CreateDispatcherQueueController`), then
  `ICompositorDesktopInterop::CreateDesktopWindowTarget(hwnd, isTopmost)`.
- Visual units are **pixels**; content does not scale with DPI. Put a DPI
  scale on the root visual, create drawing surfaces at pixel size, and
  re-rasterize on `WM_DPICHANGED`.
- The composition order of `AnchorPoint`, `CenterPoint` and `TransformMatrix`
  is undocumented. Compose graphite's matrix into `TransformMatrix` and
  `Offset` yourself.
- `Visual.Clip` does not clip children to the parent's size implicitly.
  `RectangleClip` has per-corner radii and needs build 20348.
  `BorderMode::Soft` enables anti-aliased edges.
- `DropShadow` works on `SpriteVisual` and on `LayerVisual`, where it shadows
  the flattened subtree. Whether `BlurRadius` is σ is undocumented; calibrate
  it with the conformance suite (measured: 3σ).
- Shapes: `ShapeVisual` + `CompositionSpriteShape` + `CompositionPath`,
  built from D2D geometry through `IGeometrySource2DInterop`. They support
  `TrimStart`/`TrimEnd`. The fill rule comes from the D2D geometry.
- Drawing surfaces: `ICompositorInterop::CreateGraphicsDevice(d2dDevice)`,
  then `BeginDraw`, which returns an `ID2D1DeviceContext`. After device
  loss, surface contents are not preserved: redraw on
  `RenderingDeviceReplaced`.
- Brush colors are 8-bit sRGB. Neither Composition nor Direct2D has conic
  gradients.
- Direct2D downgrades ClearType to grayscale on any target with alpha.
- DirectWrite has no max-lines API; use `SetMaxHeight` with trimming.
  System COLR v1 support is still marked pre-release.
- `Microsoft.UI.Composition` (Windows App SDK) is nearly identical but runs
  in-process and cannot sample external content. Keep the system API as the
  default.
- Airspace: composition content sits either above or below a window's child
  `HWND`s as a whole. Host graphite in its own child `HWND`.
- Measured while implementing the backend (Windows 11 build 26300, ARM64
  VM): `DropShadow.BlurRadius` is 3σ; a `ContainerVisual`'s opacity is
  group opacity; `ShapeVisual` clips to its size; a brushless
  `SpriteVisual` casts its `DropShadow` in the shape of `DropShadow.Mask`;
  Windows.Graphics.Capture returned empty frames on the VM while
  `PrintWindow(PW_RENDERFULLCONTENT)` reads composed windows, even
  off-screen. See [the Windows backend](backends/windows.md#lessons-learned).

## GTK

- There is no public retained layer tree. A custom widget emits a
  `GskRenderNode` tree in `snapshot()`. Nodes are immutable and threadsafe,
  so they can be built off the main thread; Pango font maps are not
  threadsafe.
- There is no cross-frame cache of rendered node results: damaged regions
  are re-rendered every frame. Reusing node pointers still enables damage
  diffing and texture, glyph and (since 4.19.2) path-mask caching.
- **Never use Cairo nodes for contents.** They are replayed on the CPU every
  time they render *(source)*. Rasterize Cairo output into a
  `GdkMemoryTexture` and cache it.
- Paths (4.14): `GskPath`, `GskStroke`, and fill/stroke nodes. Trim strokes
  with `GskPathMeasure` points and `gsk_path_builder_add_segment`. There are
  no boolean operations.
- Clipping to an arbitrary path needs a mask node with a fill node as the
  mask; there is no path clip node.
- `GskRoundedRect` corners are elliptical. 3D transforms are drawn in tree
  order without depth sorting *(source)*.
- Shadow blur radius semantics are inconsistent *(source)*: the blur node
  treats radius as 2σ, while the GPU shadow node appears to use σ.
  Calibrate with the conformance suite.
- Backdrop effects: `CopyNode` and `PasteNode` (4.22) power CSS
  `backdrop-filter`.
- Scale: `gdk_surface_get_scale` (a double, 4.12) gives the fractional scale.
  Rasterize at logical size × scale and draw with `texture_scale` nodes. X11
  has integer scales only.
- The new renderers place fractional coordinates exactly, which blurs
  hairlines. Graphite's pixel snapping matters more here.
- `GtkWidget:overflow` defaults to visible; set it to hidden to clip to the
  allocation.
- Animation is main-thread only (`GdkFrameClock`, tick callbacks).
- Offscreen: `gsk_renderer_render_texture` renders a node tree into a
  texture, which suits the conformance harness.
- Pango: `set_height(-n)` limits lines only when ellipsizing; line height
  needs Pango 1.50. Pango 2 is stalled.
- Color management is opt-in (`GDK_DEBUG=color-mgmt`) and `GdkColor` is
  private.
- Measured while implementing the backend (GTK 4.22.4): `GskNglRenderer`
  no longer realizes (use `GskGLRenderer`); the blur node's radius is 2σ;
  GTK falls back to GL for windows when Vulkan lacks dmabuf support. See
  [the GTK backend](backends/gtk.md#lessons-learned).

## Text stack

| Layer | macOS | Windows | GTK | Web |
|---|---|---|---|---|
| Font matching and fallback | CTFontDescriptor, cascade lists | IDWriteFontSet, IDWriteFontFallback | fontconfig via Pango | CSS font-family, FontFace |
| Shaping | CoreText | DirectWrite | HarfBuzz | not exposed |
| Line breaking, bidi, layout | CTTypesetter | IDWriteTextLayout | PangoLayout | DOM; canvas is single-line |
| Rasterization | CG | D2D `DrawGlyphRun` | GSK glyph atlas (via Cairo) | `fillText` / DOM |

The plan is to use the native stack per platform behind an
`AttributedString → TextLayout` API. Max-lines and truncation are
implemented in core, since no platform offers both. On the web, layout is
implemented with `Intl.Segmenter` and `measureText`, at reduced fidelity.
