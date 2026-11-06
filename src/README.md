# C implementation

The C and Rust implementations coexist. CMake currently builds a static
`graphite::graphite` target containing immutable resources and Canvas recording.
Scene/Layer, native backends, and text remain declarations/planned work; this
target cannot yet replace the complete Rust library.

## Build and test

Use an out-of-tree build directory:

```sh
cmake -S . -B /tmp/graphite-c -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/graphite-c
ctest --test-dir /tmp/graphite-c --output-on-failure
```

Address/undefined-behavior sanitizers can be enabled with
`-DGRAPHITE_SANITIZERS=ON` on Clang/GCC. Tests use always-active checks, including
in Release. The library requires C11 atomics; the platform toolchain must support
them. Headers are also compatible with C++ and Objective-C.

## Implemented scope

- Allocation/overflow helpers, atomic shared-resource reference counts, ordinary
  builder/Canvas reference counts, unique resource IDs, private array storage,
  and a pinned private uthash dependency.
- Paint and Stroke with copied gradient stops/dashes and retained resources.
- Path/PathBuilder with rectangles, cubic ellipse approximation, selective
  rounded corners, validation, and reusable builders.
- Image with copied premultiplied RGBA8 bytes and dimensions/scale validation.
- Canvas and immutable DisplayList: balanced graphics state, path/image resource
  ownership, recording validation, and backend operation views.
- Geometry/color validation used by these operations. Scene transform resolution,
  pixel snapping and shadow resolution remain part of the Scene/backend stages.

`review/lifecycle.c` remains a compile-only full-API example until Scene/Layer
exist. Executable resource tests live in `tests/resources.c`.

## Validation recorded

On macOS arm64 with AppleClang 21:

- Debug with ASan/UBSan and Release CTest passed.
- Coverage includes copied inputs, invalid-input atomicity, all rounded-corner
  combinations, recording retention after caller unref, unfinished recording
  cleanup, builder/Canvas reuse, and four-thread shared ref/unref/read access.
- uthash insertion/lookup/deletion and resource identity behavior passed.
- Every public header passed standalone C11, C++17 and Objective-C checks with
  strict warnings; a C++ consumer linked and ran against the C static library.
- macOS `leaks --atExit` reported zero leaked bytes for the resource tests.

Linux and Windows runtime/compiler validation has not yet been performed.
This is not rendering, performance, or native-backend conformance evidence.
