## master-control-dashboard - Technical Details

### Server Configuration
- Caddy route: /dashboard* -> reverse_proxy 127.0.0.1:18000 (uri strip_prefix)
- Static server: Node.js on port 18000 (managed by serve.ps1)
- All API calls proxied through Caddy :8080

### Backend Services (25 total)
#### Blade Tool Servers (ports 7101-7118):
repo-search(7101), docs-search(7102), fs-cache(7103), build-cache(7104), symbol-index(7105), session-context(7106), response-cache(7107), git-intel(7108), file-digest(7109), vector-search(7110), dep-graph(7111), lint-cache(7112), snippet-store(7113), task-queue(7114), memory(7115), agent-comm(7116), coordination(7117), event-bus(7118)

#### Sub-Agents (ports 7201-7207):
sentinel(7201), architect(7202), forge(7203), scribe(7204), recon(7205), nexus(7206), watchtower(7207)

#### Infrastructure:
aggregator-gateway(7200), client-tracker(7120), metrics(7121)

### CSS Layout
- Grid-based: .server-grid uses repeat(auto-fit, minmax(180px, 1fr))
- Agent grid: .agent-grid uses repeat(7, 1fr) (forced 7 columns)
- Media query at max-width 1400px was fixed to maintain 7 columns
- Designed for 1920x1080 but works down to ~1200px wide

### Metrics System
- CPU/RAM/Network from /api/metrics (system-metrics server on port 7121)
- History from /api/metrics/history (last 60 data points)
- Real-time via /api/metrics/stream (Server-Sent Events)
- Charts: custom canvas rendering, 60-point rolling window
