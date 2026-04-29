# Proof-of-Working — Evidence Contracts

Each README feature pillar has a contract below. A feature is only marked
"working" when its receipt file exists, is non-empty, and contains the
specified artifacts. No self-reporting without receipts.

**Assembly date:** 2026-04-19. **Build:** build 48 (commit `d187951`).

---

## Feature 1 — Multi-binary control plane
**Receipt:** `01-multi-binary.md`
**Contract:**
- Three binaries running simultaneously (service + shell + browser-serving process).
- `/api/health` returns HTTP 200 from the service.
- Shell's admin-API-reachable indicator shows "API REACHABLE".
- Browser admin URL serves HTML > 1KB.
- All three statically link `MasterControlApp.lib` (proven via binary size diff).

## Feature 2 — Auto-Connect AI providers — RETIRED (ADR-001 Phase 2)
**Receipt:** removed (`02-auto-connect.md` deleted alongside the provider stack).
**Status:** feature removed from the product. Replacement is the LAN client
config bundle delivered by `GET /api/clients/{id}/config` in Phase 5 of
`plans/remediation/01-gut-and-rebuild.md`. New proof artifact planned for
Phase 9 as `11-lan-client-end-to-end.md`.

## Feature 3 — Live command stream
**Receipt:** `03-command-stream.md`
**Contract:**
- `/api/activity` returns `{events[], highWaterMarkId}`.
- Each event has `id`, `timestampUtc`, `method`, `target`, `statusCode`,
  `latencyMs`, `actor`, `kind`.
- After firing 600 requests, `events.Count == 512` (cap enforced).
- `highWaterMarkId` monotonically increases to match total fires.
- Oldest events evicted in FIFO order.
- `/api/health`, `/api/dashboard`, `/api/config` deliberately excluded
  from the ring (proven by test).

## Feature 4 — CLU governance
**Receipt:** `04-clu-governance.md`
**Contract:**
- `/api/clu` returns > 10KB JSON with `posture`, `rules[]`, `availableTools[]`,
  `findings[]`, `doctrine`, `recentExecutions[]`.
- `/api/clu/tools` returns tool descriptors.
- `/api/clu/apple-operations` returns JSON array (may be empty).
- `POST /api/clu/execute` with a real governance operation →
  HTTP 200, response body valid, `recentExecutions[]` on next read
  contains the new execution.
- Forsetti module manifests for CLU present on disk (4 manifests expected).

## Feature 5 — Cross-platform gateways
**Receipt:** `05-platform-gateways.md`
**Contract:**
- `/api/platform-services/gateways` returns Windows/macOS/iOS gateway descriptors.
- `/api/platform-services/governance` returns governance lanes.
- `POST /api/platform-services/apple-hosts` with valid body → `succeeded:true`.
- `GET /api/platform-services/apple-hosts` shows the added host.
- `POST /api/platform-services/apple-hosts/remove` removes it.
- `POST` with invalid body → HTTP 400 with readable error.
- Governance profile file exists on disk.

## Feature 6 — Repo-owned installer
**Receipt:** `06-installer.md`
**Contract:**
- Bootstrapper help lists all 7 actions (detect|preflight|install|repair|upgrade|validate|uninstall).
- `bootstrapper preflight <dir> --json` → `ready:true`, serviceManaged:true.
- `bootstrapper validate <dir> --json` → `valid:true` on a healthy install.
- Service is registered and RUNNING.
- Shortcuts present (Start Menu at minimum).
- Programs & Features registration present.
- MSI artifact exists in `dist/packages/release/`.

## Feature 7 — Tron aesthetic
**Receipt:** `07-tron-aesthetic.md`
**Contract (corrected — product encodes cyan as `#00FFFF` via
`ShellAccentBrush`, not as the 0x00f6ff / 0x00dfff / 0x0a1018 /
0x050b12 literals the earlier contract drafts listed):**
- ≥15 cyan-token occurrences in App.xaml — counted as any of
  `#00FFFF` literal OR `ShellAccentBrush` StaticResource reference.
- `Bahnschrift SemiCondensed` referenced in ≥3 style setters.
- `prefers-reduced-motion` CSS block in `resources/web/styles.css`.
- Focus-visible outlines: ≥20 `FocusVisual` / `FocusState` refs in App.xaml.
- No literal em-dashes rendered as mojibake (confirmed by /utf-8 flag + \u2014 escapes).

---

## Bonus — Starter workflows
**Receipt:** `08-starter-workflows.md`
**Contract:**
- `/api/setup/workflow-templates` returns 3 templates.
- `single-provider-demo` instantiate → succeeded:true, 1 assignment.
- `mcp-assisted-demo` instantiate → succeeded:true, 1 assignment.
- `specialist-team-demo` instantiate → if prereqs met: 4 assignments;
  if not: succeeded:false with clear prereq message.

## Bonus — Sub-agent roster
**Receipt:** `09-sub-agents.md`
**Contract:**
- `/api/dashboard` → 7 sub-agent endpoints (sentinel, architect, forge,
  scribe, recon, nexus, watchtower/watchdog).
- Each has a role description.

## Bonus — Shell UI automation helper
**Receipt:** `10-ui-automation.md`
**Contract:**
- `scripts/Invoke-ShellUiProbe.ps1` script exists and runs.
- Uses `UIAutomationClient` (name-based lookup, not pixel coords).
- Demonstrated on Overview tab: finds the NavigationViewItem by Name,
  invokes it, captures the resulting SelectedItem name.

---

## Master assembly
**File:** `../PROOF-OF-WORKING.md`
Concatenates receipts into a single audit-ready document.
Any feature without a receipt → "NOT VERIFIED — receipt missing" in
the master doc.
