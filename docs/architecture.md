# Architecture

Graphite is a retained 2D layer library. The C core owns the model and native
compositors render it: Core Animation, GSK and Windows.UI.Composition. Widgets,
input routing, accessibility and UI layout belong to the application/toolkit.
Cross-platform consistency is semantic, not pixel-identical.

## Source layout

Core lives in `src/core`, native implementations in `src/platforms`, public
headers in `include/graphite`, and backend-independent rendering tests in
`conformance`. Example scenes and their shared native shells are in `examples`.

## Ownership

`gr_scene *` and `gr_layer *` are stable opaque handles. `new` returns one owned
reference; `ref` acquires one and `unref` releases one. Parents own children
and the Scene owns its root. The tree keeps a child alive
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

## Hosts

Every platform attachment returns a `gr_scene_host` ([host.h](../include/graphite/host.h)): a core-owned,
reference-counted handle with a platform vtable behind it, like text systems. The host holds a Scene link, not the
Scene. Platform headers add functions for their own hosts (native view, layer, widget or window, offscreen readback)
that recognize them by vtable address and return NULL/false for other hosts.

Hosts with a native input surface are interactive: they deliver input and frame events from
[event.h](../include/graphite/event.h) to one handler. Graphite normalizes events into host coordinates and paces
frames; hit testing, routing, gestures and focus chains stay with the application. Core owns the handler and the
delivery rules (validation, frame request coalescing, queueing events dispatched from inside a handler, deferred
handler destruction); platforms report native events through `gr_scene_host_dispatch_event` and implement the
optional event callbacks of `gr_scene_host_vtable`.

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
invalidates ancestors of changed children. Scene mutation and nested
drawing are rejected while a drawer runs.

## Text

Text systems are platform objects independent of Scenes; see
[text.h](../include/graphite/text.h). Core owns immutable attributed text
(UTF-8, a base style and ordered spans that platforms apply natively, and UTF-16
mapping) and wraps platform layouts: it counts references, validates and
normalizes query arguments, and forwards every query to the layout's vtable
without caching. Canvas records layouts as `GR_DRAW_TEXT_LAYOUT` ops and glyph
ranges of a line as `GR_DRAW_TEXT_GLYPHS` ops; a backend replays layouts whose
vtable it recognizes. Color is a drawing argument, so
recoloring needs no relayout.

## Coordinates and pixel snapping

A `gr_resolved` is the layer's properties plus what core derives from them:
`props` with its bounds and border width snapped, the composed `transform` and
the kind's `content`. Box space runs from `(0,0)` to `props.bounds.size`.
Background, border, image, clipping and default shadow use it. Local space is
offset by `props.bounds.origin` and is used by children, paths, drawings,
custom shadows and path masks; translate it by `-bounds.origin` into box space.

The resolved transform is `T(position) * transform * T(-anchor * size)` with its
translation snapped; the root uses identity. Every backend places layers with
it. Core Animation keeps the anchor point at zero, puts the translation in
`position` and only the linear part in `transform`, so translated layers keep
the cheaper identity transform. Image
placement is computed centrally, and `gr_resolved_new_shadow_path` builds the
box-space shadow outline.

Pixel snapping is always on. Translation, bounds offset, axis-aligned size and
border width snap to the backing-scale grid. Nonzero box dimensions and borders
retain at least one device pixel. Snapping is local to each layer, so
fractional error can accumulate in deep hierarchies. Scale changes invalidate
dependent resolved values.

Coordinates use logical points, top-left origin, downward Y and radians. Colors
are straight-alpha sRGB; images and captures use premultiplied RGBA8. Shadows use
Gaussian sigma; native conversions are calibrated by conformance tests.
