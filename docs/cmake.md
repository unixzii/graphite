# CMake packages and consumers

Graphite requires CMake 3.20 or newer. The core uses C11; platform prerequisites
are documented in `src/platforms/{macos,gtk,windows}/README.md`.

## Build options

| Option | Default | Effect |
|---|---|---|
| `BUILD_SHARED_LIBS` | `OFF` | Build core and enabled native backends as shared libraries instead of static libraries. |
| `BUILD_TESTING` | `ON` | Build/register tests and provide the `check` target. |
| `GRAPHITE_BUILD_EXAMPLES` | `ON` | Build demo scenes and the available native demo. |
| `GRAPHITE_BUILD_CONFORMANCE` | `ON` | Build conformance helpers and native runners. |
| `GRAPHITE_GTK` | `OFF` | Enable the GTK 4.22+ backend. |
| `GRAPHITE_SANITIZERS` | `OFF` | Enable ASan/UBSan with Clang/GCC. |

The examples and conformance switches are independent of `BUILD_TESTING`.
With testing enabled, only enabled examples/conformance tools register tests.
macOS and Windows backends are selected automatically on their platforms.
Formatting targets remain available; clang-format is required when running them,
not for building or installing the libraries.

## Install into a private prefix

Use separate build directories and prefixes for static and shared packages.
Both variants export the same target names and should not be installed together
into one prefix.

```sh
cmake -S . -B /tmp/graphite-package-build \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=OFF -DGRAPHITE_BUILD_EXAMPLES=OFF \
  -DGRAPHITE_BUILD_CONFORMANCE=OFF \
  -DCMAKE_INSTALL_PREFIX=/tmp/graphite-package
cmake --build /tmp/graphite-package-build --parallel
cmake --install /tmp/graphite-package-build
```

Add `-DGRAPHITE_GTK=ON` on Linux for GTK. On Windows use a Visual Studio developer
prompt and a private writable prefix; multi-configuration generators also need
`--config Release` for building/installing. No administrator privileges are needed.

Installation includes public headers, libraries, and the versioned CMake config
under `${CMAKE_INSTALL_LIBDIR}/cmake/graphite`. It does not install tests, demos,
conformance tools, or third-party platform SDKs. GNUInstallDirs controls the
include/library/runtime directories. No CMake user package registry is modified.

## Use from a separate project

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_app LANGUAGES C) # CXX also works
find_package(graphite 0.0.1 CONFIG REQUIRED COMPONENTS macos)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE graphite::macos)
```

Configure with `-DCMAKE_PREFIX_PATH=/tmp/graphite-package`. Available targets are
`graphite::graphite` and the installed platform's `graphite::macos`,
`graphite::gtk`, or `graphite::windows`. Platform targets transitively link core
and native dependencies. Components are optional; requesting a missing required
component fails during configuration. GTK packages rediscover GTK with pkg-config
on the consumer machine. Static Windows packages carry their native system
libraries; consumers do not need to enable C++ merely to call the C API.

The installation can be moved before configuring a consumer. Shared libraries
use sibling-relative runtime paths on macOS/Linux. On Windows, place installed
DLLs beside your executable or add the prefix's `bin` to that process's `PATH`.
Do not change the system PATH. Windows shared build-tree executables and DLLs
are colocated under `build/bin` (with a configuration subdirectory for multi-config
generators). Windows uses CMake's automatic function export support; exported
internal symbols are not a supported public API. This does not promise stable
binary compatibility across future releases.

## Independent consumer verification

`tests/package` is a standalone project; it neither adds Graphite as a
subdirectory nor accesses private headers. Configure separately for each language:

```sh
cmake -S tests/package -B /tmp/graphite-consumer-c \
  -DCMAKE_PREFIX_PATH=/tmp/graphite-package \
  -DGRAPHITE_COMPONENT=macos -DCONSUMER_LANGUAGE=C
cmake --build /tmp/graphite-consumer-c
ctest --test-dir /tmp/graphite-consumer-c --output-on-failure
```

Repeat in another directory with `CONSUMER_LANGUAGE=CXX`, and use `gtk` or
`windows` on those platforms. Each executable exercises both core and a native
backend symbol without requiring a graphical session. These checks validate
package discovery, headers, transitive linking and runtime loading, not rendering
parity. Rendering is covered by the separate native and conformance suites.

## Validation record

The package matrix passed on macOS (AppleClang), the supplied Linux VM
(GCC / GTK 4.22.4), and `ktiays_win` (MSVC ARM64): static/shared × C-only/C++-only,
12 independent configure/build/run combinations. Every consumer used an installed
prefix moved away from its original installation location. No system installation
or persistent environment changes were made.

Additional checks passed: shared demo/conformance builds on all three platforms,
Windows shared resource tests with build-tree DLL discovery, and macOS tests with
examples/conformance disabled (4/4, including native tests outside the sandbox).
A missing required package component correctly failed configuration. Formatting
and whitespace checks passed. These packaging checks do not repeat the full
cross-backend pixel-comparison matrix.
