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
- External gateway binaries such as MCPJungle may be supervised as child processes, but MCOS must expose them through a replaceable C++ adapter.
- Docker may be a development/testing option, not the required Windows production path.

