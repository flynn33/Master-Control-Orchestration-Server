## Deployment Overview

### Product Shape
Master Control Orchestration Server ships as:
- a Windows service host
- a WinUI 3 desktop shell
- a browser-based operator surface
- a native setup launcher plus bootstrapper-driven install lifecycle

### Runtime Lanes
- Platform gateways for Windows, macOS, and iOS
- Platform governance MCP server lanes for Windows, macOS, and iOS
- Shared runtime inventory for providers, MCP servers, sub-agents, and telemetry
- CLU governance and resource enforcement in the core runtime

### Installer And Package Shape
- Native entry point: `MasterControlOrchestrationServerSetup.exe`
- Diagnostic fallback: `Install-MasterControlOrchestrationServer.ps1`
- Core lifecycle: `preflight`, `install`, `validate`, `upgrade`, `repair`, `uninstall`
- Release package includes VC++ runtime dependencies, readiness metadata, and deployment acceptance artifacts

### Deployment Targets
- Windows 11 workstation installs
- Windows Server 2022 with Desktop Experience
- Mixed non-admin test installs for validation
- Managed admin installs for service registration, firewall rules, and uninstall registration

### Validation Focus
- service registration and running state
- shell/browser launch behavior
- package integrity and readiness metadata
- upgrade, repair, rollback, and uninstall behavior
