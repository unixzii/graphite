# Architecture

Graphite is a cross-platform, retained-mode 2D layer library. Its public API
is modelled on Core Animation's `CALayer`, plus a CGContext-style
immediate-mode drawing API. Each platform renders through its **native
compositor** (Core Animation, GSK and Windows.UI.Composition today; the DOM
planned) rather than through a renderer of our own.

## Goals and non-goals

In scope:

- A tree of layers with geometry, appearance and contents (bitmaps, drawings,
  vector shapes, gradients), presented by the platform compositor.
- Immediate-mode drawing into a layer's contents.
- Text layout and drawing (planned, see [roadmap](roadmap.md)).

Out of scope, by design:

- Views, input events and hit-testing for input routing, layout, accessibility.
  Those belong to a toolkit built on top.
- An animation framework (curves, springs, timelines). A compositor-driven
  keyframe primitive may be added later, because only the native compositor
  can run animations off the main thread.
- Pixel-identical output across platforms. Graphite guarantees *semantic*
  consistency (see [testing](testing.md)).

## Crates

| Path | Crate | Role |
|---|---|---|
| `crates/graphite` | `graphite` | Platform-independent core: model, commit pipeline, `Backend` trait, resources, `Canvas` |
| `crates/graphite-macos` | `graphite-macos` | Core Animation backend |
| `crates/graphite-gtk` | `graphite-gtk` | GTK 4 / GSK backend (Linux) |
| `crates/graphite-windows` | `graphite-windows` | Windows.UI.Composition + Direct2D backend (Windows 11) |
| `crates/graphite-conformance` | `graphite-conformance` | Backend-neutral conformance suite |
| `examples/demo` | (root package `graphite-examples`) | Platform-neutral demo with a small per-platform shell |

The workspace root is itself a package (`graphite-examples`, unpublished)
only so that the project-level `examples/` directory can exist.

## The pipeline

```text
user code ──setters──► model (Scene + LayerData)            platform independent
                         │  once per frame: Scene::commit()
                         ▼
                       dirty layers → CommitBatch (with resolved values)
                         │
                         ▼
                       Backend::apply(batch)                  per platform
                         ├─ retained backends (Core Animation, WinComp, DOM):
                         │    keep an id → native object map, apply deltas
                         └─ snapshot backends (GSK):
                              cache one render node per layer, rebuild dirty subtrees
                         │  whenever the backend needs a drawing (in apply or later)
                         ▼
                       batch.drawers.draw(id, info) → drawer → DisplayList
                         → rendered by the backend, then dropped
```

Why setters do not touch native objects directly:

- **Derived values need the whole layer.** The transform a backend needs
  depends on position, anchor point, bounds and transform together.
- **Threading.** Native objects often have thread or call-site constraints
  (WinComp needs a `DispatcherQueue` thread; GSK nodes are built inside
  `snapshot()`).
- **Batching.** One commit per frame; on WASM, one crossing of the JS boundary.
- **Testability.** A recording backend can check the model without a platform.
- **Recovery.** A backend can rebuild native state (for example after a
  Direct2D device loss) from the model.

## Ownership model

All layer data lives in a slot map owned by the `Scene`. Public handles
(`Layer`, `ContentLayer`, `ShapeLayer`, `GradientLayer`) are
`(Weak<SceneInner>, key)` pairs that reference-count the data:

- every live handle holds a strong reference;
- a superlayer holds one for each sublayer, a layer holds one for its mask;
- the scene holds one for the root.

So a local handle can be dropped right after `add_sublayer`; the tree keeps
the layer alive. When the count reaches zero, the data is freed. Freeing is
deferred through a pending queue and always runs with no `RefCell` borrow
held, because dropping a layer drops its drawer closure, which may own other
handles whose `Drop` re-enters the scene.

Handles hold a *weak* pointer to the scene so that a drawer capturing a
handle cannot keep the whole scene alive. Dropping the last `Scene` frees
every layer; leftover handles become inert (setters do nothing, getters
return property defaults).

Everything in the model is bound to the scene's thread (`!Send`). Resources
(`Path`, `Image`, `DisplayList`, paints) are immutable, `Arc`-based and
`Send + Sync`, so they can be built on worker threads.

Typed handles are `#[repr(transparent)]` wrappers around `Layer` that `Deref`
to it. A layer's kind is fixed at creation, because Core Animation needs a
different class per kind (`CAShapeLayer`, `CAGradientLayer`).

## Attachment and realization

A layer is **attached** when it is reachable from the root through sublayer
or mask edges. Only attached layers are committed. The first commit that sees
an attached layer **realizes** it: the backend creates a native object, which
then lives until the layer data is freed, even across detach and re-attach.
Changes made while a layer is detached are kept and committed when it is
attached again.

## Dirty tracking and commits

Setters record `DirtyBits` (`GEOMETRY`, `APPEARANCE`, `SHADOW`, `CONTENT`,
`MASK`, `CHILDREN`) and queue the layer. Core propagates dependencies itself,
so backends can trust the bits. For example, a size change also dirties
`SHADOW` (the default shadow shape follows the bounds) and `CONTENT` (image
placement follows the bounds).

