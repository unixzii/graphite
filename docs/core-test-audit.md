# Core unit-test audit

This guide records resource/Scene contract coverage measured using Clang source coverage.
Native backends, demos, and rendering conformance are excluded from these
coverage figures; they exercise additional core paths separately.

## Contract coverage

| Contract area | C coverage and finding |
|---|---|
| First realization, clean commits, coalescing, detached changes | `basics`; covers attachment and commit behavior. |
| Reparenting, reorder, parent ownership | `tree` plus `borrowed_reparent_and_reorder`; checks both model and committed backend order, including transfer without an external owning reference. |
| Cycles, cross-Scene/root restrictions, invalid masks | `tree` plus `rejected_mutations_stay_clean`; rejected calls preserve hierarchy, values, the mask, request count, and clean status. |
| Mask property | `mask_property`; path and gradient masks reach the snapshot with `GR_DIRTY_MASK`, equal values coalesce, clearing works, and the layer owns the mask's resources until replacement or teardown. |
| Deferred drawing, stale versions, read-only callbacks | `drawers`; covers deferred execution, reentry rejection, backing scale, version invalidation and inactive providers. |
| Drawer cleanup and ownership | `drawers`, `cascade`, `teardown_reentry`, plus `rejected_drawer_preserves_ownership`; invalid and wrong-kind installation retain the old drawer and leave rejected userdata owned by its caller. |
| Scene teardown and backend reentry | `deferred_release`, `teardown_reentry`, `unavailable_backend`, `deep_tree`, lifecycle example; includes callback-driven final releases and deep iterative destruction. |
| Geometry, snapping, borders, shadow outlines, dirty dependencies | `geometry`, `property_dependencies`; main contract behavior is covered, but not every numerical input/transform combination. |
| Immutable resource copies, retention, thread-shared refs | `resources`, `paths`, `shared_resources`; includes retained resources after caller release and concurrent ref/unref. |
| Canvas failure and recording boundaries | `recording_failure_and_state_isolation` adds invalid-operation atomicity, nested save/restore balancing, reuse after finish, and resource survival after the previous recording is released. |

## Coverage and validation

Measured on macOS with AppleClang, both core executables merged into one profile:

| Core scope | Line coverage |
|---|---:|
| Total including internal inline headers | 84.51% |
| scene.c | 96.38% |
| layer.c | 66.61% |
| canvas.c | 96.20% |

Overall branch coverage is 66.27%. The suites contain 15 Scene cases and 6 resource cases. Debug ASan/UBSan and Release
both pass, as do format-check and whitespace checks. These coverage measurements are from local macOS runs.

Coverage reproduction (use a private build directory): configure with
`-DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` and
`-DCMAKE_EXE_LINKER_FLAGS=-fprofile-instr-generate`, build the two core test
executables, run each with a distinct `LLVM_PROFILE_FILE`, merge using
`xcrun llvm-profdata merge -sparse`, and pass both executables to
`xcrun llvm-cov report` with `-object` and `-instr-profile`. Restrict the report to
`src/core` so test code and vendor code do not inflate coverage.

## Remaining opportunities

The main remaining Layer gaps include kind-specific property readback/inert
fallbacks, shape path/trim/fill-rule mutations, and equality comparisons across
radial-gradient and stroke-style variants. Native conformance exercises some
normal rendering paths, but does not replace targeted failure/no-op tests for
these contracts. These are useful follow-ups when changing those areas; the
present audit does not claim complete Layer API coverage.

Allocation failure, reference-count overflow, and impossible internal invariant
abort paths are not exercised. Chasing 100% line coverage with trivial accessor
checks or forced allocator failure would not be the best next investment.
The highest-value ownership/commit boundary gaps are covered without duplicating the renderer conformance suite.
