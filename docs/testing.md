# Testing

```sh
cargo test --workspace                                         # everything
cargo test -p graphite                                         # core model only
cargo test -p graphite-macos --test conformance                # conformance on macOS
cargo test -p graphite-macos --test conformance -- --nocapture # …with measured values
cargo test -p graphite-gtk --test conformance                  # conformance on GTK (Linux, needs a session)
cargo test -p graphite-windows --test conformance              # conformance on Windows (needs a desktop session)
cargo run --example demo-rs -- --selftest                         # real window, 60 frames
cargo run --example demo-rs -- --snapshot out.png                 # offscreen render of the demo
cargo run --release --example demo-rs -- --scene ui --fps         # frame rate of the low-damage scene
cargo clippy --workspace --all-targets
```

## C demo

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Debug -DGRAPHITE_SANITIZERS=ON
cmake --build /tmp/graphite-c --target check
/tmp/graphite-c/examples/demo/demo --scene ui --snapshot /tmp/ui.png
```

The C `check` target includes showcase/UI real-window selftests on macOS. The
original Rust demo is retained under `examples/demo-rs`; its code is unchanged.
See [C demo usage and comparison](../examples/demo/README.md).

## Layers of testing

### Core model: `crates/graphite/tests/scene.rs`

These tests are platform independent. `graphite::testing::Recorder` is a
backend that records every `CommitBatch` and replays it into a `MirrorTree`,
a stand-in for a native compositor tree. Replaying asserts the
`CommitBatch` contract on every batch: ids are realized before use and never
reused, the first update of a layer is complete, children lists name only
live layers, and parent links stay consistent. Any test that commits is
therefore also a contract test.

Cover new model behavior here first: ownership, dirty bits, commit contents
and resolved values.

### Conformance: `crates/graphite-conformance`

One suite, run by every backend against its own offscreen renderer, at 1×,
1.5× and 2× backing scale. The fractional scale catches issues that only
appear between device pixels. A shadow bug on a 1.333 desktop was only
reproducible at a fractional scale (see the
[GTK backend](backends/gtk.md#lessons-learned)). Cross-platform output differs in anti-aliasing,
gradient interpolation, blur kernels and text, so the suite never compares
images across platforms. It works in tiers:

| Tier | What | How it judges |
|---|---|---|
| A, semantic (`cases/semantic.rs`) | position and orientation, transforms, z-order, hidden, clipping, corners, borders, group opacity, masks, scrolling, image and drawing orientation, canvas state, shapes, gradients, shadow direction, pixel snapping, updates | Flat colors probed well inside or outside shapes, never on anti-aliased edges. Must pass exactly. |
| B, measured (`cases/measured.rs`) | shadow σ, gradient profile, stroke width, corner arc, opacity value | Reduce the render to a number and compare it with what the API promises, within a semantic tolerance. |
| C, review (not automated yet) | text appearance, anti-aliasing style | Same-backend golden snapshots and side-by-side human review |

Every render is written to `target/conformance/<backend>/<case>@<scale>x.png`
(override with `GRAPHITE_CONFORMANCE_OUT`). These files are the raw material
for tier C.

#### Adding a case

1. Write `pub(crate) fn my_case(ctx: &mut Ctx)` in `semantic.rs` or
   `measured.rs`. Build scenes with `ctx.scene()`, render them with
   `ctx.render(&scene)` (which commits), and judge them with
   `expect_color`, `expect_near` and `check`. `Frame::profile`, `crossing`
   and `coverage` help with measurements.
2. Add `semantic my_case` or `measured my_case` to the list in
   `__with_case_names!` (`cases/mod.rs`). That single list drives both the
   registry and the generated `#[test]` functions.
3. Probe in points, at least 2–3 points away from any edge, so that the same
   probe works at every scale.
4. Make sure the case fails when the behavior is broken. Temporarily break
   the behavior and run the case once.

Judge relations rather than absolute values when the platforms legitimately
differ. For example, the group-opacity case checks that overlap equals a
single square instead of checking a specific alpha.

Backends whose objects are bound to one thread (GTK, Windows) cannot use the
`#[test]` functions that `conformance_tests!` generates. They call
`graphite_conformance::run_main(&[&harness, …])` from a test target with
`harness = false`. It runs every case for every harness sequentially,
filters by substring (`<harness>::<case>`), and reports like libtest.

### Backend-specific tests

- `crates/graphite-gtk/tests/widget.rs`: `attach` in a real GTK window
  (frame-clock commits, natural size, scale, painted output). See the
  [GTK backend](backends/gtk.md#testing) for the environment it needs.
- `crates/graphite-macos/tests/offscreen.rs`: the offscreen renderer itself
  (read-back orientation, scaling), using raw Core Animation.
- `crates/graphite-macos/tests/view.rs`: `attach_to_view` in a real
  `NSWindow`. AppKit requires the main thread, so this target uses
  `harness = false` and provides its own `main`.
- `crates/graphite-windows/tests/window.rs`: `attach_to_hwnd` in a real
  window (DispatcherQueue commits, DPI tracking, what the window shows).
  See the [Windows backend](backends/windows.md#testing) for running it
  over SSH.

### Demo self-test

`cargo run --example demo-rs -- --selftest` animates the demo for 60 frames
from a timer or the frame clock. It fails unless every change made in a tick
was committed by the platform's commit scheduling before the next tick. This
is the only test that exercises scheduling in a real event loop. Run it for
both scenes (`--scene showcase` and `--scene ui`).

## Text (future)

Text will not be compared across platforms either. The plan is:

- **Cross-backend invariants on `TextLayout`:** narrowing the width never
  reduces the line count; `max_lines` is respected and truncation adds an
  ellipsis; index and position round-trip; bidi order is correct.
- **Per-backend self-consistency:** ink lies within the reported layout
  bounds and near the reported baselines.
- **Tier-C snapshots** for appearance.
