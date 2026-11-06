# Graphite documentation

| Guide | Contents |
|---|---|
| [Architecture](architecture.md) | Ownership, dirty tracking, commit batches and coordinates |
| [Layer semantics](layer-model.md) | Geometry, masks, drawing and resources |
| [API conventions](../include/graphite/types.h) | Ownership, errors, threading and values |
| [Core lifetime](core-lifetime.md) | Cleanup boundaries, retirement and Scene teardown |
| [CMake packages](cmake.md) | Build switches, install/export and consumers |
| [Testing](testing.md) | Core, native and rendering tests |
| [Core coverage](core-test-audit.md) | Covered contracts and remaining test opportunities |
| [Writing a backend](backends/writing-a-backend.md) | Backend vtable and hosting requirements |
| [macOS](backends/macos.md), [GTK](backends/gtk.md), [Windows](backends/windows.md) | Native implementations |
| [Platform notes](platform-notes.md) | Rendering capabilities and constraints |
| [Roadmap](roadmap.md) | Implemented features and planned work |

The API reference is in the documented headers under `include/graphite/`.
Public functions need documentation comments. Use clang-format 18 and the
`format-check` target; format only changed code. Add model tests under `tests/`
and rendering cases under `conformance/`. Use the shared test harness for
progress output and checks that remain active in Release builds.
