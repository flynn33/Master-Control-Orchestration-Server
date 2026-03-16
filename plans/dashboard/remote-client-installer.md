## Remote Client Installer - Plan & Status

### Problem Solved
Remote Claude Code Desktop instances need to connect to the BLADE MCP Gateway. Previous approaches (plugins, zip files) failed because Claude Code Desktop uses a marketplace system for plugins, not directory scanning.

### Solution: Install-BladeGatewayPlugin.ps1
Self-contained PowerShell script (12KB) that:
1. Checks LAN connectivity to 192.168.1.3:8080
2. Downloads + installs self-signed cert to CurrentUser\Root store
3. Saves PEM cert to ~/.blade-mcp/master-control.pem
4. Sets NODE_EXTRA_CA_CERTS env var (for Node.js TLS in Claude Code)
5. Adds master-control-gateway entry to ~/.claude.json mcpServers
6. Verifies HTTPS connectivity to https://192.168.1.3:8443/health

### Distribution
- Desktop: C:\Users\Master-Control\Desktop\Install-BladeGatewayPlugin.ps1
- NAS: \\192.168.1.3\nas\Install-BladeGatewayPlugin.ps1
- HTTP: http://192.168.1.3:8080/Install-BladeGatewayPlugin.ps1
- Source: D:\mcp\plugin\Install-BladeGatewayPlugin.ps1

### Key Design Decisions
- Directly edits ~/.claude.json (NOT a plugin - plugins use marketplace system)
- Dual cert trust: OS store (for PowerShell) + NODE_EXTRA_CA_CERTS (for Node.js)
- Stale cert removal wrapped in try/catch (admin certs may be access-denied)
- PS 5.1 compatible (no -SkipCertificateCheck, uses ServicePointManager fallback)
- Works with right-click -> Run with PowerShell (the default PS 5.1 handler)

### .claude.json Entry Format
mcpServers.master-control-gateway = { type: http, url: https://192.168.1.3:8443/mcp/gateway }

### What DID NOT Work (lessons learned)
1. Plugin directory at ~/.claude/plugins/blade-mcp-gateway/ - Claude Code Desktop does not scan for arbitrary plugin folders. It uses a git-based marketplace system.
2. installed_plugins.json is managed by the marketplace, not user-editable.
3. Zip file extraction approach - too many manual steps.
4. UNC path copying via bash - backslashes get eaten by bash interpretation.
