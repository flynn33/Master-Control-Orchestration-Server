---
description: Import a Forsetti module manifest into MCOS — registers entry points and surfaces module on the runtime.
argument-hint: <path-to-manifest.json>
---

Import a Forsetti module into the running MCOS.

**Important**: This imports a manifest that tells MCOS to register a module's entry points. It does NOT modify the vendored Forsetti framework code (`Forsetti-Framework-Windows-main/`) — that is sealed by ADR-002 §11 / FORBIDDEN-CONTRACT §5.1. Importing here means: the module's C++ implementation must already exist in the runtime (registered via `registerModule(name, factory)` in `MasterControlModules.cpp`); the manifest you import is the metadata that activates it.

Steps:

1. If `$1` is empty, list currently registered modules: `mcos_forsetti_modules`. Then ask the operator for the manifest path.
2. Read the manifest from disk (use the `Read` tool). Validate it's well-formed JSON and has at minimum:
   - `moduleID` or `moduleId`
   - `displayName`
   - `entryPoint` (must match a name registered in `MasterControlModules.cpp`)
   - `version`
   - `supportedPlatforms`
3. **Pre-check**: confirm the `entryPoint` value is registered in MCOS by reading `src/MasterControlModules/MasterControlModules.cpp`. If the entry point is not registered, refuse to import — the manifest will load but `ModuleManager::makeModule` will throw `EntryPointNotFound` at boot.
4. State to the operator:
   ```
   Importing manifest:
     moduleID: <id>
     entryPoint: <name>      [registered in MasterControlModules.cpp: yes|no]
     version: <v>
     platforms: <list>
   ```
5. Wait for confirmation.
6. `mcos_forsetti_module_import manifest=<parsed JSON>`.
7. `mcos_forsetti_modules` — verify the module appears in the registered list.
8. To activate: `mcos_forsetti_module_enable moduleId=<id>`.
9. `mcos_dashboard` — confirm module count incremented and posture is still pass/warn (not blocked).

If the import fails because the entry point is unregistered, surface the operator path: edit `src/MasterControlModules/MasterControlModules.cpp` to add `registry.registerModule("<entryPoint>", ...)`, rebuild, restart the service, then re-import. That's a code-level change, not a runtime change.
