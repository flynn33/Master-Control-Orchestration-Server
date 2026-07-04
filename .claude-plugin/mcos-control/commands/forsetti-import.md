---
description: Validate a Forsetti module manifest and activate the module in MCOS — via manifest deployment plus the module state route.
argument-hint: <path-to-manifest.json>
---

Validate and activate a Forsetti module in the running MCOS.

**Important**: MCOS has no runtime "import manifest" API — the admin surface deliberately exposes only `GET /api/forsetti/modules` (catalog) and `POST /api/forsetti/modules/state` (enable/disable/remove), because module registration is a code + on-disk-manifest concern, not a wire concern. This command therefore VALIDATES the manifest, helps deploy it, and activates the module through the state route. It does NOT modify the vendored Forsetti framework code (`Forsetti-Framework-Windows-main/`) — that is sealed by ADR-002 §11 / FORBIDDEN-CONTRACT §5.1. The module's C++ implementation must already exist in the runtime (registered via `registerModule(name, factory)` in `MasterControlModules.cpp`); the manifest is the metadata that activates it.

Steps:

1. If `$1` is empty, list currently registered modules: `mcos_forsetti_modules`. Then ask the operator for the manifest path.
2. Read the manifest from disk (use the `Read` tool). Validate it's well-formed JSON and has at minimum:
   - `moduleID` or `moduleId`
   - `displayName`
   - `entryPoint` (must match a name registered in `MasterControlModules.cpp`)
   - `version`
   - `supportedPlatforms`
3. **Pre-check**: confirm the `entryPoint` value is registered in MCOS by reading `src/MasterControlModules/MasterControlModules.cpp`. If the entry point is not registered, refuse to continue — the manifest will load but `ModuleManager::makeModule` will throw `EntryPointNotFound` at boot.
4. State to the operator:
   ```
   Activating module:
     moduleID: <id>
     entryPoint: <name>      [registered in MasterControlModules.cpp: yes|no]
     version: <v>
     platforms: <list>
   ```
5. Wait for confirmation.
6. Confirm the manifest file is deployed where the runtime discovers modules (the module manifest directory shipped with the install; see the Forsetti surface in `docs/wiki/`). If it is not, help the operator copy it there and restart the service so discovery picks it up.
7. `mcos_forsetti_modules` — verify the module appears in the catalog.
8. Activate: `mcos_forsetti_module_enable moduleId=<id>` (drives `POST /api/forsetti/modules/state` with `action=enable`).
9. `mcos_dashboard` — confirm module count incremented and posture is still pass/warn (not blocked).

If the module never appears in the catalog, the entry point is unregistered or the manifest is not in the discovery directory. The code-level path: edit `src/MasterControlModules/MasterControlModules.cpp` to add `registry.registerModule("<entryPoint>", ...)`, rebuild, restart the service, then re-run this command.
