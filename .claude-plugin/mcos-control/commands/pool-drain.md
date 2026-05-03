---
description: Drain a pool — mark instances Draining; sticky leases keep their bound instance, new leases route elsewhere.
argument-hint: <poolId>
---

Drain pool `$1`.

1. `mcos_pool_get poolId=$1` — read current state. Surface the lifecycle states of each instance and the active lease count.
2. `mcos_pool_leases poolId=$1` — surface the leases that will keep routing during the drain.
3. State the consequence to the operator:
   ```
   Draining pool $1:
     - all instances move to Draining state
     - <N> existing sticky leases keep routing to their bound instances until release
     - new stateless leases route to non-draining Ready instances elsewhere
     - hot-migration is forbidden (ADR-002 §8) — stateful sessions complete on their original instance
   ```
4. Wait for explicit operator confirmation.
5. `mcos_pool_drain poolId=$1 confirm=true`.
6. `mcos_pool_saturation poolId=$1` — verify `drainingInstanceCount` ticked up.
7. Report new state.

If `$1` is missing, list pools (`mcos_pools_list`) and ask which to drain.
