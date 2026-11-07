# Core Animation backend

`graphite::macos` / `graphite_macos` is an Objective-C ARC backend for the C core.
Public hosting and readback functions are declared in `include/graphite/macos.h`.

Each model layer maps to an outer CALayer and a kind-specific body. The outer
owns transform, opacity, visibility and shadow; the body owns local bounds,
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

Offscreen rendering uses CARenderer with a shared BGRA8 Metal texture, an
sRGB color space and a command-queue fence. Readback is flipped
and swizzled to top-down premultiplied RGBA8. The standalone layer renderer
requires a root layer without a superlayer. The offscreen host API creates and
owns that container for callers.

`text.m` implements the CoreText text system (`gr_macos_text_system_new`), and
`draw.m` replays its layouts; see the [backend notes](../../../docs/backends/macos.md#text).

`graphite_macos_tests` covers raw readback orientation, scale, real NSView hosting
and resizing, host/model lifetime, and repeated complete Graphite offscreen
captures. `graphite_macos_text_tests` covers text metrics, wrapping, truncation,
alignment, carets, selection, concurrent queries and rendering. These tests require a normal macOS GUI/Metal session; a sandbox may
hide the Metal device or deny WindowServer access. Native conformance is provided
by the repository-level `conformance/` target.
