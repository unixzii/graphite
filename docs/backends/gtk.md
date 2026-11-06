# GTK backend (`graphite-gtk`)

This backend renders a scene with GTK 4's scene graph (GSK) and uses the
gtk4-rs bindings (`gtk4` / `gsk4` 0.11, feature `v4_22`). It is built on
Linux only (`cfg(target_os = "linux")`), and everything in it is bound to
GTK's main thread.

## Source map

| File | Contents |
|---|---|
| `src/host.rs` | `SceneHost`, `attach` (widget), `attach_offscreen` |
| `src/backend.rs` | `GskBackend`: applies batches to the tree, schedules commits |
| `src/tree.rs` | One cached `GskRenderNode` per layer, and how a layer is composed |
| `src/draw.rs` | `DisplayList` → GSK node through `GtkSnapshot` |
| `src/convert.rs` | Conversions to GDK/GSK/graphene values (paths, strokes, transforms, textures) |
| `src/widget.rs` | `GraphiteWidget`: frame-clock commits, scale tracking, `snapshot()` |
| `src/offscreen.rs` | Rendering with a chosen GSK renderer (GL, Vulkan, Cairo), for tests |

## Presenting a scene

- `attach(&scene)` returns a `SceneHost` whose `widget()` you can put
  anywhere in a widget tree. The widget's natural size is the root layer's.
  It has `overflow: hidden`, so the scene is clipped when the widget is
  allocated less space.
- `attach_offscreen(&scene)` has no widget. Commit and set the backing scale
  yourself, then render with an `offscreen::Renderer` (realize one per
  renderer kind and reuse it) or the one-shot `offscreen::render`.

`graphite_gtk::gtk` re-exports the bindings, so applications use the same
versions.

## Layer composition

GSK has no retained layer tree; a widget emits a node tree on every
`snapshot()`. The backend keeps one immutable node per layer and rebuilds
only the layers a commit touched, together with their ancestors (through
sublayer and mask edges). Unchanged subtrees are reused by pointer, which
lets GSK diff damage and keep its texture, glyph and path caches.

```text
transform(resolved.transform)        box space → superlayer's local space
  opacity(props.opacity)             group opacity
    shadow                           outset shadow, or blur(2σ) of the filled shape; not clipped
    mask(mask layer, local space)    alpha mask
      clip(rounded bounds)           only with masks_to_bounds
        background                   rounded by corner_radius
        contents                     image | drawing (clipped to bounds) | shape | gradient
        translate(bounds_offset)     sublayers, in local space
        border                       inside the bounds, above sublayers
```

Notes:

- Shadows: when the layer has an opaque background and no mask, the
  shadow's interior can never show. In that case the backend uses GSK's
  analytic outset shadow node, which is exact there, cheaper than blurring,
  and the same node GTK uses for CSS `box-shadow`. Otherwise (translucent or
  missing background, a mask, or a custom shadow path) it blurs a fill of
  the shape, so the shadow also shows under the layer. The outset node takes
  the spread natively and grows corners the same way Core does.
- Transforms are always built from 2D operations (translate, rotate, skew,
  scale): general affine matrices are decomposed rather than passed as a
  raw matrix (see lessons learned).
- Drawings are clipped to the layer's bounds, like a bitmap backing store
  (and like Core Animation). Shapes and images are not clipped.
- Unlike Core Animation, shapes support gradient paints natively. The
  conformance suite does not cover gradient-painted shapes yet.

## Drawings

`draw::display_list_node` turns a `DisplayList` into GPU-rendered nodes; no
Cairo is involved:

| DisplayList | GSK |
|---|---|
| `Save` / `Restore` | `save` / `restore`, plus popping the clips pushed in between |
| `ConcatTransform` | `transform` |
| `ClipRect` | `push_clip` |
| `ClipPath` | `push_mask` with the filled path as the mask (GSK has no path clip) |
| `SetAlpha` | tracked; each primitive is wrapped in an opacity node, matching CoreGraphics' per-operation alpha |
| `FillPath` / `StrokePath` | `append_fill` / `append_stroke`, or `push_fill` / `push_stroke` around a gradient |
| `DrawImage` | `append_scaled_texture` |

Nodes are vector data, so a change of backing scale does not require
rebuilding them.

## Commit scheduling

`request_commit` asks the widget's frame clock for a **layout** phase. The
commit runs in a `layout` signal handler, which comes after tick callbacks
(where animations change the scene) and before painting. A change made in a
tick callback is therefore visible in the same frame. Requests made before
the widget is realized are replayed on `realize`.

