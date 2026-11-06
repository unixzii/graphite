# Architecture

Graphite is a retained 2D layer library. The C core owns the model and native
compositors render it: Core Animation, GSK and Windows.UI.Composition. Widgets,
input routing, accessibility and UI layout belong to the application/toolkit.
Cross-platform consistency is semantic, not pixel-identical.

## Source layout

Core lives in `src/core`, native implementations in `src/platforms`, public
headers in `include/graphite`, and backend-independent rendering tests in
`conformance`. The shared demo is in `examples/demo`.

## Ownership

`gr_scene *` and `gr_layer *` are stable opaque handles. `new` returns one owned
reference; `ref` acquires one and `unref` releases one. Parents own children,
mask owners own masks, and the Scene owns its root. The tree keeps a child alive
after the caller releases its local reference.

Layer back-pointers do not own the Scene. Final Scene release invalidates
surviving external layer handles: getters return defaults and setters become
no-ops, except drawer installation rejects ownership transfer. Cleanup is delayed
until safe traversal boundaries. Retired realized layers retain allocation
leases until the backend consumes their release notifications, preventing pointer
reuse while native maps still contain the identity.

Scene/Layer calls run on one owning thread. Callers enforce that rule; core does
not check thread affinity. Immutable paths, images, paints, strokes and display
lists use atomic reference counts and may be shared after synchronized
publication. Builders and canvases require exclusive access. Full rules are in
[API conventions](../include/graphite/types.h) and [lifetime internals](core-lifetime.md).

## Pipeline

Setters update model properties and mark dirty groups. The first pending change
calls the backend's `request_commit` callback. The backend schedules
`gr_scene_commit` at its platform commit point. Commit resolves attached dirty
layers and supplies a borrowed `gr_commit_batch` to `apply`.

A detached layer keeps its changes without scheduling rendering. Its first
attached commit realizes a native object; detaching does not destroy that
object. Reattachment reuses its identity. Final model release retires it.

Dirty groups cover geometry, appearance, shadow, content, mask and children.
Core propagates dependencies: size changes can affect shadow and contents, for
example. Native backends use the resolved snapshot and changed groups rather
than guessing those dependencies.

## Commit contract

The authoritative API is [backend.h](../include/graphite/backend.h). Apply:

1. Realize native objects, including the announced root.
2. Update complete child lists in two passes: detach missing children only if
   their current native parent is still that parent, then install all new lists.
3. Apply resolved updates (whose changed bits exclude children).
4. Release retired native objects.

Snapshots and arrays are borrowed for the callback. Ref resources retained after
it returns. Native maps may borrow identities under their backend lease, but
must remove them when the release batch is consumed. Scheduled work holds a
`gr_scene_link`, not a strong Scene reference. Reentry during backend callbacks
is prohibited except the documented drawer-provider operation.

## Drawers and caching

Drawers record a `gr_display_list` into a borrowed Canvas. Core stores the drawer,
not its output. `gr_drawer_provider_draw` checks layer identity and display
version and produces an owned recording on demand. Retain the provider for
deferred drawing; it safely becomes inactive when its Scene disappears.

Backends cache native rendering using display version, bounds and scale. They
can drop the recording after replay. macOS uses a CALayer delegate/backing store;
Windows uses composition drawing surfaces; GTK caches immutable GSK nodes and
invalidates ancestors of changed children or masks. Scene mutation and nested
drawing are rejected while a drawer runs.

## Coordinates and pixel snapping

Box space runs from `(0,0)` to the resolved size. Background, border, image,
clipping and default shadow use it. Local space includes `bounds.origin` and is
used by children, paths, drawings, custom shadows and masks. `bounds_offset` is
`-bounds.origin`, translating local content into box space.

The resolved transform is `T(position) * transform * T(-anchor * size)`; the root
uses identity. Resolved size, border width, shadow shape and image placement are
computed centrally. Backends using raw position must include `snap_offset`.

Pixel snapping is enabled by default. Translation, bounds offset, axis-aligned
size and border width snap to the backing-scale grid. Nonzero box dimensions and
borders retain at least one device pixel. Snapping is local to each layer, so
fractional error can accumulate in deep hierarchies. Scale or snapping changes
invalidate dependent resolved values.

Coordinates use logical points, top-left origin, downward Y and radians. Colors
are straight-alpha sRGB; images and captures use premultiplied RGBA8. Shadows use
Gaussian sigma; native conversions are calibrated by conformance tests.
