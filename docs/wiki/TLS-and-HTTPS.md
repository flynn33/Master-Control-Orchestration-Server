# TLS And HTTPS

This page explains the optional TLS surfaces in the current MCOS alpha:
gateway HTTPS through HTTP.sys and admin-listener TLS through SChannel. TLS is
transport security; it does not change the trusted-LAN authentication posture.

Source authority:

- `include/MasterControl/MasterControlModels.h`
- `src/MasterControlApp/McpGatewayAdapters.cpp`
- `src/MasterControlApp/MasterControlRuntime.cpp`
- `scripts/Configure-LocalServerCert.ps1`
- `scripts/Remove-LocalServerCert.ps1`
- `scripts/Register-CertAutoRotation.ps1`

## Security Boundary

MCOS alpha uses:

```text
auth=none
trust=lan
```

TLS can protect transport and satisfy strict clients that reject plain HTTP. It
is not app-layer authorization, client identity proof, or a retail security
boundary.

## Gateway HTTPS

Gateway HTTPS is implemented by the native HTTP.sys gateway. When
`mcpGateway.tlsEnabled` is `true`, the adapter registers an HTTPS prefix on
`mcpGateway.tlsListenPort` in addition to the HTTP prefix on
`mcpGateway.listenPort`.

Default fields:

```json
{
  "mcpGateway": {
    "enabled": false,
    "listenHost": "127.0.0.1",
    "listenPort": 8080,
    "mcpPath": "/mcp",
    "healthPath": "/health",
    "tlsEnabled": false,
    "tlsListenPort": 8443,
    "tlsCertThumbprint": ""
  }
}
```

Important behavior:

- HTTP remains available on `listenPort`.
- HTTPS is terminated by HTTP.sys using the OS-level `sslcert` binding.
- The runtime does not install the certificate binding directly.
- The discovery document advertises HTTPS URLs only when the gateway reports a
  bound TLS state and `tlsCertThumbprint` is non-empty.

## Admin Listener TLS

The admin/browser listener on `browserPort` is a Winsock listener, not
HTTP.sys. Its TLS path uses SChannel and these fields:

```json
{
  "security": {
    "adminTlsEnabled": false,
    "adminTlsCertThumbprint": ""
  }
}
```

Important behavior:

- Admin TLS is opt-in.
- The certificate must exist in `Cert:\LocalMachine\My`.
- A bad or missing admin TLS certificate must not brick the admin surface; the
  runtime continues serving plain HTTP and reports the problem.
- HTTP.sys `netsh http add sslcert` bindings do not apply to this listener.

## Provision A Gateway Certificate

Run from elevated PowerShell on the MCOS host:

```powershell
cd "C:\Program Files\Master Control Orchestration Server"
scripts\Configure-LocalServerCert.ps1 -RestartService
```

The script:

1. Creates or reuses a self-signed server certificate in `Cert:\LocalMachine\My`.
2. Exports the public certificate under `%PUBLIC%\Documents\Master Control Orchestration Server\certs`.
3. Removes any prior HTTP.sys sslcert binding for the selected IP and port.
4. Adds a new HTTP.sys sslcert binding.
5. Creates the `MCOS MCP Gateway TLS (<port>)` firewall rule.
6. Prints the `mcpGateway` fields to apply.

Apply the printed fields through `PATCH /api/config`:

```powershell
$patch = @{
  mcpGateway = @{
    tlsEnabled = $true
    tlsListenPort = 8443
    tlsCertThumbprint = "<thumbprint printed by Configure-LocalServerCert.ps1>"
  }
}

Invoke-RestMethod http://localhost:7300/api/config `
  -Method Patch `
  -ContentType "application/json" `
  -Headers @{ "X-Confirm-Unsafe" = "1" } `
  -Body ($patch | ConvertTo-Json -Depth 6)

Restart-Service MasterControlProgram
```

## Enable Admin TLS

Admin TLS uses the same certificate store but a different runtime path:

```powershell
$patch = @{
  security = @{
    adminTlsEnabled = $true
    adminTlsCertThumbprint = "<thumbprint in Cert:\LocalMachine\My>"
  }
}

Invoke-RestMethod http://localhost:7300/api/config `
  -Method Patch `
  -ContentType "application/json" `
  -Headers @{ "X-Confirm-Unsafe" = "1" } `
  -Body ($patch | ConvertTo-Json -Depth 6)

Restart-Service MasterControlProgram
```

After restart, verify both the HTTP fallback and HTTPS admin endpoint from the
host before distributing the setting to operators.

## Verify Gateway HTTPS

```powershell
netsh http show sslcert ipport=0.0.0.0:8443
Get-NetFirewallRule -DisplayName "MCOS MCP Gateway TLS *"

Invoke-RestMethod http://localhost:7300/api/gateway/status | ConvertTo-Json -Depth 4
Invoke-RestMethod -SkipCertificateCheck https://127.0.0.1:8443/health | ConvertTo-Json
```

From a trusted LAN peer that trusts the exported certificate:

```powershell
Invoke-RestMethod https://<mcos-host>:8443/health | ConvertTo-Json
```

## Rotate Or Remove Certificates

Register scheduled rotation:

```powershell
scripts\Register-CertAutoRotation.ps1
```

Remove the gateway binding and firewall rule:

```powershell
scripts\Remove-LocalServerCert.ps1 -RestartService
```

Remove the exported public certificate too:

```powershell
scripts\Remove-LocalServerCert.ps1 -DeleteCert -DeletePublicExport -RestartService
```

Then patch configuration back to TLS disabled:

```powershell
$patch = @{
  mcpGateway = @{ tlsEnabled = $false; tlsCertThumbprint = "" }
  security = @{ adminTlsEnabled = $false; adminTlsCertThumbprint = "" }
}

Invoke-RestMethod http://localhost:7300/api/config `
  -Method Patch `
  -ContentType "application/json" `
  -Headers @{ "X-Confirm-Unsafe" = "1" } `
  -Body ($patch | ConvertTo-Json -Depth 6)
```

## Troubleshooting

| Symptom | Check |
|---|---|
| HTTPS gateway does not bind | Verify elevated `Configure-LocalServerCert.ps1`, `netsh http show sslcert`, and `mcpGateway.tlsListenPort`. |
| Discovery does not advertise HTTPS | Confirm `mcpGateway.tlsEnabled=true`, non-empty `tlsCertThumbprint`, and gateway status `tlsBound=true`. |
| Remote client rejects the cert | Import the exported public certificate into the client's trusted root store or use a certificate chain that client already trusts. |
| Admin HTTPS fails but HTTP still works | Check `security.adminTlsCertThumbprint` and certificate store permissions; the runtime intentionally falls back to plain HTTP. |
| Firewall blocks HTTPS | Check the `MCOS MCP Gateway TLS (<port>)` rule and the Windows network profile. |

## Related Pages

[Configuration](Configuration) |
[Gateway](Gateway) |
[Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
[Troubleshooting](Troubleshooting) |
[Release Gate](Release-Gate)
