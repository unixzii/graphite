# Writing a backend

This is a checklist for adding a platform (Web is next). Read
[architecture](../architecture.md) first. The macOS and Windows backends are
the references for a retained compositor; the GTK backend is the reference
for a snapshot-style one.

## 1. Crate layout

Create `crates/graphite-<platform>` with platform-gated dependencies:

```toml
[target.'cfg(target_os = "linux")'.dependencies]
graphite = { path = "../graphite" }
# platform bindings…

[target.'cfg(target_os = "linux")'.dev-dependencies]
graphite-conformance = { path = "../graphite-conformance" }
```

Mirror the macOS module split:

- `host.rs`: a `SceneHost` type plus `attach_to_*` functions;
- `backend.rs`: the `Backend` implementation;
- `draw.rs`: display-list replay;
- `convert.rs`: value conversions;
- `offscreen.rs`: window-less rendering for tests.

## 2. Implement `graphite::Backend`

```rust
impl graphite::Backend for MyBackend {
    fn request_commit(&mut self) { /* schedule Scene::commit at the commit point */ }
    fn apply(&mut self, batch: graphite::CommitBatch) { /* see below */ }
}
```

Rules:

- **Never call back into the scene from `request_commit` or `apply`.** Hold
  a `WeakScene` and commit later, from the event loop. Reading
  `Scene::backing_scale` is fine.
- **Apply in contract order:** realize, then updates, then children (in two
  passes), then releases. macOS applies updates before children so that
  realized layers have geometry before they are linked. Either order works,
  as long as children are installed with the two-pass algorithm documented
  on `CommitBatch`.
- **Use `Resolved`, not raw props**, for anything Core derives: `transform`
  (already snapped), `bounds_offset`, `size`, `border`, `shadow_shape`
  (`ShadowShape::path` for the final outline, or its fields if the platform
  has a native spread), the image `rect`. If you build geometry from raw
  `position` the way Core Animation does, add `snap_offset`.
- **Respect the coordinate spaces.** Background, border, clip, image and
  default shadow are in box space. Sublayers, drawings, paths, custom shadow
  paths and masks are in local space (box space minus `bounds_offset`).
- **Trust the dirty bits.** Core already propagates dependencies (size →
  shadow/content, scale → geometry/content).
- **Draw on demand.** For `ResolvedContent::Drawn { version }`, call
  `batch.drawers.draw(id, &resolved.draw_info(scale))` when
  `(version, DrawInfo)` differs from what you last rendered, render the
  `DisplayList` into your native representation and drop it. Keep a clone of
  `drawers` if the platform asks for displays outside `apply` (a layer
  delegate, device loss). Do not run drawers while you hold state a drawer
  could reach through the scene.
- **Set every native property explicitly** when you realize a layer.
- **Re-home masks defensively.** A layer can stop being a mask and become a
  sublayer, or the other way round, within one batch.
- **Released layers may still be native children** of other released layers.
  Removing them from their native parent must tolerate that.

Where the platform has no retained layer tree (GSK), cache one render node
per layer. On commit, invalidate dirty layers and their ancestors, and
rebuild only invalidated nodes in `snapshot()` (see `crates/graphite-gtk/src/tree.rs`).

## 3. Hosting

Provide at least:

- an attach function for the platform's view type (`NSView`, `GtkWidget`,
  `HWND`, `HTMLElement`) that fills the parent and clips to it;
- backing-scale tracking, which calls `Scene::set_backing_scale`;
- commit scheduling at the platform's frame or commit point.

Platform-specific facts gathered so far are in
[platform notes](../platform-notes.md).

## 4. Conformance

Implement `graphite_conformance::Harness` in a test target and expand the
suite:

```rust
struct MyHarness;

impl graphite_conformance::Harness for MyHarness {
    fn name(&self) -> &str { "gtk" }
    fn attach(&self, scene: &graphite::Scene, scale: f64) -> Box<dyn graphite_conformance::Capture> {
        // attach to an offscreen host at `scale` (and set the scene's backing scale)
    }
}

graphite_conformance::conformance_tests!(MyHarness);
```

If your platform's objects are bound to the main thread, use a
`harness = false` test target that calls
`graphite_conformance::run_main(&[&harness])` instead of the macro (see
`crates/graphite-gtk/tests/conformance.rs`).

`Capture::capture` must return the root's bounds at the attach scale as
premultiplied RGBA8, with row 0 at the top. Calibrate first: render a known
asymmetric scene with the raw platform API to establish the read-back
orientation, as `crates/graphite-macos/tests/offscreen.rs` does.

All semantic cases must pass. Run with `-- --nocapture` to see measured
values; use them to calibrate conversions (for example, how the platform's
blur radius relates to σ), then record the result in a code comment.

## 5. Demo

Add `examples/demo-rs/platform/<platform>.rs` with `run(app)` and
`snapshot(scene)`, and wire it up in `examples/demo-rs/platform/mod.rs`. The
scene and the self-test are shared.

## 6. Document

Add `docs/backends/<platform>.md` covering the mapping, scheduling, lessons
learned and known gaps, and update [the roadmap](../roadmap.md).
