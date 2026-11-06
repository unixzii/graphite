# Benchmarks

These tools target macOS. Build products, logs and CSV samples are written to a
caller-selected directory. Run on an otherwise idle machine and do not overlap
benchmark runs with builds.

## Core

```sh
python3 benchmarks/run.py /tmp/graphite-perf
```

Builds O3/LTO binaries with and without allocation instrumentation. The scene
contains 1,000 or 10,000 flat container children, all with retained caller
handles. After initial commit and 20 warm-up frames, each frame updates opacity
on 0, 1, 1% or all children, then commits into a sink backend. Compiler barriers
keep scene and batch state observable. Seven timing processes run per scenario;
raw samples are in `core.csv`. No samples are discarded.

Construction time includes the first commit. Update time includes setters and
commit, excluding teardown. Clean/sparse cases run 10,000 frames, full-update
cases 1,000. Nanosecond clean-commit timings are sensitive to loop/timer overhead.
These scenarios do not measure drawing, native rendering or hierarchy edits.

Separate instrumented binaries redirect malloc/realloc/free for core and the
fixture. `malloc_size` counts usable bytes including allocator rounding.
`live_bytes` is retained heap after warm-up, including the external handle array;
it is not peak transient heap or RSS. Allocation counts include successful
malloc/realloc calls. `remaining_bytes` is the post-teardown heap delta, not a
whole-application leak guarantee.

## Native demo

Build the demo in Release, then run in a graphical session:

```sh
python3 benchmarks/native.py /path/to/demo /tmp/graphite-native-perf
```

Five processes per scene/mode write `native.csv` and complete logs, including
nonzero exit status. Snapshot measures startup, scene construction, native
rendering, readback, PNG encoding and exit together, not isolated first-present
latency. Selftest runs 60 callbacks; wall time is refresh-clock-bound and must not
be interpreted as renderer throughput. Peak RSS excludes GPU/WindowServer
allocations. OS caches are warm between samples.

Review exit codes before drawing conclusions. The macOS showcase has an observed
intermittent early-frame commit failure; see [roadmap](../docs/roadmap.md).
