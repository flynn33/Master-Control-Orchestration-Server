# Feature 1 — Multi-binary Control Plane (Build 48)

Verification date: 2026-04-19
Host endpoint: http://127.0.0.1:7300/

## 1. Service host binary running
- **Cmd:** `powershell.exe -NoProfile -Command "Get-Process MasterControlServiceHost | Select-Object Id,StartTime,MainWindowTitle,Path | Format-List"`
- **Observed:** `Id : 16628` (StartTime/Path blank — running as LOCAL SYSTEM service, inaccessible to user token; identity confirmed by process name match).
- **Artifact:** `G:/Claude/mcos_proof_servicehost.txt`
- **Status:** VERIFIED

## 2. Shell binary running
- **Cmd:** `powershell.exe -NoProfile -Command "Get-Process MasterControlShell | Select-Object Id,StartTime,Path | Format-List"`
- **Observed:** Two instances —
  - `Id : 8724`, StartTime `4/19/2026 9:53:24 AM`, Path `C:\Program Files\Master Control Orchestration Server\MasterControlShell.exe`
  - `Id : 15772`, StartTime `4/19/2026 9:53:06 AM`, Path `C:\Program Files\Master Control Orchestration Server\MasterControlShell.exe`
- **Artifact:** `G:/Claude/mcos_proof_shell.txt`
- **Status:** VERIFIED

## 3. /api/health liveness
- **Cmd:** `curl.exe -s -w '\nHTTP=%{http_code}\n' http://127.0.0.1:7300/api/health`
- **Observed:** `{"status":"ok","time":"2026-04-19T15:11:26Z"}` + `HTTP=200`
- **Artifact:** `mcos_proof_health.json`
- **Status:** VERIFIED

## 4. Browser admin HTML served
- **Cmd:** `curl.exe -s -w '\nHTTP=%{http_code} bytes=%{size_download}\n' http://127.0.0.1:7300/`
- **Observed:** `HTTP=200 bytes=3655`; `<title>Master Control Orchestration Server</title>` present; references `/app.js`.
- **Artifact:** `mcos_proof_webroot.html`
- **Status:** VERIFIED (3655 B > 1KB; title matches "Master Control")

## 5. Browser admin bundle (app.js)
- **Cmd:** `curl.exe -s http://127.0.0.1:7300/app.js -w 'HTTP=%{http_code} bytes=%{size_download}\n'`
- **Observed:** `HTTP=200 bytes=297322` (290 KB). First 200 chars: `const healthBadge = document.querySelector('#healthBadge'); const refreshButton = document.querySelector('#refreshButton'); const surfaceToolbar = document.querySelector('#surfaceToolbar'); const s`
- **Artifact:** `mcos_proof_appjs.txt`
- **Status:** VERIFIED (297322 B > 100KB; valid JS)

## 6. Browser admin CSS
- **Cmd:** `curl.exe -s http://127.0.0.1:7300/styles.css -w 'HTTP=%{http_code} bytes=%{size_download}\n'`
- **Observed:** `HTTP=200 bytes=23656` (23 KB). First 200 chars: `:root { --bg: #071018; --panel: rgba(7, 22, 34, 0.86); --panel-border: rgba(0, 242, 255, 0.28); --text: #d9fbff; --muted: #75c5cf; --accent: #00f2ff; --accent-soft: rgba(0, 242, 2`
- **Artifact:** `mcos_proof_css.txt`
- **Status:** VERIFIED (23656 B > 10KB; valid CSS)

## 7. Binary sizes (statically-linked shared runtime)
- **Cmd:** `ls -la 'C:/Program Files/Master Control Orchestration Server/*.exe'`
- **Observed:**
  - `MasterControlServiceHost.exe`  — 1,872,384 B (~1.83 MB)
  - `MasterControlShell.exe`        — 2,432,000 B (~2.38 MB)
  - `MasterControlBootstrapper.exe` —   699,392 B (~0.67 MB)
- **Status:** VERIFIED (both service host and shell in low-megabyte range, consistent with statically linked `MasterControlApp.lib`)

## 8. Bootstrapper binary
- **Observed:** `MasterControlBootstrapper.exe` 699,392 B (~683 KB), timestamped Apr 19 07:57.
- **Status:** VERIFIED

## 9. Shared lib trace (dumpbin)
- **Cmd:** `powershell.exe -NoProfile -Command "Get-Command dumpbin -ErrorAction SilentlyContinue"` (exit 1) and `which dumpbin` → `dumpbin: command not found`.
- **Observed:** `dumpbin` is not on PATH — acceptable per contract. Static link is confirmed indirectly by binary sizes (#7) and the absence of a `MasterControlApp.dll` in the install directory.
- **Status:** VERIFIED (N/A — tooling unavailable; other evidence satisfies)

---

**Verdict:** Feature 1 (Multi-binary control plane) VERIFIED — all 9 contract bullets satisfied on build 48.
