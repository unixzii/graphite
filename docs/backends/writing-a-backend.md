# Writing a backend

Implement a platform under `src/platforms/<name>` with its own CMake target and
public hosting header. Keep platform SDK types private unless the hosting API
requires them. Use the existing native backends as examples. Mark the header's
functions with a `GR_<NAME>_API` macro defined like `GR_MACOS_API`, and define
`GRAPHITE_<NAME>_BUILDING` privately on the target; everything else stays hidden
in shared builds.

## Backend interface

Implement `gr_backend_vtable` from [backend.h](../../include/graphite/backend.h):
`request_commit` schedules work, `apply` consumes a borrowed commit batch, and
`destroy` cancels work and releases native state. Install it once with
`gr_scene_set_backend`. Pending callbacks retain a `gr_scene_link` and lock it
when invoked, avoiding a Scene/backend ownership cycle.

Honor the [commit contract](../architecture.md#commit-contract), especially
two-pass reparenting and identity leases. Never retain borrowed arrays. Acquire
references to resources and drawer providers retained after apply. Do not call
back into Scene APIs during backend callbacks, except deferred drawing through
the documented provider interface.

Use resolved transforms, size, border, bounds offsets, shadows and image placement.
Set all native properties explicitly on realization. Keep box and local spaces
separate, and support rehoming between mask and child use within one batch.

## Drawing and caching

For drawn content, use `gr_resolved_get_draw_info` and
`gr_drawer_provider_draw`. Cache native output by display version and drawing
bounds/scale. Release the DisplayList after replay unless retaining it serves a
specific native need. A stale provider/version returns an error rather than
executing obsolete work. Respect the identity retirement boundary even when a
provider itself remains alive.

Replay `GR_DRAW_TEXT_LAYOUT` ops only for layouts made by your platform's text
system: compare `gr_text_layout_get_vtable` with your static layout vtable and
draw `gr_text_layout_get_data` natively; skip other layouts. Text systems are
separate from the Scene backend (see [text.h](../../include/graphite/text.h)):
build them with `gr_text_system_new` and `gr_text_layout_new`, and make every
callback safe to call from any thread.

Snapshot backends cache nodes and invalidate changed layers and ancestors.
Retained compositors update native properties and backing stores incrementally.

## Hosting and validation

Provide host attachment, backing-scale tracking, commit scheduling and offscreen
readback. Attachment functions return a `gr_scene_host` made with `gr_scene_host_new`
and a static `gr_scene_host_vtable`; functions for your hosts compare
`gr_scene_host_get_vtable` with it and return NULL/false for other hosts.
Hosts with a native input surface fill in the event callbacks of the vtable and
report native events with `gr_scene_host_dispatch_event`: deliver a requested
`GR_EVENT_FRAME` before the frame's commit point, report size/scale changes as
`GR_EVENT_VIEWPORT` (repeats are dropped by core) and keyboard focus changes.
A handler may release the host, so do not touch native state after dispatching.
Clear the native side's host pointer in `destroy` before tearing it down. Capture top-down premultiplied RGBA8 at the requested scale; verify
orientation using an asymmetric native scene first. Use `gr_conf_harness` from
`conformance/conformance.h` for rendering cases and the shared unit test harness
for hosting/lifetime tests. Test idle wakeup, resizing, teardown, masks and
reparenting. Native tests need a suitable graphical session.

Add a shell under `examples/shared/platform` implementing `platform.h` and select
it in `examples/shared/CMakeLists.txt`; every example then builds for it. Document prerequisites, mapping, known limitations and tested environments.
