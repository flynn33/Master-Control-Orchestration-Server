# Forsetti and CLU Governance Rule

## Forsetti compliance

- Do not modify vendored Forsetti framework code.
- Keep MCOS modules Core-facing and preserve module manifest boundaries.
- Update compliance scripts when architecture changes invalidate old assumptions.
- Treat Windows-host compliance as mandatory for every phase.

## CLU role

CLU is the governance distributor and decision module.

CLU must provide:

- Windows, macOS, and iOS Forsetti governance bundles.
- Forsetti Framework for Agentic Coding instructions.
- Machine-readable governance profile metadata.
- Human-readable governance instructions.
- Checksums/version identifiers for delivered bundles.
- Decision endpoint behavior for governed changes.

CLU must not be used as app-layer authentication for AI clients.

