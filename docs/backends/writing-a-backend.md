# Writing a backend

Implement a platform under `src/platforms/<name>` with its own CMake target and
public hosting header. Keep platform SDK types private unless the hosting API
requires them. Use the existing native backends as examples.

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

Snapshot backends cache nodes and invalidate changed layers and ancestors.
Retained compositors update native properties and backing stores incrementally.

## Hosting and validation

Provide host attachment, backing-scale tracking, commit scheduling and offscreen
readback. Capture top-down premultiplied RGBA8 at the requested scale; verify
orientation using an asymmetric native scene first. Use `gr_conf_harness` from
`conformance/conformance.h` for rendering cases and the shared unit test harness
for hosting/lifetime tests. Test idle wakeup, resizing, teardown, masks and
reparenting. Native tests need a suitable graphical session.

Add a shell under `examples/demo/platform`, sharing the existing scene code and
CLI. Document prerequisites, mapping, known limitations and tested environments.
