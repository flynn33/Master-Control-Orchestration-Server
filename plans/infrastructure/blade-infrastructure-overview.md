## BLADE MCP Infrastructure - Overview Plan

### Server: MASTER-CONTROL (192.168.1.3)
- Windows Server 2022 Datacenter
- 2x Xeon E5-2640 (12c/24t), 64GB RAM
- 280GB (C:) + 10TB (D:)

### MCP Server Stack (18 Blade servers + 7 Sub-agents + 1 Gateway)
Ports 7101-7118: Blade tool servers
Ports 7201-7207: AI sub-agents
Port 7200: Aggregator Gateway (single endpoint for all tools)
Port 7120: Client tracker / broadcast

### Caddy Reverse Proxy
- Port 8080: HTTP gateway (individual routes + aggregator)
- Port 8443: HTTPS gateway (self-signed cert, aggregator only)

### Remote Access
- HTTPS: https://192.168.1.3:8443/mcp/gateway
- Self-signed cert: CN=master-control, IP SAN 192.168.1.3
- Thumbprint: 9A2DC81D
- Installer: Install-BladeGatewayPlugin.ps1

### Data Storage
All at D:\mcp\data:
- memory/ - Shared key-value memory (namespaced)
- sessions/ - Session context persistence
- repos/ - Mirrored git repos
- docs/ - Documentation corpus
- indexes/ - Symbol and vector indexes
- snippets/ - Reusable code patterns
- build-cache/, lint-cache/, response-cache/ - Build artifacts

### Dashboard
- URL: http://192.168.1.3:8080/dashboard/
- Real-time: CPU, RAM, network, all server status, sub-agent metrics
- Source: D:\mcp\dashboard\index.html
