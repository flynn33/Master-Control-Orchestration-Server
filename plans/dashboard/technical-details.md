## Master Control Orchestration Server - Technical Details

### Runtime Composition
- `src/MasterControlApp` hosts the shared application runtime, configuration, governance, Apple execution, and browser APIs
- `src/MasterControlModules` provides Forsetti modules and manifests
- `src/MasterControlServiceHost` runs the orchestration runtime as a Windows service or console host
- `src/MasterControlShell` provides the WinUI 3 operator shell
- `src/MasterControlBootstrapper` owns install, preflight, validate, repair, upgrade, and uninstall flows

### Deployment Layout
- install root contains the service host, shell, bootstrapper, setup launcher, and shared payload assets
- staged Forsetti manifests are installed under `share/MasterControlOrchestrationServer/ForsettiManifests`
- staged browser assets are installed under `share/MasterControlOrchestrationServer/web`
- CLU governance resources are installed under `share/MasterControlOrchestrationServer/clu`

### Runtime Data
- persistent state lives under ProgramData unless overridden by environment configuration
- configuration, install history, entitlements, provider credentials, and Apple operation history are repo-defined runtime artifacts
- runtime exports and work directories are created under the resolved data root

### Control Surfaces
- WinUI 3 shell hosts guided setup, CLU, telemetry, runtime, provider, import, export, security, and settings views
- browser UI mirrors the same runtime-backed control plane concepts through the service API
- modules publish capabilities through Forsetti services instead of directly owning UI shells

### Governance And Platform Lanes
- CLU routes governance through platform-specific lanes
- Windows governance executes local Forsetti and architecture validation
- macOS and iOS governance route through Apple remote hosts using SSH or companion-service transport
- Apple operations support readiness, build, test, archive, export, install, sign, notarize, staple, replay, and persisted history

### Installer And Packaging
- `Package-MasterControlOrchestrationServer.ps1` builds staged release packages, bundles CRT dependencies, writes metadata, and emits install instructions
- `MasterControlOrchestrationServerSetup.exe` is the standard interactive entry point
- `Install-MasterControlOrchestrationServer.ps1` is the diagnostic fallback entry point with desktop logging
- deployment harness scripts validate mixed and managed install lifecycles and write acceptance bundles

### Validation Baseline
- Forsetti compliance is enforced by `scripts/check-mastercontrol-forsetti.ps1`
- repo-native validation uses local Debug builds plus `ctest`
- packaged deployment acceptance exists for install, validate, upgrade, repair, and uninstall
- current external validation gap is Windows Server 2022 acceptance
