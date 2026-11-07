# Windows.UI.Composition backend

Implementation, hosting API and build instructions: [backend guide](../../src/platforms/windows/README.md).
Public declarations: [`windows.h`](../../include/graphite/windows.h).

## Lessons learned

- **`DropShadow.BlurRadius` is 3σ.** Calibrated by
  `shadow_blur_matches_sigma` (with and without a background): for σ = 4,
  4.10–4.12 at the 1σ point and 3.78–3.81 (halved) at the 2σ point, at
  1×, 1.5× and 2×. The root scale scales the blur with everything else.
- **A `ContainerVisual`'s opacity is group opacity.**
- **A `LayerVisual` clips its subtree to its `Size`.** A plain
  `ContainerVisual` never clips, but masks and blur render through a
  LayerVisual, so the backend sizes each one to the subtree's extent
  (`masks_keep_sublayers_outside_bounds`,
  `blur_covers_sublayers_and_bleeds_past_bounds`).
- **Direct2D's Gaussian blur σ is in device pixels**, even under the root
  scale, unlike `DropShadow.BlurRadius`. `layer_blur_matches_sigma` measures
  3.85–4.04 for σ = 4 at 1×, 1.5× and 2× once σ is multiplied by the backing
  scale.
- **`ShapeVisual` clips to its size**, including content at negative
  coordinates. Size it to the geometry.
- **A brushless `SpriteVisual` still casts its `DropShadow`**, shaped by
  `DropShadow.Mask`.
- **Destroying a window that hosted a `DesktopWindowTarget` can unload
  dcomp.dll** while composition objects are still alive; their next
  release jumps into unmapped code (an access violation in the release, far
  from the cause). The context pins dcomp.dll for the life of the process.
  Observed on build 26300; it may be specific to that build or VM.
- **Do not release composition objects from thread-local destructors at
  process exit.** The test process then exited with a stray code (2170)
  after all cases passed. The per-thread context is never dropped.
- **Windows.Graphics.Capture delivered all-zero frames** on the
  development VM, for visuals (`CreateFromVisual`) and windows alike, while
  D3D read-back itself worked. `PrintWindow(PW_RENDERFULLCONTENT)` works
  there, so it is what the read-back uses.
- **`SetTimer` cannot drive 60 Hz animation**: its period is rounded up to
  the system timer resolution, so a 16 ms timer ticks at about 39 Hz. The
  example shell uses the compositor clock and posts updates to the window thread.
- **On the development VM's GPU, Direct2D strokes are not anti-aliased.**
  `DrawGeometry` drew strokes without a single partially covered pixel
  (counted on a test pentagram: 0 of 700 stroke pixels, against 837 of
  1212 with the WARP device), while fills were anti-aliased; the device
  context's mode was `PER_PRIMITIVE`. Strokes are therefore widened and
  filled, which looks the same on correct drivers. Visuals rendered by DWM
  (rotated sprites and shapes, clips) measured as anti-aliased, but edges
  still look rougher than on macOS there, and WinUI 3 applications show
  the same roughness on that VM: treat edge quality on it as a driver
  limitation, and judge it on real hardware.
- **Windows Update reboots the development VM**, which ends the
  interactive session; the tests cannot run until someone logs in again.

## Text

`gr_windows_text_system_new` (`src/platforms/windows/text.cpp`, tested by the
conformance suite's text cases) lays text out with a shared DirectWrite
factory and draws it with `ID2D1DeviceContext::DrawTextLayout`:

- DirectWrite's range setters override earlier calls on overlapping ranges,
  so spans apply natively in order.
- **Colors are drawing effects only while drawing.** D2D brushes belong to a
  device and layouts do not, so span colors become brushes for one
  `DrawTextLayout` call and are removed afterwards. Color fonts are enabled
  and text antialiasing is grayscale (surfaces are transparent).
- **There is no line limit.** A max height covering the first `max_lines`
  lines plus half the next, with an ellipsis trimming sign, trims the last of
  them; the rest get height 0. Hit tests on the ellipsis and carets inside
  elided text land on the visible edge, and the first `isTrimmed` hit-test
  entry gives the elision start. Without an ellipsis the extra lines are
  clipped when drawing.
- **DirectWrite only trims a line it cuts.** When the cut falls at a paragraph
  break (or between unwrapped lines), max-height trimming would flag the line
  as trimmed without drawing a sign, so those layouts are clipped instead and
  get a separate "…" layout, eliding graphemes until it fits.
- **`trailingWhitespaceLength` includes the paragraph separator**, so it is
  not subtracted on top of `newlineLength`.
- Letter spacing uses trailing character spacing, removed from each
  paragraph's last grapheme so it only goes between graphemes.
- Glyph font identities key on the font files (loader and reference key),
  face index, `GetSimulations` (synthetic bold or oblique) and
  `IDWriteFontFace5` axis values.
- **Glyphs come from a custom `IDWriteTextRenderer`** passed to
  `IDWriteTextLayout::Draw`; the ellipsis sign is an inline object that draws
  through it too. Right-to-left runs arrive in logical order with their origin
  on the right, so glyphs are sorted into visual order. Runs are collected
  without drawing effects, so one run can hold several span colors.
- **Glyph ranges draw like `DrawTextLayout` only with its two habits
  reproduced**: run baselines snap to whole device pixels under axis-aligned
  transforms, and color glyphs go through `TranslateColorGlyphRun` including
  `COLR_PAINT_TREE` with `ID2D1DeviceContext7::DrawPaintGlyphRun`. Windows 11
  emoji are COLRv1 paint trees with gradients; the COLRv0 path draws them as
  flat layers.
- With no wrapping and width 0, centering happens around x = 0, so unbounded
  layouts get the widest line as their width after a first pass.
- **`CreateTextLayout` rejects a NULL string** even when its length is 0.
- **Cluster metrics are not grapheme clusters**: a flag's two regional
  indicators are separate clusters. Caret stops come from the system ICU
  (`icu.h`, `ubrk` character breaks).
- Reading direction is layout-wide, so `GR_TEXT_DIRECTION_AUTO` uses ICU's
  `ubidi_getBaseDirection` of the whole text.
- `IDWriteTextLayout` is mutable, so a mutex per layout serializes queries and
  drawing.
- Windows' emoji font has no flag glyphs; regional indicator pairs render as
  letters (for example "CN").

## Known gaps

- **Device loss is not handled.** After a Direct3D device loss, drawing
  surfaces lose their contents. The fix is to listen to
  `CompositionGraphicsDevice.RenderingDeviceReplaced`, recreate surfaces
  and re-rasterize drawings, images and shadow masks. The backend keeps the
  batch's drawer provider so that drawings can be redrawn from that event without
  waiting for a commit.
- The mask's visual surface renders at the visual's own units, so mask
  edges may be soft at high scales.
- Layer blur is in screen space: scale transforms do not scale σ.
- Text: mixed-direction paragraphs share the reading direction of the first
  strong character (DirectWrite's direction is layout-wide).
- Shadow masks and drawings are rasterized per layer; there is no
  cross-layer cache beyond images.
- `gr_windows_attach_to_hwnd` does not resize itself with the parent (the parent
  moves the child in `WM_SIZE`).
- Not yet verified: edge quality on real GPUs (the VM's driver is suspect,
  see lessons learned), moving a window between monitors with different
  DPIs, x64 hardware, and `gr_windows_attach_to_visual` in an application's own tree.
