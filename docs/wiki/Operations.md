# Master Control Program Operations

Build, test, deploy, and manage the platform.

## Build

The project uses CMake with preset-based configuration. Debug builds are the
standard development target.

```powershell
# Configure (first time or after CMakeLists.txt changes)
cmake --preset debug

# Build
cmake --build build\debug --config Debug
```

## Test

Tests run through CTest with verbose output on failure.

```powershell
ctest --test-dir build\debug -C Debug --output-on-failure
```

## Install

Install produces a distributable layout under `dist\debug\`.

```powershell
cmake --install build\debug --config Debug --prefix dist\debug
```

## Run the Bootstrapper

The bootstrapper handles environment detection, import flows, and initial setup.

```powershell
# Detect the current environment
dist\debug\MasterControlBootstrapper.exe detect
```

## Dashboard Access

| Surface | URL | Notes |
| --- | --- | --- |
| Browser dashboard | `http://192.168.1.3:8080/dashboard/` | Real-time metrics and server status |
| Gateway dashboard | `http://192.168.1.3:7200/dashboard` | Aggregator gateway status |
| Gateway health | `http://192.168.1.3:7200/health` | JSON health check |
| HTTPS gateway | `https://192.168.1.3:8443/mcp/gateway` | Remote MCP connection point |

## Service Management

### Sub-Agents

```powershell
# Start all 7 sub-agents
D:\Sub-Agents\Start-SubAgents.ps1

# Stop all 7 sub-agents
D:\Sub-Agents\Stop-SubAgents.ps1
```

### Dashboard Server

```powershell
# Start the Node.js static server on port 18000
D:\mcp\dashboard\serve.ps1
```

## Push Guard

The repository includes a pre-push hook that rejects commits declaring AI
contributors. This is enforced at two levels:

1. **Local hook**: Enable with `scripts\Enable-GitHooks.ps1`. The pre-push
   hook inspects staged commits and blocks any that list AI contributors.
2. **GitHub workflow**: The `AI Contributor Guard` workflow runs on `push` and
   `pull_request` events, mirroring the same rule server-side.

## Development Workflow

The standard local development loop:

1. Make changes in `src/` or `include/`.
2. Build: `cmake --build build\debug --config Debug`
3. Test: `ctest --test-dir build\debug -C Debug --output-on-failure`
4. Commit and push to `main`.
5. GitHub agents automatically update CHANGELOG, README, wiki, and version.

See also: [Automation](Automation) | [Remote Client](Remote-Client)
