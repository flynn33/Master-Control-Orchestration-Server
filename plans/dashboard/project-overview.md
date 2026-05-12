## Master Control Orchestration Server - Project Overview

### What It Is
Forsetti-compliant Windows orchestration server for MCP services, LAN AI-client onboarding, CLU governance, imports, exports, and operator control.
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
- LAN AI client onboarding (Claude Code, Codex, ChatGPT, Grok, and generic MCP clients) — MCOS publishes a per-client onboarding profile at `/api/onboarding/{clientType}` carrying the gateway URL + governance bundle URL; clients import it and connect via the LAN MCP gateway.
- custom MCP server authoring and custom sub-agent authoring
- Windows, macOS, and iOS gateway/governance lanes
- Apple remote-host readiness, execution, signing, notarization, export, install, and history flows
- resource governance and controlled local process execution
- deployment acceptance, release packaging, and readiness reporting

### Command Logic Unit Module
- the Command Logic Unit, or CLU, is a first-class Forsetti service module rather than a UI-only concept
- module manifest: `src/MasterControlModules/Resources/ForsettiManifests/CommandLogicUnitModule.json`
- module ID: `com.mastercontrol.command-logic-unit`
- CLU is responsible for governance posture, rule evaluation, governance bundle distribution, and platform-specific governance execution
- CLU is also the operator-facing coordination lane for guided setup, Apple governance operations, and model-to-role assignment
- the WinUI shell and browser surface both read CLU state from the runtime instead of reimplementing governance logic on their own

### Current Build State
- feature-complete for the current build
- locally validated for Forsetti compliance, build health, and repo-native tests
- installer and setup flows validated on Windows 11
- Windows Server 2022 acceptance remains the main external validation gap

### Current Focus
- remove naming and packaging drift so product identity, docs, and release artifacts align
- keep guided setup as the primary operator path
- preserve deployment stability while polishing the end-user install and operations experience
