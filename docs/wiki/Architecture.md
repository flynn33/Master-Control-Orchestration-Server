# Master Control Orchestration Server Architecture

This page reflects the current repository-backed architecture rather than the retired external aggregator design.

### What It Is
Forsetti-compliant Windows orchestration server for MCP services, AI coding agents, provider routing, CLU governance, imports, exports, and operator control.
The product ships as a Windows service, a WinUI 3 desktop shell, and a browser-based admin surface backed by the same local runtime.

### Core Objective
- make setup fast through guided workflows instead of low-level manual editing
- host and govern MCP servers, providers, sub-agents, and platform governance lanes from one control plane
- provide a desktop-first and browser-accessible operations surface for telemetry, runtime control, and deployment visibility
- package the product so it can be installed, validated, upgraded, repaired, and uninstalled with repo-owned tooling

### Major Product Surfaces
1. Windows service host for orchestration, telemetry, configuration, imports, governance, and browser APIs
2. WinUI 3 shell for guided setup, CLU control, runtime operations, provider configuration, and security posture
3. Browser admin UI backed by the same service APIs and Forsetti surface model
4. Bootstrapper and setup launcher for install, validate, repair, upgrade, and uninstall flows

### Major Functional Areas
- Forsetti module architecture with manifest-driven composition
- CLU governance and policy enforcement
- provider routing for Codex, Claude Code, and xAI
- custom MCP server authoring and custom sub-agent authoring
- Windows, macOS, and iOS gateway/governance lanes
- Apple remote-host readiness, execution, signing, notarization, export, install, and history flows
- resource governance and controlled local process execution
- deployment acceptance, release packaging, and readiness reporting

### Current Build State
- feature-complete for the current build
- locally validated for Forsetti compliance, build health, and repo-native tests
- installer and setup flows validated on Windows 11
- Windows Server 2022 acceptance remains the main external validation gap

### Current Focus
- remove naming and packaging drift so product identity, docs, and release artifacts align
- keep guided setup as the primary operator path
- preserve deployment stability while polishing the end-user install and operations experience

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

### Purpose
Master Control Orchestration Server hosts platform-aware gateway and governance lanes inside the Forsetti framework so agents and operators can work through one orchestration surface while still targeting Windows, macOS, and iOS workflows correctly.

### Gateway Model
- gateway modules are Forsetti modules inside the product, not external sidecars
- clients discover the product through platform-specific gateway surfaces
- the current architecture keeps the gateway logic inside the local runtime and service APIs rather than a separate legacy aggregator deployment

### Governance Model
- CLU is the governance coordinator
- platform governance flows are routed by target platform instead of host OS alone
- governance tools execute through framework contracts instead of direct module-to-module shortcuts
- the UI reads governance state through the framework and does not become a second governance engine

### Current Platform Lanes
- Windows gateway and Windows governance lane
- macOS gateway and macOS governance lane
- iOS gateway and iOS governance lane

### Apple Execution Fabric
- Apple hosts can be selected per host using SSH or companion-service transport
- readiness includes Xcode, SDK, simulator, device control, signing, and notarization state
- operations include build, test, archive, export, install, sign, notarize, staple, replay, and history

### Deployment Direction
- Windows remains the primary hosted runtime for the orchestration server
- platform lanes remain module-driven and Forsetti-compliant
- deployment work is currently focused on installer polish, identity alignment, and target-host validation rather than new gateway topology changes

See also: [Infrastructure](Infrastructure) | [API Reference](API-Reference) | [Operations](Operations)
