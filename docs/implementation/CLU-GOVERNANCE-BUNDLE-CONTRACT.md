# CLU Governance Bundle Contract

## Bundle paths

- `/api/governance/bundles/windows`
- `/api/governance/bundles/macos`
- `/api/governance/bundles/ios`
- `/api/governance/profile`
- `/api/governance/decisions`

## Bundle content

Each platform bundle must include:

- `platform`
- `forsettiFrameworkVersion`
- `agenticCodingFrameworkVersion`
- `cluSchemaVersion`
- `instructionsMarkdown`
- `rulesJson`
- `decisionPolicy`
- `checksum`
- `generatedAt`

## Client instruction

When a client connects, MCOS provides the governance bundle URL in the onboarding profile. The client is instructed to use CLU/Forsetti guidance before mutating code, generating workflows, modifying MCP server definitions, or changing sub-agent behavior.

## Server compliance

MCOS itself must comply with the Windows Forsetti framework because it runs on Windows 11 Pro/Server.

