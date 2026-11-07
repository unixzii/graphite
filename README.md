# Graphite

A cross-platform, retained-mode 2D layer library with a CALayer-style model and
an immediate-mode drawing API. A C11 core exposes opaque, reference-counted C
handles. Native backends use Core Animation on macOS, GTK 4.22 / GSK on Linux,
and Windows.UI.Composition on Windows 11.

## Build

```sh
cmake -S . -B /tmp/graphite-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/graphite-build --parallel
/tmp/graphite-build/examples/showcase/showcase
/tmp/graphite-build/examples/ui/ui
```

On Linux add `-DGRAPHITE_GTK=ON` and install GTK 4.22+ development headers. On
Windows use a Visual Studio developer prompt; examples are `showcase.exe` etc. Shared
Windows builds put executables and DLLs together in `build/bin`.

`BUILD_SHARED_LIBS` selects static/shared libraries. `BUILD_TESTING`,
`GRAPHITE_BUILD_EXAMPLES` and `GRAPHITE_BUILD_CONFORMANCE` independently control
auxiliary targets. See the [CMake guide](docs/cmake.md) for installation into a
private prefix and `find_package(graphite CONFIG REQUIRED)` integration.

## Development

```sh
cmake --build /tmp/graphite-build --target check
```

The check target requires clang-format 18. Native rendering tests require a
working graphical session. Core-only CI does not require a window server.

- [Documentation](docs/README.md)
- [Public API conventions](include/graphite/types.h)
- [Examples](examples/README.md)
- [Conformance](conformance/README.md)
- [Benchmarks](benchmarks/README.md)
