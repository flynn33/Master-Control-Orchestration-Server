# Feature Audit — README Requirements

Per the README's "Highlights" table, this software is supposed to provide 7 major
feature groups. This document walks each one, verifies the code implements it,
runs a live probe / test, and records the result honestly (bug found / fixed /
verified / still broken). No feature is marked "done" unless it has a concrete,
repeatable runtime proof attached.

Status key: `[ ]` unverified · `[~]` bug found (fix in progress) · `[x]` verified
end-to-end on the running machine · `[!]` broken, cannot complete in scope.

---

## 1. Multi-binary control plane

**Claim:** "Windows service host, WinUI 3 desktop shell, and browser admin UI — all
backed by the same in-process runtime"

Concrete subclaims I have to verify:
1. `MasterControlServiceHost.exe` runs as a Windows service *and* a console
   fallback.
2. `MasterControlShell.exe` is a WinUI 3 desktop application that talks to the
   service host over HTTP.
3. `resources/web/` is served by the service host at a reachable HTTP port and
   renders a usable admin dashboard.
4. `MasterControlApp` is the shared static library linked into all three binaries
   so the same request handlers serve both shell and browser.

Probe / test plan:
- `Get-Process` shows the service host + shell simultaneously.
- `curl http://localhost:7300/api/health` returns 200.
- `curl http://localhost:7300/` serves the browser admin `index.html`.
- Both surfaces (shell + browser) show the same provider list after seeding one.

---

## 2. Auto-Connect AI providers

**Claim:** "Enter credentials, pick roles, runtime handles capability resolution,
model discovery, DPAPI encryption, and assignment fan-out"

Concrete subclaims:
1. Providers section has an **Auto-Connect** form (provider picker, credential
   fields, role list, Auto-Connect button).
2. `POST /api/providers/auto-connect` accepts kind + credentials + role list and
   returns success after probing the vendor endpoint + registering.
3. Credentials are encrypted at rest under DPAPI before landing on disk.
4. Role assignments fan out atomically — a single Auto-Connect call registers
   the provider AND assigns it to every role in the list.

Probe / test plan:
- UIA dump on AI Integrations finds `QuickConnectProviderSelector`,
  `ConnectQuickProviderButton`, `AutoConnectRoleSelector`.
- Live `POST /api/providers/auto-connect` returns `succeeded=true` with a bogus
  test key against a mock/local endpoint (or at minimum doesn't 500 / crash).
- Read DPAPI credential file under `%PUBLIC%\Documents\...\credentials\` and
  verify it's not plain-text.

---

## 3. Live command stream

**Claim:** "Every admin API request is captured by a 512-event ring buffer with
millisecond timestamps, methods, targets, status codes, and latency"

Concrete subclaims:
1. Ring buffer in runtime caps at 512 entries (pops oldest when full).
2. `GET /api/activity` returns a cursor-delimited feed with timestamp, method,
   path, status, latency-ms per entry.
3. Browser + shell both render the live stream.

Probe / test plan:
- Hit `/api/dashboard` 600 times, then `/api/activity?limit=520` returns ≤ 512.
- Entries include `timestamp`, `method`, `path`, `status`, `latencyMs`.
- `ActivityStreamListView` in UIA dump contains rows matching recent API calls.

---

## 4. CLU governance

**Claim:** "First-class Forsetti service module for posture, rules, role routing,
Apple operations, and platform governance execution"

Concrete subclaims:
1. A `CluGovernanceModule` module manifest exists and is loaded.
2. `GET /api/governance/*` endpoints return posture + rules + role routing.
3. Shell has a CLU section that surfaces the governance profile.

Probe / test plan:
- `/api/governance` returns 200.
- CLU tab in shell renders governance data.
- Governance profile JSON at `resources/clu/governance-profile.json` is loaded
  and reachable.

---

## 5. Cross-platform gateways

**Claim:** "Windows, macOS, and iOS gateway + governance lanes — Apple lanes route
through SSH/companion-service Apple hosts"

Concrete subclaims:
1. Gateway modules for Windows + macOS + iOS are registered and enabled.
2. Apple host registry exists with add / remove / replay operations.
3. SSH / companion-service execution path is callable.

Probe / test plan:
- `/api/platform-gateways` returns Windows, macOS, iOS entries.
- `/api/platform-services/apple-hosts` round-trips (add + list + remove).
- Shell Runtime section shows the gateway lanes.

---

## 6. Repo-owned installer

**Claim:** "Native setup launcher with Tron progress UI, plus diagnostic
PowerShell fallback, plus bootstrapper for preflight/install/validate/upgrade/
repair/uninstall"

Concrete subclaims:
1. `MasterControlBootstrapper.exe` supports every listed action.
2. MSI (from v0.4.3) supports interactive install with install-dir + shortcut
   + service/firewall options.
3. Uninstall cleanly removes the product.

Probe / test plan:
- `.\MasterControlBootstrapper.exe preflight <dir> --json` returns valid JSON.
- `.\MasterControlBootstrapper.exe validate <dir> --json` returns valid JSON.
- `.\MasterControlBootstrapper.exe upgrade <dir> --json` is implemented.
- `msiexec /i ....msi` shows the wizard (proven in v0.4.5).

---

## 7. Tron aesthetic, end-to-end

**Claim:** "Cyan-on-blue-black palette, Bahnschrift SemiCondensed type, zero
corner radii, accent pulse animations, focus-visible outlines,
prefers-reduced-motion respected"

Concrete subclaims:
1. Shell uses cyan + blue-black.
2. Fonts are Bahnschrift SemiCondensed.
3. Corner radii are 0.
4. Focus outlines are visible on Tab navigation.
5. `prefers-reduced-motion` actually disables animations (not just declared).

Probe / test plan:
- App.xaml styles grep for `Bahnschrift`, corner radius `0`, accent colors.
- Shell runtime check reads `UISettings().AnimationsEnabled()` and gates
  animations.
- Browser CSS honours `@media (prefers-reduced-motion: reduce)`.
