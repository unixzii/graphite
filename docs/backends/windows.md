# Windows backend (`graphite-windows`)

This backend renders a scene with the system Windows.UI.Composition API
(WinComp) from Win32, draws immediate-mode contents with Direct2D, and uses
the `windows` 0.62 bindings. It is built on Windows only (`cfg(windows)`),
and everything in it is bound to the thread that created the scene.

## Source map

| File | Contents |
|---|---|
| `src/host.rs` | `SceneHost`, `attach_to_hwnd`, `attach_to_visual`, `compositor` |
| `src/backend.rs` | `WinCompBackend`: maps commit batches onto visuals |
| `src/context.rs` | The per-thread `Context` (DispatcherQueue, Compositor, D3D/D2D devices, graphics device) and commit scheduling |
| `src/draw.rs` | Replays a `DisplayList` into a Direct2D device context |
| `src/convert.rs` | Conversions to composition, Direct2D and numerics values; `CompositionPath` from D2D geometry |
| `src/effects.rs` | The alpha-mask effect description used for masks |
| `src/window.rs` | The window class of host windows (DPI tracking, no GDI painting) |
| `src/offscreen.rs` | Off-screen hosts and `PrintWindow` read-back, for tests and snapshots |

## Presenting a scene

- `attach_to_hwnd(&scene, parent)` creates a child window that fills
  `parent`'s client area and puts a `DesktopWindowTarget` on it. The scene
  follows the window's DPI (`WM_DPICHANGED_AFTERPARENT`). Resize the child
  from the parent's `WM_SIZE` (`SceneHost::hwnd`), like any child control.
  The child returns `HTTRANSPARENT`, so input goes to the parent. Hosting in
  a child window keeps composition content out of the way of the parent's
  other child windows (the airspace rule).
- `attach_to_visual(&scene, &container)` presents the scene in any
  `ContainerVisual` from `graphite_windows::compositor()`. Call
  `Scene::set_backing_scale` yourself.