## Backing scale

On `realize` and on the surface's `notify::scale`, the widget calls
`Scene::set_backing_scale(surface.scale())`. The surface scale is
fractional on Wayland; the test VM runs at 1.333. Core uses it for pixel
snapping. GSK nodes are resolution independent, so nothing is rasterized
by the backend.

## Performance

Measured on the VM (GTK 4.22, scale 1.333, 1280 × 853 device pixels) with
the demo, which animates all six cards at once:

| Per frame | Release | Debug |
|---|---|---|
| `Demo::update` (setters) | 0.02 ms | 0.05 ms |
| `Scene::commit` (drawers, batch, apply) | 0.18 ms | 0.25 ms |
| Rebuilding layer nodes | 0.08 ms | 0.14 ms |
| Full-frame raster, Cairo renderer | 64–69 ms | 60 ms |
| Full-frame raster, GL renderer on llvmpipe | 40 ms | |

- Graphite's own work is well under 1 ms a frame; the time goes into
  rasterizing pixels.
- **Damage tracking works.** Moving a small layer, either at the root or
  inside a card (so the card's node is rebuilt), keeps the Cairo renderer
  at the frame clock's full rate (75–83 fps on the VM). Rebuilt
  decoration nodes with unchanged values do not add damage.
- The demo's two scenes make the comparison directly (`--scene … --fps`,
  release build, frame clock at 60 Hz):

  | Scene | Cairo | GL (virgl) |
  |---|---|---|
  | `ui`: an application window, a few small animated regions | 58–60 fps | 46–60 fps |
  | `showcase`: nearly the whole window animating | 16–19 fps | 16–26 fps |

- The `showcase` scene is slow with Cairo because nearly the whole
  window changes every frame, and Cairo is a CPU renderer: masks and group
  opacity need offscreen surfaces, and rotated images, clips and blurs are
  costly. The compositing card alone costs about 20 ms. GTK's own widget
  factory animates only small regions.
- Folding `SetAlpha` into paint colors (instead of an opacity node per
  primitive) made no measurable difference, so it was not kept.
- On the VM, GL via virgl renders even the moving-dot scene at about 40 fps;
  it appears to redraw whole windows. This is not representative of real
  hardware.

Ideas if CPU rendering matters: split static and animated parts of a
drawing into separate layers, so that only the animated part is
re-recorded and damaged; and cache static subtrees as textures (the
`shouldRasterize` idea from the design discussion).

## Testing

GTK objects are main-thread only, so `tests/conformance.rs` and
`tests/widget.rs` use `harness = false` and provide their own `main`. The
conformance target uses `graphite_conformance::run_main`. Both need a
graphical session:

```sh
export XDG_RUNTIME_DIR=/run/user/$(id -u) WAYLAND_DISPLAY=wayland-0   # when running over ssh
cargo test -p graphite-gtk --test conformance                        # GL and Vulkan renderers
GRAPHITE_GSK_RENDERERS=vulkan cargo test -p graphite-gtk --test conformance
cargo test -p graphite-gtk --test widget
cargo run --example demo -- --selftest
```

- **Conformance** runs every case with each selected renderer (`gl`,
  `vulkan`, `cairo`). All cases pass with all three renderers on conformant
  drivers.

On a QEMU VM with virgl/Venus (see lessons learned), use software drivers
for reproducible results:

```sh
export LIBGL_ALWAYS_SOFTWARE=1 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
```

To view the demo there with correct shadows under translucent layers, run
it with `GSK_RENDERER=vulkan`.
- **Widget** opens a real window and checks the following: frame-clock
  commits (including after the clock has gone idle), the natural size,
  following the surface scale, and what the widget actually paints (through
  `GtkWidgetPaintable`).

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
  `convert::transform` decomposes affine matrices into translate · rotate ·
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
- `Texture::download` gives B8G8R8A8; use `TextureDownloader` with
  `R8g8b8a8Premultiplied`. Rows are top-down.
- `gsk::RoundedRect::new` (and other constructors) assert they run on GTK's
  main thread after initialization.

## Known gaps

- Shadows that can show under their layer are a blur of the filled shape,
  re-rendered whenever their region is damaged.
- No headless setup yet (for example `mutter --headless`); the tests need a
  live session.
- Not yet verified: X11, integer-scale monitors, and moving windows between
  monitors with different scales.
