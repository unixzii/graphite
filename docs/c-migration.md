# C migration: decisions and implementation plan

Status: stage 1 interfaces committed; stage 2 resource/Canvas implementation and
CMake tests are in `src/`. Scene/Layer migration has not started. See
[interface contracts](../src/CONTRACT.md) and [implementation status](../src/README.md).

Planning baseline: `4e612d7135cf39a47fd3ad6e9b12dbfe7cd7887e` (2026-11-06).
Record subsequent behavior changes explicitly while other development continues.
The baseline's public behavior and conformance suite remain the reference until
a deliberate change is documented.

## Decisions

### Implementation and integration

- Implement the platform-independent core in C, with opaque C handles as the
  public API.
- Implement backends in their platform's appropriate language: Objective-C on
  macOS, C on GTK, and C++ on Windows. Their boundary with core is C.
- Use CMake for the complete build, enabling platform languages and dependencies
  only on the corresponding targets.
- Core defines a backend vtable. Core and backends are built and linked together;
  no independently versioned backend plugin ABI is required. Do not add
  `abi_version` or `struct_size` fields for that hypothetical use case.
- Small single-header container libraries are acceptable for maps and similar
  infrastructure. Minimize dependencies and keep container types out of public
  headers. The specific library is not selected yet.
- Preserve retained layers, native compositors, semantic cross-platform
  consistency, and the existing separation from a widget toolkit. This is a
  language/ownership migration, not a new GPU renderer.

### Handles and ownership

- Use stable object pointers, including `gr_layer *`, instead of SlotMap keys or
  generation-based layer handles. An object's address stays stable until final
  destruction. Do not reallocate live objects when a registry grows.
- Use `new`, `ref`, and `unref` consistently. `new` returns one owned reference;
  `ref` acquires another and returns the same pointer; `unref` releases one.
- Setters that retain objects acquire their own references. Getters return
  borrowed pointers unless explicitly documented otherwise. Document each
  borrowed pointer's lifetime and invalidating operations.
- Parent layers own references to children, mask users own references to masks,
  and the Scene owns its root. Back-pointers are non-owning and must be cleared
  when their targets cease to be valid.
- Queues, commit records, and backend mappings use pointers with explicit
  lifetime rules. Protect queued objects until consumed, and remove stale native
  mappings before an address can be reused.
- Keep delayed destruction and callback lifetime protection. Drawers run with
  the Scene read-only and cannot replace themselves. Cleanup callbacks may
  release other objects; they must not invalidate the core's current traversal.
- On Scene teardown, invalidate surviving externally referenced layer objects
  and clear their Scene association. They remain safe to `unref`; preserve the
  baseline's inert setter/default getter behavior. Do not retain the whole Scene
  through every layer and introduce a Scene/layer reference cycle.
- Reference counting is an ownership mechanism, not arbitrary pointer
  validation. A caller must own or validly borrow a reference before using it.
  Use-after-free, over-release, invalid casts, and unsynchronized access are
  caller errors. Structural rules such as cross-Scene attachment and cycles
  still require explicit handling; pointer handles do not make them legal.

Illustrative naming, not a complete API:

```c
typedef struct gr_scene gr_scene;
typedef struct gr_layer gr_layer;
typedef struct gr_path gr_path;

gr_layer *gr_layer_new(gr_scene *scene);
gr_layer *gr_layer_ref(gr_layer *layer);
void gr_layer_unref(gr_layer *layer);
```

### Threading

- Scene and layer APIs run on one owning thread, normally the platform's main
  thread. Honor stricter host API requirements where applicable.
- Resource preparation may run on other threads and the finished resources may
  be sent to the consuming thread. Freeze resources before sharing them.
- Do not add compile-time thread-affinity machinery or runtime thread checks.
  Correct thread usage and synchronized publication are caller obligations.
- Use ordinary reference counts for thread-confined objects and atomic reference
  counts for resources whose references may be shared/released across threads.
  Atomic reference counting does not synchronize mutable object contents.
- Builders are exclusively used during construction; the finished resources are
  immutable. Platform resources with additional restrictions must document them.

### Drawing and backend contract

Preserve the **current baseline**, rather than the earlier proposal in which
core ran drawers during commit and retained their resulting DisplayLists:

- Core tracks display versions and emits resolved commit updates.
- Backends request a drawing through the drawer provider during `apply` or later
  when they need it. They retain their rendered result and can discard/rebuild it.
- Drawer-produced DisplayLists can be dropped after rendering. Do not introduce
  mandatory core-side retention of these transient recordings during migration.
- An explicitly retained DisplayList owns references to resources in its ops
  for as long as that DisplayList remains alive.
- General backend-to-Scene reentry remains prohibited; invoking drawers is the
  documented exception. Preserve the baseline's permitted callback mutations.

The backend interface starts with scheduling, applying commits, and destruction:

```c
typedef struct {
    void (*request_commit)(void *backend);
    void (*apply)(void *backend, const gr_commit_batch *batch);
    void (*destroy)(void *backend);
} gr_backend_vtable;
```

The batch is borrowed during `apply`. Persistent backend state must acquire its
own references. The drawer provider needs an explicit owned lifetime if retained
for deferred drawing; it must safely become inactive when its Scene disappears.
Precisely specify that mechanism before implementing the adapter.

A drawer consists of a function, userdata, and a userdata destructor. After a
successful installation, the core owns the destruction obligation. Replacement,
removal, failure, and in-flight invocation must have unambiguous ownership rules.

