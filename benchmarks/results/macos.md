# macOS migration comparison

Measured on Apple M3 Pro, macOS 27.2 (26B5101f), AppleClang 21.0.0 and Rust 1.98.0,
using the implementation at `e5ef6d5` plus the benchmark fixtures. Both core
binaries use O3 and whole-program LTO. See ../README.md for methodology and
limitations. Values below are medians of seven independent timing processes.
Memory/allocation values are from a separate instrumented process per case.

## Core results

| Children | Changed/frame | C µs/frame | Rust µs/frame | Rust / C | C allocs/frame | Rust allocs/frame |
|---:|---:|---:|---:|---:|---:|---:|
| 1000 | 0 | 0.0013 | 0.0090 | 6.94 | 0 | 0 |
| 1000 | 1 | 0.0730 | 0.0773 | 1.06 | 3 | 2 |
| 1000 | 10 | 0.2163 | 0.6342 | 2.93 | 3 | 6 |
| 1000 | 1000 | 28.0200 | 32.6220 | 1.16 | 3 | 18 |
| 10000 | 0 | 0.0014 | 0.0090 | 6.43 | 0 | 0 |
| 10000 | 1 | 0.0720 | 0.0778 | 1.08 | 3 | 2 |
| 10000 | 100 | 1.8799 | 4.4629 | 2.37 | 3 | 12 |
| 10000 | 10000 | 284.8940 | 808.2936 | 2.84 | 3 | 26 |

Clean-commit nanosecond timings are a loop-level microbenchmark, not a meaningful
application speedup. Single-layer updates are close; broader dirty batches show
a larger difference. These measurements cover flat container trees and opacity
changes, not drawers, paths, masks, hierarchy edits, or native GPU submission.

| Children | C retained heap | Rust retained heap | C reduction |
|---:|---:|---:|---:|
| 1000 | 529,088 bytes | 549,088 bytes | 3.6% |
| 10000 | 5,333,696 bytes | 8,552,672 bytes | 37.6% |

Every instrumented case returned to its pre-scene heap baseline after teardown.
This is retained usable heap, not peak transient allocation or whole-process RSS.
The ratio changes with layer count: Rust uses a capacity-growing SlotMap, while
C allocates individual stable layer objects. Capacity and object representation
are plausible contributors; this benchmark does not isolate their shares.

C currently allocates three temporary batch arrays on each dirty commit even
for one-layer changes (`src/core/scene.c`). Rust takes/drops its dirty Vec and
grows batch Vecs (`crates/graphite/src/scene.rs`), so its allocation count rises
with batch size. Neither implementation has a zero-allocation dirty-commit path.
This suggests reusable batch storage as a targeted future optimization; no
implementation changes were made as part of this measurement.

Construction plus first core commit (median across the seven full-update-case
processes): 1,000 children C 0.131 ms / Rust 0.176 ms; 10,000 children
C 1.181 ms / Rust 1.663 ms. This excludes native rendering.

## Native demo process results

Five samples per cell; medians include all samples, including nonzero exits.
Native C uses CMake Release defaults; Rust uses the workspace Release profile
with fat LTO. These reflect shipped build settings, unlike the matched-LTO core
probe. The timing tool reports seconds with limited precision.

| Scene | Mode | C wall / CPU seconds | Rust wall / CPU seconds | C peak RSS MiB | Rust peak RSS MiB |
|---|---|---:|---:|---:|---:|
| showcase | snapshot | 0.08 / 0.04 | 0.12 / 0.08 | 41.31 | 42.27 |
| showcase | selftest | 0.77 / 0.27 | 1.22 / 0.32 | 99.02 | 96.34 |
| ui | snapshot | 0.07 / 0.03 | 0.07 / 0.03 | 36.94 | 38.31 |
| ui | selftest | 0.77 / 0.15 | 1.21 / 0.15 | 83.33 | 80.55 |

All 20 snapshot processes succeeded. Snapshot includes PNG encoding and process
startup; it is not a pure renderer throughput or first-present measurement.
Native memory does not show the core benchmark's large percentage reduction.

**Selftest is not a matched-cadence comparison.** C uses CADisplayLink while Rust
uses a 1/60-second NSTimer. Their 60 updates cover different elapsed animation
intervals and different scheduling overhead. Thus shorter C wall time does not
establish higher renderer throughput, and even CPU/RSS comparisons are only
observations of these demo configurations. GPU/WindowServer memory is excluded.

C showcase exited 1 in one of five final selftest samples, reporting
`frames=60 uncommitted=[2] backing_scale=2`. The other 19 selftests passed. An
earlier preliminary batch also stopped on a C showcase failure before recording
the failing output; a standalone rerun passed. The final runner preserves exit
codes and failed logs instead of filtering them. This suggests an intermittent
scheduling/commit-order issue to investigate; the root cause is not established
and no scheduling fix is included in this benchmark change.

## Conclusions and next work

- Core sparse/full opacity updates benefit from the current C implementation;
  single-layer update costs are close. The result is workload-specific.
- Retained core heap is 3.6% lower at 1,000 children and 37.6% lower at 10,000.
  Do not extrapolate a single percentage to UI applications.
- Dirty C commits still allocate three arrays. Reusing transient batch storage
  is a concrete optimization candidate, requiring lifetime-safe implementation.
- Investigate the observed macOS display-link/commit ordering failure before
  relying on the demo selftest as a stable performance harness. A controlled
  native benchmark with equal cadence and equal animation timestamps is needed
  for a renderer-throughput conclusion.
- These are macOS measurements only. GTK/Windows performance, deep trees,
  resource-heavy drawers, and peak transient core heap have not been measured.
