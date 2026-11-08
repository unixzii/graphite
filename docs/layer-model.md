# Layer model and API semantics

The documented C headers in `include/graphite` define signatures and ownership.
A Scene owns a root container and accepts one backend. Its root resolves at the
host's origin; root position, anchor and transform are ignored. Root size does
not resize the native host.

```c
#include <graphite/graphite.h>

gr_scene *scene = gr_scene_new((gr_size){400, 300});
gr_layer *card = gr_layer_new(scene, GR_LAYER_CONTAINER);
gr_layer_set_frame(card, (gr_rect){{20, 20}, {240, 120}});
gr_color white = {1, 1, 1, 1};
gr_layer_set_background(card, &white);
gr_layer_set_corner_radius(card, 12);
gr_layer_add_child(gr_scene_get_root(scene), card);
gr_layer_unref(card); /* The parent now owns the child. */
/* Attach a platform host before committing or presenting the Scene. */
gr_scene_unref(scene);
```

Check `gr_result` in production code when inputs can be rejected.

## Layer kinds

`GR_LAYER_CONTAINER` supplies common geometry, appearance and children.
`GR_LAYER_CONTENT` adds an image or drawer, `GR_LAYER_SHAPE` a filled/stroked path,
and `GR_LAYER_GRADIENT` a gradient. Kind is fixed at creation. Kind-specific
setters reject incompatible layers; kind-specific getters require the right kind.

## Geometry and appearance

Bounds define local coordinates; bounds origin scrolls children and drawings.
Position locates the anchor in the parent's local space. Anchor defaults to
(0.5,0.5); a 2D affine transform applies around it. `gr_layer_set_frame` assumes
an identity transform and preserves bounds origin without resetting transform.
3D/perspective transforms are not supported.

Opacity applies to the whole subtree. Corner radius and selected corners round
background/border; `masks_to_bounds` also clips children and contents. Borders
are inside bounds, above contents and children. Pixel snapping rounds geometry
and border width to the backing-scale grid; see [architecture](architecture.md).

Shadows escape the layer's own clip. Their shape is an explicit local-space path
or rounded bounds, never the alpha of contents. The entire shape casts a shadow,
including beneath transparent backgrounds. Blur uses Gaussian sigma.

`gr_layer_set_blur` blurs the layer's whole rendering with a Gaussian σ in
points: shadow, background, contents, sublayers, border, mask and clip. The mask
and clip apply before the blur, so the blurred result bleeds past the bounds and
softens the mask's edges. Zero removes the blur. Backends differ in where σ is
measured: Core Animation scales it with the layer's and its ancestors'
transforms (as CSS `filter: blur()` does), while GTK and Windows apply it in
screen space, unaffected by scale transforms.

## Masks

A mask is a layer property, not another layer. `gr_layer_set_mask` takes a
`gr_mask` of one of two kinds:

- `GR_MASK_PATH`: opaque inside a path in local space (with a fill rule),
  transparent outside it, so sublayers past the bounds stay visible inside it.
- `GR_MASK_GRADIENT`: the alpha of a linear or radial gradient in unit
  coordinates of the bounds, like a gradient layer's; transparent outside the
  bounds. Colors are ignored. Fading a scrolling list's edges is the typical use.

The mask covers the background, border, contents and sublayers but not the
shadow. Every backend maps both kinds to native operations: a `CAShapeLayer`
or `CAGradientLayer` mask, a GSK mask node, a LayerVisual alpha-mask effect,
and CSS `clip-path` or `mask-image`.

## Contents and Canvas

Images are immutable premultiplied RGBA8 sRGB with a pixels-per-point scale.
Gravity controls placement. Installing an image removes a drawer and vice versa.

A drawer descriptor contains a callback, userdata and optional destructor.
Successful installation transfers userdata cleanup responsibility. The backend
may call the drawer any number of times on the Scene thread, or never. It records
local-space operations into a borrowed Canvas. The Scene is read-only during
that callback: mutation, nested drawing and commit return errors. External state
changes require `gr_content_layer_set_needs_display`.

Core retains the callback, not its recording. Backends cache their rendering and
request it again when bounds, scale or display version change. Clearing a drawer
leaves empty content. A drawer may borrow its own layer during its lifetime;
strong self/ancestor ownership can create cycles. See [layer callback documentation](../include/graphite/layer.h).

Canvas supports save/restore, affine transforms, multiplicative drawing alpha,
rect/path clips, filled/stroked paths, rectangles and images. Finishing balances
the saved state and returns an owned DisplayList, leaving the Canvas reusable.
Paints include solid colors and linear/radial gradients. Canvas blend modes and
shadows are not exposed.

Shape paths use local coordinates. Stroke start/end trim by path-length fraction.
Gradient-layer coordinates are normalized to bounds; radial radii scale with
width and height. Paths, paints, strokes, images and DisplayLists are immutable
reference-counted resources. Reuse resources to benefit from native caches.
