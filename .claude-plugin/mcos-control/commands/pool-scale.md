---
description: Force a pool to its minInstances. Useful after a Failed instance, or to warm up a previously cold pool.
argument-hint: <poolId>
---

Force pool `$1` to honor its `scalePolicy.minInstances`.

1. `mcos_pool_get poolId=$1` — read current state.
2. State the action: "Pool $1 currently has <N> instances (<R> Ready, <S> Starting, <F> Failed). Scaling to minInstances=<M> will spawn <M-N> new instances if needed."
3. `mcos_pool_scale poolId=$1`.
4. `mcos_pool_get poolId=$1` again — verify the lifecycle moved.
5. Report new state. If any new instance lands in `Failed`, hand off to mcos-troubleshooter.

This is non-destructive (it only ADDS instances) so no `confirm:true` guard.

If `$1` is missing, list pools and ask which to scale.
