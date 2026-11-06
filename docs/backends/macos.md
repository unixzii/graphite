# macOS backend (`graphite-macos`)

Renders a scene with Core Animation, draws immediate-mode contents with
CoreGraphics, and uses the `objc2-*` bindings.

## Source map

| File | Contents |
|---|---|
| `src/host.rs` | `SceneHost`, `attach_to_view`, `attach_to_layer` |
| `src/backend.rs` | `CaBackend`: maps commit batches onto `CALayer`s; run-loop commit scheduling |
| `src/draw.rs` | Replays a `DisplayList` into a `CGContext` |
| `src/delegate.rs` | `GraphiteDrawingDelegate`, the `CALayerDelegate` that displays drawn layers |
| `src/convert.rs` | Conversions to CG/CA values (paths, colors, images, transforms) |
| `src/view.rs` | `GraphiteView`, the layer-hosting `NSView` that tracks the backing scale |
| `src/offscreen.rs` | `CARenderer` + Metal offscreen rendering, used by the tests |

## Presenting a scene

A `SceneHost` is the bridge between a scene and an existing hierarchy. It
draws nothing itself. It owns a flipped (y-down), clipping **host layer**
whose only sublayer is the scene root.

- `attach_to_view(scene, parent_view, mtm)` creates a layer-hosting
  `GraphiteView` that fills `parent_view` and resizes with it. It follows
  the window's backing scale (`viewDidChangeBackingProperties`,
  `viewDidMoveToWindow`).
- `attach_to_layer(scene, container)` adds the host layer to any `CALayer`
  you own. It rasterizes at the container's `contentsScale`; call
  `Scene::set_backing_scale` yourself when that changes.

## Layer mapping

Every graphite layer becomes two `CALayer`s:

```text
outer  (CALayer)        bounds = (0, 0, w, h), position (+ snap_offset), anchorPoint,
  │                     transform, opacity, hidden, shadow (always with a shadowPath)
  └─ body (kind class)  frame = outer.bounds, bounds = graphite bounds (snapped origin),
       │                masksToBounds, cornerRadius, maskedCorners, background, border,
       │                contents / CAShapeLayer / CAGradientLayer properties, mask
       ├─ image layer   (only for image contents, framed per gravity)
       └─ sublayers' outers
```

Why it is split this way:

- The shadow lives on `outer`, so it escapes the body's `masksToBounds`,
  which is graphite's semantics and not `CALayer`'s.
- `body.bounds` equals graphite's bounds, so `bounds.origin` scrolling,
  shape paths and masks are handled natively.
- Image contents get their own first sublayer framed by Core's
  gravity-resolved rectangle. `contentsGravity` is avoided because its
  meaning depends on geometry flipping.
- The body's class is fixed by the layer kind (`CALayer`, `CAShapeLayer`,
  `CAGradientLayer`).

All changes are applied inside a `CATransaction` with `setDisableActions(true)`.
Standalone `CALayer`s otherwise animate every property change implicitly.

## Commit scheduling

`request_commit` installs a one-shot `CFRunLoopObserver` for `BeforeWaiting`
on the run loop that created the backend. Its order (1 999 000) runs it just
before Core Animation's own commit observer (2 000 000), so changes reach the
screen in the same frame. On threads without a running run loop (tests),
call `Scene::commit` yourself.

## Drawings

A drawn layer's body gets a `DrawingDelegate` (holding the batch's
`Drawers` and the layer id) as its `CALayerDelegate`. Core Animation owns
the backing store: the backend only calls `setNeedsDisplay` when
`(display version, local bounds, scale)` changes, and Core Animation calls
`drawLayer:inContext:` during its transaction commit (right after ours, so
the drawer sees the committed state), on contents-scale changes, or after it
purged the store. The delegate runs the drawer for the layer's current
bounds and `contentsScale` and replays the recording straight into the
context, flipping the CTM only if Core Animation did not already (under the
flipped host layer it has).

- `CALayer.delegate` is weak, so the node holds the delegate strongly and
  clears it on release.
- The class is not `MainThreadOnly` (offscreen rendering and tests use other
  threads); it records the scene's thread and ignores displays requested
  from any other thread, because `Drawers` is not `Send`.

## Lessons learned (keep these in mind)

- **AppKit resets `geometryFlipped` on a layer-hosting view's own layer.**
  The flipped host layer is therefore a sublayer of the view's layer.
  `tests/view.rs` guards this in a real window.
- **`CARenderer` only sees a layer tree after a transaction commit that
  follows `setLayer`.** `offscreen::render` flushes after attaching.
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

- Gradient paints on `ShapeLayer` fills and strokes degrade to the first
  stop. Doing this properly needs a `CAGradientLayer` masked by the shape.
- No image magnification filter property yet (Core Animation defaults to
  linear filtering).
- `CGImage`s are shared per `ResourceId` through per-node reference counts.
  There is no cross-layer cache for paths or drawings.
- Not yet verified: moving a window between displays with different scales.
