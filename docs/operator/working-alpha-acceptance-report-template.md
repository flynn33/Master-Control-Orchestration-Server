# MCOS Working Alpha Acceptance Report

> Fillable operator form. Copy this file per validation run and record results
> from the Gate D (local runtime) and Gate E (second-host LAN) acceptance
> scripts. See `working-alpha-acceptance.md` for the commands.

## Environment

- Date/time:
- MCOS host:
- OS:
- Operator:
- Elevated:
- Network profile:
- Install path:
- Data path:

## Local Installed Runtime Gate (Gate D)

| Check | Result | Evidence |
|---|---:|---|
| Service registered | pending |  |
| Service running | pending |  |
| Service recovery configured | pending |  |
| Install state valid | pending |  |
| Admin listener reachable | pending |  |
| `/api/health` | pending |  |
| `/api/version` | pending |  |
| `/api/health/summary` | pending |  |
| `/.well-known/mcos.json` | pending |  |
| `/api/discovery` | pending |  |
| `/api/gateway/status` | pending |  |
| `/api/gateway/health` | pending |  |
| MCP initialize | pending |  |
| MCP ping | pending |  |
| MCP tools/list | pending |  |
| URL ACL state | pending |  |
| Firewall state | pending |  |
| TLS state, if enabled | pending |  |
| Diagnostics bundle | pending |  |

## Second-Host LAN Client Gate (Gate E)

| Check | Result | Evidence |
|---|---:|---|
| Well-known discovery reachable | pending |  |
| Advertised URLs routable | pending |  |
| Discovery document valid | pending |  |
| Registered client bundle consumed | pending |  |
| Telemetry heartbeat posted | pending |  |
| Authenticated client heartbeat posted | pending |  |
| MCP initialize from second host | pending |  |
| MCP ping from second host | pending |  |
| MCP tools/list from second host | pending |  |
| Client liveness observed | pending |  |
| Evidence bundle produced | pending |  |

## Blocking Issues

- None recorded yet.

## Pending Operator-Gated Actions

- Final release build/package/deployment if not authorized.
- Managed install validation if not authorized.
- Second-host LAN validation if not authorized.

## Final Result

- Working alpha certified: pending
