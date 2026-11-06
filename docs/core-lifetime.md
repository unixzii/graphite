# Core lifetime and cleanup

Public calling conventions live in [types.h](../include/graphite/types.h) and the
individual API declarations. The backend callback and identity lease contracts
live in [backend.h](../include/graphite/backend.h). This page describes how core
maintains those contracts; see [architecture](architecture.md) for the pipeline.

## Stable identities and retirement

Layers are individually allocated and registered with a Scene. Public references
and parent/mask ownership determine model lifetime. Native maps borrow identity
pointers under a separate allocation lease; taking public references in those
maps would delay retirement rather than protect only pointer identity.

Final public layer release queues native retirement if the object was realized.
Its allocation survives until the apply callback carrying its released entry
returns. Unrealized objects can be reclaimed at the next safe cleanup boundary.
The lease prevents address reuse while a backend still has an identity mapping.

## Callback boundaries

Traversal and callback pins defer physical cleanup until the active operation is
safe. Reparenting temporarily owns the child across detach/attach. Operations
which invoke userdata cleanup pin the Scene so a destructor can release its
owner without invalidating the ongoing call.

Drawer replacement publishes consistent new state before running the old
userdata destructor. Cascading releases are processed without recursively
invalidating active traversal. Objects already being destroyed cannot be
resurrected. The resource setters acquire new references before releasing old
ones, including when caller input aliases an existing property.

## Scene teardown

Final Scene release:

1. Marks the Scene closing and invalidates weak links/drawer providers.
2. Clears layer Scene associations before user cleanup. External handles become
   inert, with default getters and empty relationships.
3. Destroys the backend while leased identity allocations remain valid. The
   backend cancels work and clears native maps without dereferencing identities.
4. Clears ownership edges, drawers and resources, then drops internal leases.
   Layers with external references remain allocated until those are released.

Scheduled callbacks can retain a weak Scene link and safely observe expiration
later; they must not retain the Scene through backend ownership.

## Commit and deferred drawing

An outstanding commit request coalesces changes. Core resolves attached dirty
layers into borrowed snapshots, then invokes the backend. Snapshot resources need
explicit refs to survive the callback. Native apply failures have no automatic
rollback or replay; platform recovery belongs to the backend.

Drawer providers check the current display version and pin the active callback.
A stale version is rejected so output cannot be cached under an obsolete key.
The returned DisplayList is owned by the caller; core does not retain recordings.
Provider lifetime does not extend a retired layer's identity lease.

`tests/lifecycle.c` demonstrates ownership and deferred work. Scene tests cover
callback-driven destruction, reentry, deferred retirement, mask replacement,
borrowed reparenting and deep iterative release. See [testing](testing.md).
