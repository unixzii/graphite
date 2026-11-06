# C / Rust migration benchmarks

These probes compare implementations, not the intrinsic cost of the languages.
They currently target macOS: heap statistics use `malloc_size`, and native
process measurements use the macOS `/usr/bin/time -lp` output.

## Core

```sh
python3 benchmarks/run.py /tmp/graphite-perf
```

The script builds both implementations at O3 with whole-program LTO, then runs
seven independent processes per timing case, alternating execution order. It
writes raw samples to `core.csv`. No samples are discarded. Run on an otherwise
idle machine; do not run other builds or the native benchmark concurrently.

Both programs construct a root with 1,000 or 10,000 container children and retain
all child handles. A sink backend consumes batches without rendering. After an
initial commit and 20 warm-up frames, each measured frame changes opacity on
0, 1, 1%, or 100% of the children, then commits. Frames alternate 0.5/1.0 opacity.
Compiler barriers keep scene and batch state observable with LTO enabled.
`create_ns` includes construction and the initial commit; `update_ns` includes
setters and commit, but not teardown. Clean/sparse cases run 10,000 frames and
full-update cases run 1,000. The very short clean timings are sensitive to timer
and loop overhead and should not be used to claim application-level speedups.

Memory/allocation statistics come from separate instrumented binaries, never
from the timing binaries. C redirects core and fixture malloc/realloc/free;
Rust uses a counting System allocator. Both count usable allocation bytes via
`malloc_size`, including allocator size-class rounding, and include the retained
handle array. Rust startup/argument allocations are subtracted at the baseline.
Allocation counts include successful malloc/realloc calls, not bytes allocated.
`live_bytes` is the retained heap after warm-up, not peak RSS or peak transient
heap. `remaining_bytes` is the delta after all benchmark objects are destroyed;
it does not prove absence of leaks in unexercised code or platform frameworks.

## Native demo process

Build the C and Rust demos in Release, then run outside a rendering-restricted
sandbox:

```sh
python3 benchmarks/native.py /path/to/c/demo /path/to/rust/demo-rs /tmp/graphite-native-perf
```

Five processes per language/scene/mode, alternating order, write `native.csv` and
individual logs. Snapshot measures launch, scene creation, native rendering,
readback, PNG encoding and exit together: it is **not** an isolated first-present
measurement, and the two PNG implementations differ. Selftest runs 60 animation
ticks; wall time is refresh/clock-bound, so CPU time is more useful than its
apparent FPS. Peak RSS covers the process, not WindowServer or GPU allocations.
OS caches are warm between runs; these are not cold-machine startup numbers.
