---
description: Remove a pool definition entirely. Drains running instances under Job Object closure. Destructive — requires confirmation.
argument-hint: <poolId>
---

Remove pool `$1` definition entirely.

1. `mcos_pool_get poolId=$1` — read current state.
2. `mcos_pool_leases poolId=$1` — count active leases.
3. State the destructive consequence:
   ```
   Removing pool $1:
     - pool definition deleted from mcos.json
     - <N> running instances reaped under Job Object closure
     - <M> active leases terminate (NOT drained gracefully — they end immediately)
     - this is irreversible without re-registering via /api/pools
   ```
4. **Strongly recommend draining first** if there are active leases. Ask: "Drain first or remove immediately?"
5. If operator confirms removal: `mcos_pool_remove poolId=$1 confirm=true`.
6. `mcos_pools_list` — verify the pool is gone.

Never call this without explicit operator confirmation. The bridge enforces `confirm:true` but you should also state the consequence first.

If `$1` is missing, list pools and ask which to remove.
