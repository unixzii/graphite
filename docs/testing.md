# Testing

Configure and run the enabled suites with:

```sh
cmake -S . -B /tmp/graphite-test -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graphite-test --target check
```

`check` builds tests and enabled demo/conformance targets, runs clang-format 18
validation and invokes CTest with progress output. `BUILD_TESTING=OFF` disables
tests; examples and conformance have independent build switches. See
[CMake packages](cmake.md).

## Core

`tests/resources.c` checks immutable copies, recording ownership, validation and
cross-thread resource references. `tests/scene.c` checks a recording backend and
mirror tree, commit ordering, reentry, masks, deferred drawers and teardown.
`tests/text.c` checks attributed text validation, span copies and UTF-16
mapping, and drives text layouts through a fake text system to check caret
normalization, lifetimes and text recording.
`tests/lifecycle.c` demonstrates ownership through an entire Scene lifetime.
The shared harness supports listing/filtering and reports each case; failures
remain active in Release builds. See [coverage notes](core-test-audit.md).

```sh
ctest --test-dir /tmp/graphite-test -R 'graphite.(resources|scene|text)$' --output-on-failure
cmake -S . -B /tmp/graphite-asan -DGRAPHITE_SANITIZERS=ON
cmake --build /tmp/graphite-asan --target graphite_resource_tests graphite_scene_tests graphite_text_tests
ctest --test-dir /tmp/graphite-asan -R 'graphite.(resources|scene|text)$' --output-on-failure
```

Sanitizers require Clang/GCC. Windows MSVC builds use separate Debug/Release
checks. GTK's optional external-library leak suppressions are documented in
[its backend guide](../src/platforms/gtk/README.md).

## Native and rendering

Platform tests verify hosting, scaling, idle scheduling, snapshots and lifetime.
They need a GUI/renderer session: macOS needs Metal/WindowServer, GTK a graphical
display, and Windows an interactive desktop rather than SSH session 0.

[Conformance](../conformance/README.md) contains semantic assertions and measured
edge/blur/coverage probes at three scales. Compare captures from the same backend
and renderer with `conformance/compare.py`; cross-platform output is not required
to be pixel-identical. Keep visual review separate from numerical acceptance.

Demo selftests run 60 callbacks and detect changes left uncommitted at the
backend's commit point: before the next tick on GTK and Windows, and at every
main run-loop idle point on macOS. They test scheduling, not throughput.

## Package consumers and benchmarks

`tests/package` independently configures C or C++ projects using installed targets.
Run against both static/shared packages and relocated installation prefixes.
[Benchmarks](../benchmarks/README.md) measure core work and native demo processes;
keep timing runs separate from instrumented allocation runs and builds.

## Future text tests

When text support is added, cover wrapping, line limits, ellipsis, bidi,
grapheme boundaries, caret/selection queries, layout lifetime and paint-only
changes. Use semantic assertions across platforms and native reference captures
for platform-specific rasterization.
