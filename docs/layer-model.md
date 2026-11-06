# Layer model and API semantics

This page describes what the public API means, especially where it
deliberately differs from `CALayer`. For signatures, see the rustdoc
(`cargo doc --open -p graphite`).

## Scenes

A `Scene` owns one layer tree and is presented by exactly one backend
(`Scene::set_backend` may be called once). It is a cheap, reference-counted
handle bound to the thread that created it.

The root layer (`Scene::root`) always sits at the top-left of the host. Its
`position`, `anchor_point` and `transform` are ignored. Its size does not
resize the host: whatever extends past the host is clipped.

```rust
use graphite::*;

let scene = Scene::new(Size::new(400.0, 300.0));

let card = Layer::new(&scene);
card.set_frame(Rect::new(20.0, 20.0, 240.0, 120.0));
card.set_background(Some(Color::WHITE));
card.set_corner_radius(12.0);
card.set_masks_to_bounds(true);
card.set_shadow(Some(Shadow::new(Color::BLACK.with_alpha(0.2), Vec2::new(0.0, 4.0), 8.0)));
scene.root().add_sublayer(&card);
// `card` can be dropped now; the tree keeps the layer alive.
```

## Layer kinds

| Handle | Kind | Adds |
|---|---|---|
| `Layer::new` | `Container` | background, border, shadow, sublayers |
| `ContentLayer` | `Content` | an image, or a drawing produced by a drawer |
| `ShapeLayer` | `Shape` | a filled and/or stroked path, with `stroke_start` / `stroke_end` |
| `GradientLayer` | `Gradient` | a linear or radial gradient |

Every typed handle `Deref`s to `Layer`, so `parent.add_sublayer(&shape)` and
`shape.set_opacity(0.5)` work. Use `Layer::downcast` and `downcast_ref` to get
a typed handle back, for example from `sublayers()`.

## Geometry

Geometry follows the CALayer model:

- `bounds`: the layer's local coordinate space. `bounds.origin` acts as a
  scroll offset for sublayers, drawings and paths.
- `position`: where the anchor point lies in the superlayer's local space.
- `anchor_point`: in unit coordinates of the bounds size; `(0.5, 0.5)` by
  default.
- `transform`: a 2D affine transform applied around the anchor point.
- `frame()` and `set_frame()` are derived. Like `CALayer`, `set_frame` assumes
  an identity transform.

Only 2D affine transforms are supported. 3D and perspective are not in the
core feature set because the platforms disagree on how they flatten.

## Appearance

- `opacity` is **group opacity**: a layer and its subtree fade as a whole.
- `corner_radius` with `masked_corners` rounds the background and border;
  `masks_to_bounds` also clips contents and sublayers to that rounded shape.
  There are no per-corner radii (Core Animation cannot express them).
- `border` is drawn inside the bounds, above contents and sublayers.

### Shadows: deliberate differences from CALayer

- A shadow is **not** clipped by the layer's own `masks_to_bounds`, so a
  clipped, rounded card can still cast a shadow.
- A shadow is never derived from the alpha of the contents. Its shape is
  `Shadow::path` (in local space) if set, otherwise the layer's rounded
  bounds. This avoids offscreen passes and keeps backends consistent.
- `blur_sigma` is a Gaussian standard deviation in points.

## Masks

`set_mask(Some(&mask))` masks a layer and its subtree by the alpha of `mask`,
which is positioned in the layer's local space. The constraints match the
strictest platform (Core Animation):

- the mask must not be in the tree (no superlayer) and cannot be added as a
  sublayer while it is a mask;
- masks cannot be nested: a mask cannot have a mask, and a mask layer cannot
  mask anything;
- a layer cannot be masked by one of its ancestors.

Violations panic (they are programming errors).

## Contents

`ContentLayer` shows one of:

- `Contents::Image(image)`, placed according to `gravity` (`Resize` by
  default). `Image` is premultiplied RGBA8 sRGB with a `scale` (pixels per
  point).
- A drawing. `set_drawer(|canvas, info| …)` installs a closure that records
  into a `Canvas` in local space. It runs at the next commit and again after
  every `set_needs_display()`. Drawings are recorded in points, so a change
  of backing scale re-rasterizes them; the drawer is re-run as well, so it
  can adapt (for example, hairlines).

`set_contents` removes any drawer; `clear_drawer` keeps the last drawing.

If a drawer needs to refer to its own layer or an ancestor, capture a
`WeakLayer` (`layer.downgrade()`). A strong handle would keep the layer alive
until the scene is dropped.

## Canvas

`Canvas` records a `DisplayList`. The op set is the intersection that every
backend implements exactly:

- state: `save`, `restore`, `with_saved`, `concat_transform`, `set_alpha`
  (which multiplies);
- clipping: `clip_rect`, `clip_path`;
- paths: `fill_path` (with a fill rule), `stroke_path` (width, cap, join,
  miter limit, dash), `fill_rect`;
- `draw_image`.

Paints are solid colors and linear or radial gradients. Blend modes and
canvas shadows are intentionally absent, because Direct2D cannot match
CoreGraphics there.

## Shapes and gradients

- `ShapeLayer` paths are in local space. `stroke_start` and `stroke_end`
  trim the stroked portion as fractions of the path length.
- `GradientLayer` gradients are in **unit coordinates** of the bounds
  (`(0, 0)` top-left, `(1, 1)` bottom-right), so they follow the layer's
  size. A radial gradient is an ellipse with radii `radius · size`.

## Resources

`Path`, `Image`, `DisplayList` and paints are immutable and shareable.
Identity (`ResourceId`, or `Arc` pointer equality) is how backends detect
changes and cache native counterparts. Build them once and reuse them.

## Backing scale and pixel snapping

The backend reports the host's device-pixel ratio through
`Scene::set_backing_scale`. Pixel snapping (on by default) is described in
[architecture](architecture.md#pixel-snapping).
