# Master Control Orchestration Server — Versions

![scheme](https://img.shields.io/badge/scheme-semver-00f6ff?style=flat-square) ![strategy](https://img.shields.io/badge/strategy-patch%20on%20main-00aacc?style=flat-square)

Semantic versioning, automated bumps on every push to `main`. The version agent 
analyzes commit history to decide between patch / minor / major.

---

## Current release

| Field | Value |
| --- | --- |
| **Version** | `v0.2.0` |
| **Released** | `2026-04-11` |
| **Summary** | Tron-density UX rework, validated end-to-end on Windows Server 2022. |

### Highlights

- Tron-theme the setup launcher progress window (cyan accent bar, Bahnschrift fonts, accent marquee) to match the shell's App.xaml palette.
- Expand shell resource dictionary with status chip, tonal button variants, compact tiles, sub-agent badge, and live-clock text styles.
- Redesign OverviewSectionControl around hero card + operational snapshot + narrative grid + authored-surfaces legend.
- Add Tron command-center density to MainWindow: live clock (HH:MM:SS) in the title bar, sub-agent footer row (SENTINEL/ARCHITECT/FORGE/SCRIBE/RECON/NEXUS/WATCHDOG), and a ScrollViewer wrapping the main content so the full layout reaches low-resolution displays.
- Add browser dashboard polish layer: prefers-reduced-motion, focus-visible outlines, accent pulse animation, <dialog>::backdrop blur.
- Add one-shot ProgramData migration from legacy MasterControlProgram to MasterControlOrchestrationServer path, with safe fallback if the rename cannot complete.
- Update GitHub repository URL references to flynn33/Master-Control-Orchestration-Server.
- Guard Package-MasterControlOrchestrationServer.ps1 git calls so packaging works outside a git repo.
- Update PlatformToolset from v143 to v145 so the shell builds on Visual Studio 2026.
- Validated end-to-end on Windows Server 2022 Datacenter (21H2, build 20348): cmake configure + build (0 errors, 0 warnings), ctest 4/4 green, package staged (44 MB), unattended install smoke CLEAN, shell launches and renders Tron UI with live clock, bootstrapper preflight/validate reports valid:true.

---

## Versioning scheme

| Bump | When | Examples |
| --- | --- | --- |
| **Patch** `0.1.x` | Bug fixes, doc updates, metadata | `0.2.0 → 0.2.1` |
| **Minor** `0.x.0` | New features, new modules, capabilities | `0.2.5 → 0.3.0` |
| **Major** `x.0.0` | Breaking changes | `0.9.0 → 1.0.0` |

Versions are tracked in `VERSION.json` and tagged as GitHub Releases.

---

## Release artifacts

- [Version Index](../versions/index.md) — full list of releases
- [Latest Release Notes](../versions/latest.md) — notes for the current release

---

## Recent releases

| Version | Date | Summary |
| --- | --- | --- |
| `v0.2.0` | `2026-04-11` | Tron-density UX rework, validated end-to-end on Windows Server 2022. |
| `v0.1.66` | `2026-04-10` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.65` | `2026-04-10` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.64` | `2026-04-10` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.63` | `2026-04-10` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.62` | `2026-04-10` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.61` | `2026-04-10` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.60` | `2026-04-10` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.59` | `2026-04-03` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.58` | `2026-04-03` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.57` | `2026-04-03` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.56` | `2026-04-03` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.55` | `2026-04-03` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.54` | `2026-04-03` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.53` | `2026-04-02` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.52` | `2026-04-02` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.51` | `2026-04-02` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.50` | `2026-04-02` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.49` | `2026-03-29` | Automated patch release for Master Control Orchestration Server. |
| `v0.1.48` | `2026-03-29` | Automated patch release for Master Control Orchestration Server. |

See also: [Automation](Automation) · [Operations](Operations)