When the scene goes from clean to dirty it calls `Backend::request_commit`
once. The backend calls `Scene::commit` at its platform's commit point (end
of the run-loop iteration on macOS, `requestAnimationFrame` on the web, and
so on). The commit turns dirty, attached layers into a `CommitBatch`; it runs
no user code.

## Drawing

Drawn contents are not part of the model. A drawn layer resolves to
`ResolvedContent::Drawn { version }`, where the version is bumped by
`set_drawer` and `set_needs_display`. The backend gets the drawing through
`CommitBatch::drawers`, a cheap `'static` handle (a weak scene pointer) that
it may keep and call at any time on the scene's thread:
`drawers.draw(id, &DrawInfo)` runs the drawer and returns the `DisplayList`.
Because drawers are pure, a backend caches its rendering under
`(version, DrawInfo)` and draws again whenever that key changes or it lost
the rendering. The display list itself is dropped right away, so neither
Core nor the backend holds recordings (and, later, the text layouts they
will reference).

`Drawers::draw` looks the drawer up by id, so the closure stays owned by
Core and stale ids return `None`. While a drawer runs the scene is
read-only (a `drawing` flag makes every mutation panic); reads borrow the
layer map only briefly, so Core never calls into a backend while holding a
borrow.

How backends use it:

- Core Animation sets a `CALayerDelegate` on the drawn layer and calls
  `setNeedsDisplay` when the key changes; Core Animation owns the backing
  store and calls `drawLayer:inContext:` when it needs to.
- WinComp draws into a `CompositionDrawingSurface` during `apply` and keeps
  the `Drawers` to redraw surfaces later (device loss).
- GSK translates the recording into a render node during `apply`.

## The `CommitBatch` contract

A batch carries, in application order:

1. `realized`: layers to create, with their kind (`root` announces the root
   on the first commit);
2. `children`: the complete, ordered sublayer list of every layer whose list
   changed;
3. `updates`: a full `Resolved` snapshot plus the changed groups (never
   `CHILDREN`);
4. `released`: native objects to destroy;

plus the `backing_scale` to rasterize at and the `drawers` to draw with.

Children lists must be applied in two passes, because a layer can move
between parents in one commit. First detach, for every listed parent, the
children that left its list *and whose current native parent is still that
parent*. Then install the new lists, re-parenting as needed.

The authoritative description lives in the rustdoc of `graphite::CommitBatch`
and `graphite::Backend`. `graphite::testing::MirrorTree` asserts the contract
on every batch in Core's tests.

## Resolved values

`Resolved` contains the raw `LayerProps` plus values Core derives so that
every backend interprets the model identically. Two coordinate spaces matter:

- **Box space**: `(0, 0)` is the layer's top-left corner, `(w, h)` its
  bottom-right. Background, border, image contents, clipping and the default
  shadow shape live here.
- **Local space**: box space offset by `bounds.origin`. Sublayers, display
  lists, shape paths, custom shadow paths and masks live here.

| Field | Meaning |
|---|---|
| `transform` | Box space to the superlayer's local space: `T(position) · transform · T(-anchor · size)`; identity for the root |
| `bounds_offset` | `-bounds.origin`; translate local-space content by it to reach box space |
| `snap_offset` | What pixel snapping added to the translation; backends that use raw `position` (Core Animation) add it |
| `size` | The box size to draw (snapped); use it instead of `props.bounds.size` |
| `border` | The border with its width snapped; use it instead of `props.border` |
| `shadow_shape` | The custom path, or the rounded bounds with the spread; `ShadowShape::path` gives the final outline |
| `content` | Kind-specific contents; image rectangles are already placed according to `gravity` |

## Pixel snapping

When enabled (the default, `Scene::set_pixel_snapping`), Core rounds to the
device-pixel grid of the backing scale:

- each layer's translation and bounds offset;
- its size, when its own transform is a translation, so the far edges land
  on the grid too (a box keeps at least one device pixel);
- border widths, to whole device pixels (at least one), so a 1 pt border
  is a solid line at any scale.

Together these keep every edge of an axis-aligned box crisp at fractional
scales; without size snapping, a box at 1.333× had one sharp and one blurred
edge. Snapping is per layer: under translation-only ancestors all edges are
pixel-aligned without re-resolving descendants when an ancestor moves. The
costs: fractional offsets in deep hierarchies can accumulate up to half a
device pixel per level, and a box may be drawn up to one device pixel larger
or smaller than its bounds. Changing the scale or the setting re-resolves
every layer's geometry, border, shadow and size-dependent contents.

## Conventions

- Origin at the top-left, y pointing down; lengths in logical points (`f64`);
  angles in radians.
- Colors are non-premultiplied sRGB (`f32`). Wide gamut and HDR are out of
  scope for now.
- Shadows are specified by their Gaussian standard deviation (`blur_sigma`);
  each backend converts it to its own notion of blur radius and is
  calibrated by the conformance suite.
- Never rely on platform defaults: backends set every property explicitly
  when they realize a layer.
