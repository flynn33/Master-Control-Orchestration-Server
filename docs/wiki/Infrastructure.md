# Master Control Orchestration Server — Infrastructure

![targets](https://img.shields.io/badge/targets-Win11%20/%20Server%202022-00f6ff?style=flat-square) ![install size](https://img.shields.io/badge/install%20size-~44%20MB-00aacc?style=flat-square) ![scope](https://img.shields.io/badge/scope-single%20host-031018?style=flat-square)

The product is **single-host** by design. There are no cluster components, no remote 
control plane, no cloud dependencies. One Windows machine runs the service, the shell, 
and the browser admin UI; remote operator access is via the loopback admin API tunneled 
through whatever transport the operator already trusts (RDP, SSH port forward, etc.).

---

## Target hosts

| Host | Status |
| --- | --- |
| Windows 11 (22H2+) | ✅ supported |
| Windows Server 2022 Datacenter (Desktop Experience) | ✅ supported, end-to-end validated |
| Windows Server Core | ❌ unsupported (XAML Islands required) |
| Windows 10 | ⚠ untested; may work with Windows App SDK 1.5 prerequisites |

---

## Deployment shape

```mermaid
flowchart LR
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;

    subgraph Host[Windows host]
        Service[Service host<br/>:7300 loopback]:::accent
        Shell[WinUI shell]:::accent
        Browser[Browser admin UI]:::accent
        Data[(ProgramData)]:::accent
    end

    Operator((Operator)) --> Shell
    Operator --> Browser
    Shell --> Service
    Browser --> Service
    Service --> Data
```

---

## Packaging model

| Layer | Contents |
| --- | --- |
| Setup launcher | Tron-themed UI, elevation, payload extraction, bootstrapper invocation |
| Bootstrapper | Lifecycle engine: preflight, install, validate, upgrade, repair, uninstall |
| Service host | The orchestration runtime, registered as a Windows service |
| Shell | WinUI 3 desktop UI, runs in the operator session |
| Browser assets | Static HTML/CSS/JS served by the runtime |
| Forsetti manifests | Module catalog under `share/MasterControlOrchestrationServer/ForsettiManifests/` |
| CLU profile | Governance defaults under `share/MasterControlOrchestrationServer/clu/` |

---

## Validation focus

Current external validation gap is automated upgrade-from-legacy on Server Core. 
All other lifecycle flows are exercised by the deployment acceptance harness.

---

See also: [Operations](Operations) · [Architecture](Architecture) · 
[Remote Client](Remote-Client)