- `offscreen::attach(&scene, scale)` and `offscreen::render` render without
  showing anything (see [Read-back](#read-back)).

Dropping a `SceneHost` detaches the scene (it destroys its window or
removes its visual), so keep it alive while the scene is shown.

The process should be per-monitor DPI aware (v2), set in the manifest or
with `SetProcessDpiAwarenessContext` before creating windows. Otherwise
Windows bitmap-scales the window and blurs it. `graphite_windows::windows`
re-exports the bindings.

## Layer mapping

Composition units are pixels. The host puts the whole tree in a container
scaled by the backing scale, so the backend works in points like the other
backends. Every graphite layer becomes a small group of visuals:

```text
outer (ContainerVisual)        TransformMatrix = resolved transform, Opacity, IsVisible
  ├─ shadow (SpriteVisual)     no brush; DropShadow whose Mask is the rasterized shadow shape
  └─ [mask (LayerVisual)]      only when masked: alpha-mask effect fed by a VisualSurface of the mask
       └─ body (ContainerVisual)
            ├─ clipped (ContainerVisual)     Clip = rounded box (masks_to_bounds)
            │    ├─ background (SpriteVisual)    color brush, rounded by a RectangleClip
            │    ├─ content                      SpriteVisual (image, drawing, gradient) | ShapeVisual
            │    └─ sublayers (ContainerVisual)  Offset = bounds_offset; the sublayers' outers
            └─ border (ShapeVisual)          an even-odd ring inside the box
```

Why it is shaped this way:

- **Transforms** use the resolved transform as `TransformMatrix`, with
  `Offset`, `AnchorPoint` and `CenterPoint` left at zero. Their composition
  order is undocumented, and the resolved matrix already includes position,
  anchor and pixel snapping.
- **Group opacity** is native: a `ContainerVisual`'s `Opacity` composites
  its subtree as a whole (measured: overlapping children at opacity 0.5
  give the same alpha as one child). No `LayerVisual` is needed.
- **Shadows** sit beside the body, so `masks_to_bounds` does not clip them,
  and below it, so they show through a layer without a background. A
  `DropShadow` on a `SpriteVisual` without a brush draws the shape given by
  its `Mask` brush, so the shape is a white fill of `ShadowShape::path`,
  rasterized with Direct2D at the backing scale. It is re-rasterized when
  the shape or the scale changes.
- **Corners** come from `RectangleClip`, which has per-corner radii, so
  `masked_corners` maps directly. The same clip shape rounds the
  background and, on the body, clips contents and sublayers.
- **Borders** are a `ShapeVisual` filling the ring between the rounded box
  and the box inset by the width (inner radius `radius - width`, like Core
  Animation). The border sits outside the clip, and under an opaque border
  the background and the clip end at the border's midline. Otherwise their
  anti-aliased edges coincide with the border's outer edge: the background
  or the contents bleed through it as a dark fringe, and the clip
  anti-aliases the border's edge a second time. (XAML's `BackgroundSizing`
  exists for the same reason.)
- **Shapes** are `ShapeVisual`s built from Direct2D path geometry
  (`CompositionPath` over an `IGeometrySource2DInterop`). A `ShapeVisual`
  clips to its own size, so each one is sized to the path's control-point
  bounds plus the stroke's reach. Fill and stroke are separate sprite
  shapes, because the geometry's `TrimStart`/`TrimEnd` would also trim the
  fill. Unlike Core Animation, gradient paints work on shapes.
- **Gradient layers** are sprites with a linear or radial gradient brush in
  relative mapping mode (unit space), clamped, interpolated in sRGB.
- **Images** are uploaded once per `ResourceId` into a drawing surface and
  shared by every layer that shows them.
- **Masks** wrap the body in a `LayerVisual` whose effect is Direct2D's
  alpha mask. The mask layer's visuals live in a separate container that a
  `CompositionVisualSurface` renders; the surface covers the masked layer's
  box. `effects.rs` implements the `IGraphicsEffect` /
  `IGraphicsEffectD2D1Interop` description that Win2D would provide in C#.
- `BorderMode::Soft` on the scaled container anti-aliases the edges of
  transformed visuals; children inherit it.

## Commit scheduling

`request_commit` adds the scene to a per-thread list and enqueues one
handler on the thread's `DispatcherQueue`, which runs after the current
message is handled. The thread therefore needs a message loop. If it has no
`DispatcherQueue`, the context creates one on first use
(`CreateDispatcherQueueController`, in an STA unless the thread is already
in the MTA).

## Drawings

`draw::replay` translates a display list to Direct2D in a drawing surface
of `ceil(size × scale)` pixels (drawings are clipped to the bounds, like a
bitmap backing store):

| DisplayList | Direct2D |
|---|---|
| `Save` / `Restore` | tracked; clips pushed in between are popped |
| `ConcatTransform` | world transform |
| `ClipRect` | `PushAxisAlignedClip` when the transform keeps axes aligned, otherwise a layer with a geometric mask |
| `ClipPath` | a layer with a geometric mask |
| `SetAlpha` | multiplied into each brush's opacity (per primitive, like CoreGraphics) |
| `FillPath` | `FillGeometry` |
| `StrokePath` | the stroke widened into an outline (`ID2D1Geometry::Widen`), then `FillGeometry`; miter joins bevel past the limit (`MITER_OR_BEVEL`); dashes are scaled by the width |
| `DrawImage` | `DrawBitmap` (linear) from an RGBA bitmap |

Gradients interpolate with gamma 2.2 (sRGB) and clamp.

## Read-back

Composition renders in DWM, so pixels can only be read back from there.
The conformance harness and `--snapshot` show the scene in a visible,
unactivated popup window placed at (−20000, −20000), and read it with
`PrintWindow(PW_RENDERFULLCONTENT)`, which asks DWM for the composed
window and works for off-screen and covered windows (not for hidden ones).
A window has no alpha, so each capture renders twice, over a black and
over a white backdrop visual. With premultiplied colors, the black result
is the color, and white minus black is `1 − alpha`. Each capture first
waits for `Compositor::RequestCommitAsync`.
`offscreen::capture_window` reads back any window of the process.

## Testing

Composition needs an interactive desktop session: an SSH logon runs in
session 0, which has none. The test targets use `harness = false` (objects
are bound to their thread):

```sh
cargo test -p graphite-windows --test conformance   # the suite at 1×, 1.5×, 2×
cargo test -p graphite-windows --test window        # attach_to_hwnd in a real window
cargo run --example demo -- --selftest
```

- **Conformance**: all 26 semantic and 7 measured cases pass.
- **Window** creates a top-level window and checks: the host fills it,
  the scene follows the window's DPI, the DispatcherQueue commits without
  an explicit `Scene::commit` (also after the loop has gone idle), and the
  window shows the layer, scaled by the DPI, before and after a move.

Over SSH, run them through a scheduled task in the user's session
(`schtasks /create … /it`, then `/run`) and collect the output from a file.

## Performance

Measured on the development VM (Windows 11 build 26300, ARM64 under QEMU,
VirtIO GPU, scale 1.75), release build: both demo scenes, `showcase` with
nearly the whole window animating and `ui` with a few small animated
regions, run at the full 60 Hz tick rate. Composition renders in DWM; the
application thread only applies property changes and rasterizes drawings
whose display list changed. Per-frame costs have not been profiled yet.

## Lessons learned

- **`DropShadow.BlurRadius` is 3σ.** Calibrated by
  `shadow_blur_matches_sigma` (with and without a background): for σ = 4,
  4.10–4.12 at the 1σ point and 3.78–3.81 (halved) at the 2σ point, at
  1×, 1.5× and 2×. The root scale scales the blur with everything else.
- **A `ContainerVisual`'s opacity is group opacity.**
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
  demo uses a high-resolution waitable timer on absolute deadlines in its
  message loop.
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

## Known gaps

- **Device loss is not handled.** After a Direct3D device loss, drawing
  surfaces lose their contents. The fix is to listen to
  `CompositionGraphicsDevice.RenderingDeviceReplaced`, recreate surfaces
  and re-rasterize drawings, images and shadow masks.
- **Masks only cover the masked layer's box**: content outside it is
  hidden even where the mask layer extends. The mask's visual surface
  renders at the visual's own units, so mask edges may be soft at high
  scales.
- Shadow masks and drawings are rasterized per layer; there is no
  cross-layer cache beyond images.
- `attach_to_hwnd` does not resize itself with the parent (the parent
  moves the child in `WM_SIZE`).
- Not yet verified: edge quality on real GPUs (the VM's driver is suspect,
  see lessons learned), moving a window between monitors with different
  DPIs, x64 hardware, and `attach_to_visual` in an application's own tree.
