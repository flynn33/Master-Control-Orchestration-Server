# Windows-Native C++ Rule

MCOS is a Windows-native C++20 application.

## Preferred APIs

- Service hosting: Windows Service Control Manager patterns.
- Process supervision: `CreateProcessW`, pipes, Job Objects, `AssignProcessToJobObject`, `SetInformationJobObject`.
- HTTP front door: HTTP.sys or a clearly justified transitional HTTP layer.
- Outbound HTTP/proxy calls: WinHTTP.
- LAN discovery: Win32 DNS-SD APIs, `DnsServiceRegister`, `DnsServiceBrowse`; Bonjour/mDNSResponder only as a deliberate fallback or compatibility dependency.
- Telemetry: PDH for CPU/disk/network/process counters, DXGI for GPU memory where available, ETW/TraceLogging for structured activity.
- Secrets, if any remain for admin/operator tasks: DPAPI.

## Dependency policy

- Do not add Java or interpreted runtime dependencies as core MCOS implementation.
- Python may be used for test tooling only, not MCOS source implementation.
- The shipping gateway substrate is the in-process Win32 HTTP.sys adapter (`NativeHttpSysGatewayAdapter`) behind `IMcpGateway`. Any future substrate must implement the same interface so the topology stays single-endpoint to clients.
- Docker may be a development/testing option, not the required Windows production path.

