# Library source

Graphite has a C11 core and native platform backends. Build, installation and
consumer configuration are documented in the [CMake guide](../docs/cmake.md).

| Directory | Responsibility |
|---|---|
| `core/` | Resources, Canvas, Scene/Layer ownership, dirty tracking and resolution |
| `platforms/macos/` | Objective-C ARC, Core Animation and CoreGraphics |
| `platforms/gtk/` | C, GTK widgets and cached GSK render nodes |
| `platforms/windows/` | C++20, C++/WinRT, Composition and Direct2D |
| `../include/graphite/` | Public C API, platform hosts and backend interface |
| `../tests/` | Shared test harness and core/native tests |
| `../conformance/` | Backend-independent rendering scenarios and capture tools |

`core/model.h` defines the private pointer model. `core/scene.c` owns scheduling,
weak links, cleanup and commit batches; `core/layer.c` owns properties and tree
operations; `core/resolve.c` derives backend coordinates and drawing values.

The registry and dirty queue are intrusive lists. Detached dirty bits are kept
until attachment. Batch storage scales with queued layers; clean commits do not
allocate. Backend maps borrow layer identities until release notification,
independently of public reference counts. User cleanup runs after consistent
state is published, with Scene/allocation pins protecting active operations.

See the [lifetime internals](../docs/core-lifetime.md), [architecture](../docs/architecture.md), and
[testing guide](../docs/testing.md) before changing these boundaries.
