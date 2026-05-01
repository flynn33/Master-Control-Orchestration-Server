# Validation and Reporting Rule

Every phase must end with a concise completion report.

## Required report format

```markdown
# Phase Completion Report — PHASE-XX

## Scope completed

## Files changed

## Public contracts changed

## Validation performed

| Command | Result | Notes |
|---|---|---|

## Risks and blockers

## Deferred work

## Ready for next phase?
```

## Evidence rules

- Do not claim a build passed unless it actually ran and passed.
- Do not claim runtime behavior unless it was tested or directly proven.
- Static source inspection must be labeled as static source inspection.
- If the environment prevents a Windows validation step, state that clearly and perform a static fallback.

