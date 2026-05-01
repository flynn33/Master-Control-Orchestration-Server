---
phase: PHASE-05
label: CLU/Forsetti governance bundle distribution
objective: Deliver platform-specific governance bundles and CLU decision behavior to clients.
---


# PHASE-05 — CLU/Forsetti Governance Bundle Distribution

## Goal

When a client connects, MCOS provides platform-specific governance files and CLU instructions.

## Required changes

- Implement governance bundle model.
- Serve Windows, macOS, and iOS bundles.
- Include Forsetti Framework for Agentic Coding guidance.
- Include checksum/version fields.
- Complete or clarify governance decision endpoint behavior.
- Update compliance scripts for gateway-first architecture.

## Exit criteria

- Onboarding response links to governance bundle.
- Bundle schema tests pass.
- Forsetti compliance check is updated and run or statically validated.

## Read first

- `resources/clu/governance-profile.json`
- `Forsetti-Framework-Windows-main`
- `scripts/check-mastercontrol-forsetti.ps1`
- `src/MasterControlModules`

## Deliverables

- Governance bundle endpoints
- Windows/macOS/iOS bundle records
- Checksums
- Client instructions
- Updated compliance gate

## Acceptance criteria

- Windows governance bundle works
- macOS/iOS paths represented
- CLU profile served during onboarding
- Forsetti compliance script passes or reports exact blockers

## Validation

- `Forsetti compliance script`
- `Bundle schema tests`
- `Checksum validation tests`

