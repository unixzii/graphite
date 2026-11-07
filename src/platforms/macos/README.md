# Core Animation backend

`graphite::macos` / `graphite_macos` is an Objective-C ARC backend for the C core.
Public hosting and readback functions are declared in `include/graphite/macos.h`.

Each model layer maps to an outer CALayer and a kind-specific body. The outer
owns transform, opacity, visibility, shadow and the blur filter (the private
`CAFilter`); the body owns local bounds,
clipping, background, border, contents and mask. Images use a dedicated first
sublayer. Image CGImages are shared by resource identity with per-node usage
counts. Shape gradient paints currently fall back to the first stop.

Drawn content uses a CALayerDelegate. Core Animation requests display, the
delegate invokes the drawer provider with its display version and the native
bounds/scale, and the DisplayList is replayed into the supplied CGContext then
released. No per-layer DisplayList is retained. Frame commits are scheduled on
the owning run loop before Core Animation's own commit observer.

Scene teardown explicitly cancels the pending observer, clears delegates and
detaches native nodes before releasing backend ownership. This avoids relying on
ARC/autorelease timing for model pointer lease safety. Hosts hold weak Scene
links; dropping a host does not detach views/layers retained by their native
parent. NSView hosting requires the AppKit main thread and tracks window backing
scale automatically. Layer/offscreen hosting uses the Scene's owning thread.

View hosts are interactive. Frames come from the view's `CADisplayLink`
(macOS 14), created on the first request, added to the main run loop in common
modes and paused after a frame without a new request; it fires before the
backend's commit observer, so changes made for a frame commit with it. The view
reports its size and backing scale as `GR_EVENT_VIEWPORT`, and keyboard focus
while it is the first responder of the key window. Releasing the host
invalidates the display link and leaves the view inert.

The view also reports mouse and tablet pointer events (a tracking area gives
hover moves and crossings; AppKit keeps drags with the view), wheel and touchpad
scrolling with phases and momentum, and keys: `keys.m` maps `kVK_*` codes to
physical keys and fixed named keys, other keys resolve their unmodified text
with `charactersByApplyingModifiers:`, and modifier keys arrive through
`flagsChanged:` using device-specific flags so left and right sides are
distinguished. A press makes the view first responder. Unconsumed events go to
`super`, which passes them up the responder chain. Cursors are set from
`cursorUpdate:` and immediately while hovering. Text input requests are accepted
but not applied yet.

Offscreen rendering uses CARenderer with a shared BGRA8 Metal texture, an
sRGB color space and a command-queue fence. Readback is flipped
and swizzled to top-down premultiplied RGBA8. The standalone layer renderer
requires a root layer without a superlayer. The offscreen host API creates and
owns that container for callers.

`text.m` implements the CoreText text system (`gr_macos_text_system_new`), and
`draw.m` replays its layouts; see the [backend notes](../../../docs/backends/macos.md#text).

`graphite_macos_tests` covers raw readback orientation, scale, real NSView hosting
and resizing, frames, viewport and focus events, host/model lifetime, and repeated complete Graphite offscreen
captures; text is covered by the conformance suite's text cases. These tests
require a normal macOS GUI/Metal session; a sandbox may
hide the Metal device or deny WindowServer access. Native conformance is provided
by the repository-level `conformance/` target.
