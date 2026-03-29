# Master Control Program Remote Client Setup

How to connect a remote Claude Code Desktop instance to the BLADE MCP Gateway.

## Overview

Remote Claude Code instances on the LAN need to reach the aggregator gateway
at `https://192.168.1.3:8443/mcp/gateway`. Because the gateway uses a
self-signed certificate, the client machine needs the certificate installed
and the MCP server entry added to `~/.claude.json`.

A self-contained PowerShell installer script handles everything in one step.

## Installation

Run `Install-BladeGatewayPlugin.ps1` on the remote Windows machine:

```powershell
# From any of these sources:
# Right-click -> Run with PowerShell
```

### What the Installer Does

| Step | Action |
| --- | --- |
| 1 | Checks LAN connectivity to `192.168.1.3:8080` |
| 2 | Downloads and installs the self-signed cert to `CurrentUser\Root` store |
| 3 | Saves the PEM cert to `~/.blade-mcp/master-control.pem` |
| 4 | Sets `NODE_EXTRA_CA_CERTS` environment variable (for Node.js TLS in Claude Code) |
| 5 | Adds `master-control-gateway` entry to `~/.claude.json` mcpServers |
| 6 | Verifies HTTPS connectivity to `https://192.168.1.3:8443/health` |

After installation, restart Claude Code Desktop to pick up the new MCP server.

## Distribution Methods

| Method | Path |
| --- | --- |
| Desktop shortcut | `C:\Users\Master-Control\Desktop\Install-BladeGatewayPlugin.ps1` |
| NAS share | `\\192.168.1.3\nas\Install-BladeGatewayPlugin.ps1` |
| HTTP download | `http://192.168.1.3:8080/Install-BladeGatewayPlugin.ps1` |
| Source | `D:\mcp\plugin\Install-BladeGatewayPlugin.ps1` |

## Technical Details

### Certificate Handling

The installer uses dual certificate trust to cover both PowerShell and Node.js:

- **OS certificate store** (`CurrentUser\Root`): Covers PowerShell, .NET, and
  system-level HTTPS calls.
- **NODE_EXTRA_CA_CERTS**: Points Node.js (which Claude Code uses internally)
  to the PEM file at `~/.blade-mcp/master-control.pem`.

Stale certificate removal is wrapped in `try/catch` because admin-installed
certs may return access-denied errors on removal.

### Claude Code Configuration

The installer adds this entry to `~/.claude.json`:

```json
{
  "mcpServers": {
    "master-control-gateway": {
      "type": "http",
      "url": "https://192.168.1.3:8443/mcp/gateway"
    }
  }
}
```

This is a direct JSON edit, not a plugin installation. Claude Code Desktop
uses a marketplace system for plugins, not directory scanning. The installer
edits `~/.claude.json` directly.

### Compatibility

- PowerShell 5.1 compatible (the default `Run with PowerShell` handler).
- Does not use `-SkipCertificateCheck` (PS 5.1 lacks it). Uses
  `ServicePointManager` fallback for certificate bypass during initial download.
- Works with right-click -> Run with PowerShell on the `.ps1` file.

## Troubleshooting

| Issue | Solution |
| --- | --- |
| `Connection refused` on install | Verify MASTER-CONTROL server is running and reachable at `192.168.1.3:8080` |
| Claude Code doesn't see the gateway | Restart Claude Code Desktop after installation |
| Certificate errors after reinstall | Delete `~/.blade-mcp/` and run the installer again |
| UNC path issues in bash | Use the HTTP download method instead of the NAS share |

### What Did NOT Work (Lessons Learned)

These approaches were attempted and abandoned:

1. **Plugin directory** at `~/.claude/plugins/blade-mcp-gateway/` — Claude Code
   Desktop does not scan for arbitrary plugin folders. It uses a git-based
   marketplace system.
2. **installed_plugins.json** — Managed by the marketplace, not user-editable.
3. **Zip file extraction** — Too many manual steps for reliable LAN deployment.
4. **UNC path copying via bash** — Backslashes get eaten by bash interpretation.

See also: [Infrastructure](Infrastructure) | [Architecture](Architecture)
