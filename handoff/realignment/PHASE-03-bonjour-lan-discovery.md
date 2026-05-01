---
phase: PHASE-03
label: Bonjour-style LAN discovery and beacon correction
objective: Advertise MCOS gateway via DNS-SD/mDNS and normalized discovery JSON.
---


# PHASE-03 — Bonjour-Style LAN Discovery and Beacon Correction

## Goal

MCOS advertises the gateway and onboarding service using DNS-SD/mDNS and an optional UDP beacon.

## Required changes

- Register `_mcos._tcp.local`.
- Register `_mcos-mcp._tcp.local`.
- Register `_mcos-onboarding._tcp.local`.
- Add `/.well-known/mcos.json`.
- Add `/api/discovery`.
- Normalize UDP beacon payload to match gateway-first discovery.

## Required DNS-SD TXT fields

```text
product=MCOS
role=mcp-gateway
gateway=mcpjungle
mcp_path=/mcp
config_path=/api/onboarding
governance_path=/api/governance/bundles
protovers=2025-03-26
auth=none
trust=lan
clu=true
forsetti=true
```

## Exit criteria

- Discovery docs describe how a companion utility finds MCOS and writes local MCP config.
- DNS-SD metadata matches schema.
- Beacon no longer implies native auto-binding by major clients.

## Read first

- `src/MasterControlApp/MasterControlRuntime.cpp`
- `include/MasterControl`
- `docs/wiki/Remote-Client.md`
- `resources/web/app.js`

## Deliverables

- DNS-SD service registration
- /.well-known/mcos.json
- /api/discovery
- UDP beacon update
- Discovery tests

## Acceptance criteria

- DNS-SD TXT fields include gateway/config/governance paths
- Discovery JSON matches schema
- Beacon is gateway-first not provider-first

## Validation

- `Serialization tests`
- `Static API route tests`
- `Windows DNS-SD code review`