### Text direction, implemented separately

The agreed text direction fits this architecture but is not part of functional
parity for the initial migration:

- A backend `layout_text` operation accepts text and constraints and returns an
  opaque, reference-counted layout calculated by the platform.
- The layout supports measurement, line information, hit testing, caret geometry,
  and selection queries. Internal query data may be loaded lazily.
- Canvas can draw a layout. Core does not cache layout results; users and backend
  implementations decide whether and how to cache.
- Separate layout attributes from paint attributes. Colors, underlines, and
  strikethroughs do not enter shaping/layout cache keys or create shaping-run
  boundaries merely because their paint differs.
- Native layout objects and raster caches are backend implementation details.
  Use native final-line layout as the initial correctness baseline, rather than
  splitting an already shaped long line at arbitrary glyph boundaries.
- Finalize index/affinity semantics, paint boundaries inside ligatures, layout
  compatibility, and platform threading before implementing this API.

## Implementation stages

These are the work sequence and acceptance gates; the status above and the
implementation status document distinguish completed work from future stages.
Keep existing Rust implementation and tests available until replacement is
validated. Avoid unrelated rendering or API behavior changes during translation.

### 1. Specify the C contract

Write public and backend headers covering handles, errors, getters/setters,
resource ownership, callback ownership, Scene teardown, and deferred drawers.
Keep platform SDK and container implementation types private.

Acceptance: review representative create/attach/draw/detach/destroy sequences,
including rejected drawer mutation, cleanup callbacks releasing other objects,
and external layer references that outlive a Scene. Resolve any ownership
ambiguity before implementing them.

### 2. Establish C infrastructure and immutable resources

Add CMake targets, select the small container dependency, and implement allocation
helpers, reference counting, geometry, colors, paths, images, paints, Canvas, and
DisplayList operations. Do not port SlotMap/generation handles.

Acceptance: resource ownership and recording tests pass, including releasing
caller references after recording. Build the public headers from C and C++.
Run applicable address/undefined-behavior sanitizers. Custom allocator APIs and
arena optimizations are deferred until there is a concrete need.

### 3. Port Scene and Layer semantics

Implement stable layer allocations, parent/mask ownership, dirty tracking,
attachment/realization, resolved values, commit ordering, display versions,
drawer invocation, delayed destruction, and Scene invalidation.

Acceptance: translate the recording-backend and MirrorTree contract checks;
cover reparenting, masks, detached updates, callback reentry, destruction, and
resource replacement. Check behavior against the selected Rust baseline.

### 4. Validate the C core using existing backends

Implement a temporary Rust adapter for the C backend vtable and reuse existing
native backends and conformance scenarios. The adapter may translate C pointer
identities into existing Rust identifiers internally; this is temporary and is
not part of the C API. Account for that bridge explicitly rather than assuming
Rust CommitBatch objects can be passed through unchanged.

Acceptance: C core drives real rendering and satisfies the existing semantic
and measured checks. Verify deferred drawer lifetimes across the boundary.
If full adapter translation exceeds the cost of a direct backend port, retain
the recording comparison and bring up the macOS backend first instead.

### 5. Replace platform backends

Recommended order:

1. macOS in Objective-C: hosting, run-loop scheduling, Core Animation mapping,
   CoreGraphics replay, scale changes, and offscreen rendering.
2. GTK in C: GObject ownership, frame-clock scheduling, GSK node caching,
   snapshot composition, and offscreen rendering.
3. Windows in C++: COM/WinRT ownership, DispatcherQueue integration, composition
   surfaces, Direct2D replay, and error/cleanup paths.

Acceptance per backend: existing conformance cases plus real-window validation,
scale/content updates, teardown, and platform-specific resource checks. Preserve
baseline rendering and cache policies so differences can be attributed to the
migration. Do not require pixel-identical output across platforms.

### 6. Package and switch the primary implementation

Provide CMake install/export support, public headers, native libraries, a C
example, and platform hosting examples. Add a small Rust ownership wrapper where
needed to preserve existing consumers/examples. Document platform prerequisites.

Acceptance: clean consumer builds, applicable sanitizers/leak checks, and
comparative measurements of allocations, memory, commit cost, first frame, and
steady-state rendering. Record platform and build settings; do not assume a
language change improves performance. Remove temporary adapters and retire the
old implementation only after parity is established.

### 7. Add text as a separate feature

Implement the opaque text-layout contract and each platform adapter after the
migration baseline is stable. Add cross-backend semantic tests for wrapping,
global line limits, ellipsis, bidi, grapheme boundaries, and position queries.
Keep layout/raster cache policy backend-owned and measure it independently.

## Milestones

| Milestone | Evidence |
|---|---|
| C core replaces the Rust core | ownership/commit checks and rendering through an adapter or first native backend |
| Three native backends replace Rust backends | conformance and real-window evidence on macOS, GTK, and Windows |
| C library is ready for consumers | reproducible builds, headers/examples, lifetime checks, and performance comparison |

## Details still to settle

- C language baseline and the container library/license.
- Error reporting, allocation-failure policy, and null-argument conventions.
- Exact Scene teardown sequencing, deferred drawer-provider representation,
  and backend cleanup order while pointer identities remain valid.
- Static/shared packaging defaults and the scope of the Rust convenience wrapper.
- The final text API and native-resource threading contracts.

These details do not reopen the decisions to use CMake, stable pointer handles,
`new/ref/unref`, or a backend vtable without plugin-version fields.
