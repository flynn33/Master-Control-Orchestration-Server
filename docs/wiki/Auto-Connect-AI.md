# Master Control Orchestration Server — Auto-Connect AI

![status](https://img.shields.io/badge/status-production-00f6ff?style=flat-square) ![stages](https://img.shields.io/badge/stages-7-00aacc?style=flat-square) ![encryption](https://img.shields.io/badge/encryption-DPAPI-5a00e8?style=flat-square)

**Goal:** the operator types credentials, picks roles, and clicks Connect. 
Every other step — capability lookup, ID generation, model discovery, encryption, 
registration, role fan-out — happens automatically inside the runtime.

---

## End-to-end pipeline

```mermaid
flowchart LR
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;
    classDef ok fill:#031018,stroke:#1CF2C1,color:#E6FCFF;
    classDef warn fill:#031018,stroke:#FFC857,color:#E6FCFF;

    UI[Auto-Connect card]:::accent --> POST[POST /api/providers/auto-connect]:::accent
    POST --> S1[1. Resolve capability]:::ok
    S1 --> S2[2. Generate provider id]:::ok
    S2 --> S3[3. Probe remote endpoint]:::ok
    S3 --> S4[4. Discover models]:::ok
    S4 --> S5[5. Persist credentials DPAPI]:::ok
    S5 --> S6[6. Register provider]:::ok
    S6 --> S7[7. Apply role assignments]:::ok
    S7 --> Result[(AutoConnectResult)]:::accent
```

Each stage is timed individually and reported back in the response so the UI 
can render a transparent progress log. If any stage fails, the pipeline aborts 
and returns the partial step list with the failing stage flagged.

---

## Stage detail

### 1. Resolve capability
Looks up the matching `ProviderCapabilityDescriptor` for the supplied `kind`. 
Capabilities declare credential field names, the recommended model, and the base URL.

### 2. Generate provider id
Builds a stable, unique provider ID by combining the kind with a short hash of the credential payload.

### 3. Probe remote endpoint
Issues a real HTTP request through WinHTTP using the provided credentials. 
401/403 are surfaced as friendly errors; 200 advances the pipeline.

### 4. Discover models
Calls the provider's models endpoint (e.g. `GET /v1/models` for OpenAI-compatible APIs) 
and prefers `capability.recommendedModel`. The bearer auth helper is module-agnostic — 
any credential field whose ID matches `api_key` / `apikey` / `token` / `secret` is used.

### 5. Persist credentials (DPAPI)
Encrypts the credential bundle with `CryptProtectData` and writes it to the credential store. 
Only the current Windows user account can decrypt it; the credential never leaves the host in plaintext.

### 6. Register provider
Inserts the provider into the runtime registry, exposing it through `/api/providers` and `/api/dashboard`.

### 7. Apply role assignments
If the request includes `assignmentTargetIds`, the provider is bound to those roles via 
`IProviderAssignmentService::upsertAssignment` so CLU can route work to it immediately.

---

## Calling it directly

```bash
curl -X POST http://127.0.0.1:7300/api/providers/auto-connect \
  -H "Content-Type: application/json" \
  -d '{
    "kind": "openai",
    "credentials": { "api_key": "sk-..." },
    "assignmentTargetIds": ["planner", "coder"],
    "discoverModels": true
  }'
```

Successful response:

```json
{
  "succeeded": true,
  "providerId": "openai-7f3a",
  "summary": "Auto-Connect completed in 184 ms",
  "totalLatencyMs": 184,
  "steps": [ ... ],
  "discoveredModels": [ { "id": "gpt-4o", "selected": true } ]
}
```

Failure (bad credentials):

```json
{
  "succeeded": false,
  "providerId": "",
  "summary": "Probe failed: HTTP 401 from https://api.openai.com/v1/models",
  "errorMessage": "HTTP 401 unauthorized",
  "steps": [
    { "name": "Resolve capability", "ok": true },
    { "name": "Generate provider id", "ok": true },
    { "name": "Probe remote endpoint", "ok": false, "errorMessage": "HTTP 401" }
  ]
}
```

---

## Where it lives in the source

| Concern | File |
| --- | --- |
| Models / contracts | `include/MasterControl/MasterControlModels.h` |
| Service interface | `include/MasterControl/MasterControlContracts.h` (`IProviderRegistry::autoConnectProvider`) |
| Implementation | `src/MasterControlApp/MasterControlRuntime.cpp` (`ProviderRegistryService::autoConnectProvider`) |
| HTTP route | `src/MasterControlApp/MasterControlRuntime.cpp` (`/api/providers/auto-connect`) |
| Shell UI | `src/MasterControlShell/ProvidersSectionControl.xaml{,.cpp,.h}` |

---

See also: [API Reference](API-Reference) · [CLU Governance](CLU-Governance) · 
[Architecture](Architecture)
