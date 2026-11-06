# Vendored containers

`uthash.h` is copied without modification from uthash v2.3.0:

https://github.com/troydhanson/uthash/blob/v2.3.0/src/uthash.h

SHA-256: `344175d0ae3d0d7651887f932fc224f6d133559f0fc83ee73fe641697d77211a`.

The upstream copyright and BSD-style license are preserved in the header. Builds
do not fetch dependencies. Core includes it through `src/core/containers.h`, which
routes allocations through core's abort-on-OOM helpers. No uthash types appear
in the public API. The resource implementation needs only growable arrays; the
hash container is exercised by tests and available for Scene/backend maps.
