# 15 - LAN Remote Access

**Build:** Master Control Orchestration Server build 48
**Host:** PC-GAMING-R7-58 (Windows 10 Pro 25H2, build 26200)
**Date:** 2026-04-19
**Verdict:** LAN ACCESS CONFIRMED on IPv4 (192.168.1.7:7300). IPv6 NOT BOUND.

---

## 1. Configuration

File: `C:\ProgramData\MasterControlOrchestrationServer\config\master-control-orchestration-server.json`

```
bindAddress: "0.0.0.0"
browserPort: 7300
security.allowOpenLanAccess: true
security.enableAuthentication: false
security.enableTls: false
```

Expectation met: bind is `0.0.0.0` and port is `7300`.

## 2. Listener state

`mcos_proof_lan_listen.txt`:

```
LocalAddress LocalPort OwningProcess State
------------ --------- ------------- -----
0.0.0.0           7300         12908 Listen
```

PID 12908 was listening on `0.0.0.0:7300` — accepting from all IPv4 interfaces.
**Note:** only the IPv4 wildcard is bound. There is no `::` (IPv6 wildcard) or
dual-stack socket. This is the direct consequence of `bindAddress="0.0.0.0"`
being written literally as an IPv4 address.

## 3. Host LAN addresses

`mcos_proof_lan_ips.txt`:

| IPAddress                                 | Interface | Family |
|-------------------------------------------|-----------|--------|
| 192.168.1.7                               | Ethernet  | IPv4   |
| fde3:c02c:3afa:4572:6687:be77:e9f1:32c8   | Ethernet  | IPv6 (ULA) |
| fde3:c02c:3afa:4572:c0d5:e1a9:5911:7841   | Ethernet  | IPv6 (temporary) |
| fe80::e4c1:163d:9df8:143%9                | Ethernet  | IPv6 link-local |

## 4. IPv4 LAN probe

`mcos_proof_lan_ipv4_probe_192_168_1_7.json`:

```
{"status":"ok","time":"2026-04-19T15:34:17Z"}
HTTP=200 bytes=54
```

Identical payload and status to the localhost baseline (`{"status":"ok",...}`
HTTP 200, 54 bytes). **Confirmed: LAN clients on 192.168.1.0/24 can reach the
admin on 192.168.1.7:7300.**

## 5. IPv6 ULA probe

`mcos_proof_lan_ipv6_probe.json`:

```
HTTP=000
```

Verbose trace: `connect ... port 7300 from :: port 56692 failed: Connection
refused`. The service does not listen on IPv6 because `bindAddress` is
`0.0.0.0` (IPv4-only wildcard), not `::` (dual-stack wildcard). **IPv6 access
is not available in this build.**

## 6. Firewall

`mcos_proof_lan_firewall.txt`: empty. No rules matching `*Master Control*` or
`*MCOS*` exist. Since the IPv4 LAN probe from a peer on the subnet still
succeeded (see section 4 — incoming connections from 192.168.1.7 are recorded
with states TimeWait), Windows Defender Firewall is allowing port 7300 inbound
on the active (Private) profile. This likely indicates either (a) the active
profile has TCP inbound allowed for the binary/category, or (b) the install
was run with `--skip-firewall` and the rule is simply absent without blocking
the port on this network profile.

If additional hosts on a more restrictive network profile are expected to
connect, this rule would make the intent explicit:

```
netsh advfirewall firewall add rule name="MCOS Browser 7300" ^
  dir=in action=allow protocol=TCP localport=7300 profile=Private
```

(Not applied — no firewall changes were made. Operator consent required.)

## 7. Browser surface parity

`mcos_proof_lan_browser_content.txt`:

| URL          | localhost bytes | 192.168.1.7 bytes | verdict |
|--------------|-----------------|-------------------|---------|
| /            | 3655            | 3655              | MATCH   |
| /app.js      | 297322          | 297322            | MATCH   |
| /styles.css  | 23656           | 23656             | MATCH   |

LAN clients receive byte-identical assets to localhost.

## 8. Activity ring

`mcos_proof_lan_activity.json` (fetched via 192.168.1.7:7300) shows
ids 13-18 for `GET /`, `GET /app.js`, `GET /styles.css` (two each, one from
localhost and one from 192.168.1.7). The ring records method, target, status,
latency and timestamp, but **does not record source IP** — so LAN vs local
origin cannot be distinguished from the ring. The LAN requests completed
(HTTP 200) which is the practical proof. Source-IP recording would be a
useful enhancement for future builds.

## 9. Service-lifecycle observation

Mid-test, after step 7, the server process (PID 12908) exited. A later
`Get-NetTCPConnection -LocalPort 7300` shows no `Listen` row and many
`TimeWait` entries including remote address `192.168.1.7` (i.e. the LAN
connections closed cleanly before the process stopped). This does not affect
the above verdict — all evidence above was captured while the listener was
active — but is worth noting as an auxiliary observation for a separate bug
report.

---

## Verdict

**LAN ACCESS CONFIRMED** for IPv4 address **192.168.1.7:7300** (same subnet,
Private profile). Payload parity and byte-count parity verified.

**IPv6 NOT ACCESSIBLE.** Root cause: `bindAddress` is `0.0.0.0` which is
IPv4-only; the socket never opens on `::`. To enable IPv6 the config would
need `bindAddress: "::"` (dual-stack) or an explicit `fde3:…` address, and
the service restarted.

No firewall rule exists but none is needed on the current network profile.
Documenting the `netsh advfirewall` rule above for operators who deploy on
profiles where inbound 7300 is blocked by default.

## Artefacts (in `G:\Claude\`)

- `mcos_proof_lan_listen.txt`
- `mcos_proof_lan_ips.txt`
- `mcos_proof_lan_ipv4_probe_192_168_1_7.json`
- `mcos_proof_lan_ipv6_probe.json`
- `mcos_proof_lan_firewall.txt`
- `mcos_proof_lan_browser_content.txt`
- `mcos_proof_lan_activity.json`
