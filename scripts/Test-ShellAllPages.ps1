# Test-ShellAllPages.ps1
# -------------------------------------------------------------------------
# Drives the Master Control Orchestration Server Shell (WinUI 3) across all
# 9 known nav destinations, exercising an interactive action on each and
# capturing per-page content (both chrome-wide and section-specific) so
# the verifier can see each view actually rendered (not a stale
# "Forsetti View Unavailable" card left over from the commit-d187951 fix
# era).
#
# Depends on Invoke-ShellUiProbe.ps1 (same folder).
# Output is written to STDOUT; the caller redirects to a file.
#
# Exit codes:
#   0  run completed (per-page pass/fail is in the text body)
#   1  Shell not running and auto-start failed
#   3  UIAutomation failure
# -------------------------------------------------------------------------

[CmdletBinding()]
param(
    [int]$MaxElementsPerPage = 50,
    [int]$PostNavSettleMs    = 900
)

$ErrorActionPreference = 'Stop'
$script:FailedPages    = New-Object System.Collections.Generic.List[string]
$script:PartialPages   = New-Object System.Collections.Generic.List[string]

$probePath = Join-Path $PSScriptRoot 'Invoke-ShellUiProbe.ps1'
if (-not (Test-Path $probePath)) {
    Write-Error "Cannot find sibling Invoke-ShellUiProbe.ps1 at: $probePath"
    exit 3
}

function Write-Section([string]$title) {
    Write-Output ''
    Write-Output ('=' * 72)
    Write-Output $title
    Write-Output ('=' * 72)
}

function Write-KV([string]$k, [string]$v) {
    Write-Output ("{0,-22} {1}" -f $k, $v)
}

# ------------------------------------------------------------------
# 1. Ensure the Shell is running.
# ------------------------------------------------------------------
$shellCandidates = @(
    'C:\Program Files\Master Control Orchestration Server\MasterControlShell.exe',
    (Join-Path (Split-Path -Parent $PSScriptRoot) '.claude-work\smoke-install-dest\MasterControlShell.exe'),
    (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\release\src\MasterControlShell\Release\MasterControlShell.exe')
)
$shellExe = $shellCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

$shellProc = Get-Process -Name MasterControlShell -ErrorAction SilentlyContinue |
             Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
             Select-Object -First 1

if (-not $shellProc) {
    if (-not $shellExe) {
        Write-Output 'MasterControlShell.exe not found in any known location.'
        exit 1
    }
    Write-Output "MasterControlShell not running; starting: $shellExe"
    try {
        Start-Process -FilePath $shellExe -ErrorAction Stop
    } catch {
        Write-Output "Start-Process failed: $($_.Exception.Message)"
        exit 1
    }
    Start-Sleep -Seconds 8
    $shellProc = Get-Process -Name MasterControlShell -ErrorAction SilentlyContinue |
                 Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
                 Select-Object -First 1
    if (-not $shellProc) {
        Write-Output 'Shell still not visible after 8s wait.'
        exit 1
    }
}

Write-Output "MasterControlShell PID=$($shellProc.Id) Title='$($shellProc.MainWindowTitle)'"

# ------------------------------------------------------------------
# 2. Dot-source helper; acquire root.
# ------------------------------------------------------------------
. $probePath | Out-Null
[void](Get-ShellMainWindow)

$proc = Get-Process -Name MasterControlShell -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
        Select-Object -First 1

function Get-FreshRoot {
    return [System.Windows.Automation.AutomationElement]::FromHandle($proc.MainWindowHandle)
}

# ------------------------------------------------------------------
# 3. Helpers.
# ------------------------------------------------------------------

# Chrome elements that appear on every page (title bar, status banner,
# hero card header, sub-agent grid badges, live stream). We filter these
# out of the "visible text sample" so the sample focuses on page content.
$script:ChromeNoise = @(
    'MASTER CONTROL ORCHESTRATION SERVER',
    'Windows command shell for guided setup, telemetry, MCP orchestration, provider routing, governance, and runtime control.',
    'LIVE', 'SYSTEM STATUS', 'SUB-AGENT GRID',
    'SENTINEL', 'ARCHITECT', 'FORGE', 'SCRIBE', 'RECON', 'NEXUS', 'WATCHDOG',
    'perimeter watch', 'plan + design', 'build + compile', 'docs + records',
    'discovery probe', 'provider routing', 'soak + uptime',
    'QUICK ACTIONS', 'HOST CONTROLS', 'GUIDED SETUP WIZARDS',
    'LIVE COMMAND STREAM', 'ALWAYS-ON COMMAND TELEMETRY',
    'CPU LOAD', 'MEMORY', 'DISK', 'LIVE TRAFFIC',
    'LIVE OPERATIONS', 'HOST IDENTITY', 'GOVERNANCE POSTURE', 'RUNTIME LEDGER',
    'SERVICE', 'ADMIN API', 'RUNTIME LANES', 'PROVIDERS',
    'Refresh', 'Start Service', 'Stop Service', 'Open Dashboard',
    'Open Config', 'Open Data',
    'Assign Responsibility', 'New MCP Server', 'Manage Runtime Lanes',
    'New Sub-Agent', 'New Sub-Agent Group', 'New Apple Host',
    'Manage Forsetti Modules', 'Guided Import', 'Validate Provider Routing',
    'Security Hardening', 'Host Settings'
)

function Test-IsChromeNoise([string]$text) {
    if ([string]::IsNullOrWhiteSpace($text)) { return $true }
    # Strip tag prefix like "[text] "
    $stripped = $text -replace '^\[[^\]]+\]\s+', ''
    if ($script:ChromeNoise -contains $stripped) { return $true }
    # Timestamp ('10:40:11', '#3', etc.)
    if ($stripped -match '^\d\d:\d\d:\d\d$') { return $true }
    if ($stripped -match '^#\d+$') { return $true }
    if ($stripped -match '^\d+\s+event') { return $true }
    if ($stripped -match '^(API|SERVICE|GRID)\s+(UNKNOWN|REACHABLE|UNREACHABLE|RUNNING|STOPPED|SYNCHRONIZED)') { return $true }
    if ($stripped -match '^Live status refreshed') { return $true }
    if ($stripped -match '^Polling /api/activity') { return $true }
    # Live command stream items
    if ($stripped -match '^admin_api_request') { return $true }
    if ($stripped -match '^\d\d:\d\d:\d\d\s+admin_api_request') { return $true }
    return $false
}

function Get-PageVisibleText {
    param(
        [System.Windows.Automation.AutomationElement]$Root,
        [int]$MaxElements = 50,
        [int]$MaxDepth    = 12,
        [switch]$IncludeChrome
    )
    if (-not $Root) { return @() }
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    $q = New-Object System.Collections.Queue
    $q.Enqueue(@{ E = $Root; D = 0 })
    $texts = New-Object System.Collections.Generic.List[string]
    $count = 0
    while ($q.Count -gt 0 -and $count -lt $MaxElements) {
        $n = $q.Dequeue()
        if ($n.D -gt $MaxDepth) { continue }
        $count++
        try {
            $ct   = $n.E.Current.LocalizedControlType
            $name = $n.E.Current.Name
            if (-not [string]::IsNullOrWhiteSpace($name) -and
                $ct -in @('text','button','edit','combo box','check box','data grid','list','list item','pane')) {
                $short = $name.Trim()
                # Multi-line names: keep only the first line.
                $short = ($short -split "`r?`n")[0]
                if ($short.Length -gt 120) { $short = $short.Substring(0,120) + '...' }
                $candidate = "[$ct] $short"
                if ($IncludeChrome -or -not (Test-IsChromeNoise $candidate)) {
                    [void]$texts.Add($candidate)
                }
            }
        } catch { }
        try {
            $c = $walker.GetFirstChild($n.E)
            while ($c) {
                $q.Enqueue(@{ E = $c; D = $n.D + 1 })
                $c = $walker.GetNextSibling($c)
            }
        } catch { }
    }
    return ,($texts | Select-Object -Unique)
}

function Get-AutomationIdText {
    param(
        [System.Windows.Automation.AutomationElement]$Root,
        [string]$AutomationId
    )
    if (-not $Root) { return $null }
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $AutomationId)
    try {
        $e = $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($e) {
            $n = $e.Current.Name
            if (-not [string]::IsNullOrWhiteSpace($n)) { return $n.Trim() }
        }
    } catch { }
    return $null
}

function Find-ButtonByName {
    param([System.Windows.Automation.AutomationElement]$Root, [string]$Name)
    if (-not $Root) { return $null }
    $cond = New-Object System.Windows.Automation.AndCondition(
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty, $Name)),
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button))
    )
    try { return $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond) }
    catch { return $null }
}

function Try-Invoke {
    param([System.Windows.Automation.AutomationElement]$El)
    if (-not $El) { return $false }
    $pat = $null
    if ($El.TryGetCurrentPattern(
            [System.Windows.Automation.InvokePattern]::Pattern, [ref]$pat)) {
        try { $pat.Invoke(); return $true } catch { return $false }
    }
    $selPat = $null
    if ($El.TryGetCurrentPattern(
            [System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$selPat)) {
        try { $selPat.Select(); return $true } catch { return $false }
    }
    return $false
}

function Find-WindowByTitle {
    param([string]$Title)
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty, $Title)
    try {
        return [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
            [System.Windows.Automation.TreeScope]::Descendants, $cond)
    } catch { return $null }
}

# ------------------------------------------------------------------
# 4. Per-page orchestrator.
# ------------------------------------------------------------------
function Exercise-Page {
    param(
        [Parameter(Mandatory)][string]$TabName,
        [scriptblock]$Action = $null
    )
    Write-Section "PAGE: $TabName"
    $navResult = 'FAIL'
    $selected  = $null
    try {
        $selected = Select-ShellNavTab -TabName $TabName
        if ($selected -eq $TabName) { $navResult = 'PASS' }
    } catch {
        Write-Output "  nav exception: $($_.Exception.Message)"
    }
    Write-KV 'nav-switch'   $navResult
    $selectedSafe = if ($selected) { $selected } else { '<null>' }
    Write-KV 'selected-tab' $selectedSafe

    Start-Sleep -Milliseconds $PostNavSettleMs

    $localRoot = Get-FreshRoot

    # Section-specific sample (chrome filtered, capped at 10 for logging).
    $sectionTexts = Get-PageVisibleText -Root $localRoot -MaxElements $MaxElementsPerPage -MaxDepth 15
    $sample = $sectionTexts | Select-Object -First 10
    Write-Output '  section-visible-text (top 10, chrome filtered):'
    if ($sample.Count -eq 0) { Write-Output '    (none)' }
    else { $sample | ForEach-Object { Write-Output "    $_" } }

    $unavailHits = @($sectionTexts | Where-Object { $_ -match 'Forsetti View Unavailable' })
    $unavailable = ($unavailHits.Count -gt 0)
    Write-KV 'forsetti-unavailable' ([string]$unavailable)

    # Hero card anchors always available (proves the window is live).
    $eyebrow = Get-AutomationIdText -Root $localRoot -AutomationId 'CurrentViewEyebrowText'
    $title   = Get-AutomationIdText -Root $localRoot -AutomationId 'CurrentViewTitleText'
    if ($eyebrow) { Write-KV 'hero-eyebrow' $eyebrow }
    if ($title)   { Write-KV 'hero-title'   $title }

    $actionResult = '<none>'
    if ($Action) {
        try {
            $actionResult = & $Action $localRoot
        } catch {
            $actionResult = "EXCEPTION: $($_.Exception.Message)"
        }
    }
    Write-KV 'action-result' $actionResult

    if ($navResult -ne 'PASS') { $script:FailedPages.Add($TabName) }
    elseif ($unavailable)      { $script:PartialPages.Add("$TabName (Forsetti View Unavailable)") }
}

# ------------------------------------------------------------------
# 5. Execute the sequence.
# ------------------------------------------------------------------
Write-Section 'RUN SUMMARY'
Write-KV 'hostname'     ([System.Net.Dns]::GetHostName())
Write-KV 'timestamp'    ((Get-Date).ToString('o'))
Write-KV 'shell-pid'    $proc.Id
Write-KV 'cap-per-page' $MaxElementsPerPage

# 1 - Overview: click Quick Actions "New MCP Server" and verify nav switch.
Exercise-Page -TabName 'Overview' -Action {
    param($root)
    $overviewStatus = Get-AutomationIdText -Root $root -AutomationId 'OverviewStatusText'
    if ($overviewStatus) { Write-Output ("    overview-status = " + $overviewStatus) }

    $btn = Find-ButtonByName -Root $root -Name 'New MCP Server'
    if (-not $btn) { return 'no-new-mcp-button' }
    $before = Get-ShellSelectedNavItem
    if (-not (Try-Invoke $btn)) { return 'invoke-failed' }
    Start-Sleep -Milliseconds 900
    $after = Get-ShellSelectedNavItem
    $beforeName = if ($before) { $before.Name } else { '<none>' }
    $afterName  = if ($after)  { $after.Name  } else { '<none>' }
    if ($afterName -ne $beforeName) {
        return "QuickAction 'New MCP Server' nav '$beforeName' -> '$afterName'"
    } else {
        return "nav unchanged ('$afterName')"
    }
}

# 2 - Telemetry: verify host-identity card shows machine hostname.
# Accept either the 15-char NetBIOS name (env:COMPUTERNAME / Win32
# GetComputerName) OR the full .NET DNS hostname - the Shell reads the OS
# hostname through the former on Windows, so when the FQDN has >15 chars
# we see a 'truncated' form that is NOT a product bug.
Exercise-Page -TabName 'Telemetry' -Action {
    param($root)
    $dnsName  = [System.Net.Dns]::GetHostName()
    $netbios  = $env:COMPUTERNAME
    $hostIdent = Get-AutomationIdText -Root $root -AutomationId 'HostIdentityText'
    $heroIdent = Get-AutomationIdText -Root $root -AutomationId 'HeroIdentityText'
    $msg = "host-identity-card='$hostIdent' hero-identity='$heroIdent' dns='$dnsName' netbios='$netbios'"
    $matched = $false
    foreach ($cand in @($dnsName, $netbios) | Select-Object -Unique) {
        if ($hostIdent -and ($hostIdent -match [regex]::Escape($cand))) {
            $msg += " PASS(section-card matches '$cand')"; $matched = $true; break
        }
        if ($heroIdent -and ($heroIdent -match [regex]::Escape($cand))) {
            $msg += " PASS(hero-card matches '$cand')"; $matched = $true; break
        }
    }
    if (-not $matched) { $msg += ' FAIL(no-hostname-variant-matched)' }
    return $msg
}

# 3 - Runtime: click "New Apple Host" quick-action. Verify either a dialog
# opens (legacy 'New Apple Host Wizard' modal) OR the runtime page's
# guided-workflow banner is updated to the apple-host copy.
Exercise-Page -TabName 'Runtime' -Action {
    param($root)
    $btn = Find-ButtonByName -Root $root -Name 'New Apple Host'
    if (-not $btn) { return 'no-new-apple-host-button' }
    if (-not (Try-Invoke $btn)) { return 'invoke-failed' }
    Start-Sleep -Milliseconds 900
    $dlg = Find-WindowByTitle -Title 'New Apple Host Wizard'
    if ($dlg) {
        $dtitle = $dlg.Current.Name
        # Close via Close button.
        $closed = $false
        $closeBtn = Find-ButtonByName -Root $dlg -Name 'Close'
        if ($closeBtn -and (Try-Invoke $closeBtn)) { $closed = $true }
        if (-not $closed) {
            $deskCloseBtn = Find-ButtonByName -Root ([System.Windows.Automation.AutomationElement]::RootElement) -Name 'Close'
            if ($deskCloseBtn -and (Try-Invoke $deskCloseBtn)) { $closed = $true }
        }
        return "dialog-opened title='$dtitle' closed=$closed"
    }
    # No dialog - check the guided workflow status banner text (on MainWindow).
    $banner = Get-AutomationIdText -Root (Get-FreshRoot) -AutomationId 'GuidedWorkflowStatusText'
    if ($banner -and $banner -match '(?i)apple host') {
        return "no-dialog; banner-updated='$banner'"
    }
    # Fallback: scan visible text for any 'Apple Host' mention that isn't the button label.
    $hits = @(Get-PageVisibleText -Root (Get-FreshRoot) -MaxElements 60 -MaxDepth 15 |
              Where-Object { $_ -match '(?i)apple host' -and $_ -notmatch 'New Apple Host$' })
    if ($hits.Count -gt 0) { return "no-dialog; page-text-mentions-apple-host='$($hits[0])'" }
    return 'no-dialog; no-banner-update'
}

# 4 - AI Integrations: verify the Providers/Provider Modules panel shows
# capabilities and a card-click causes a summary text change.
Exercise-Page -TabName 'AI Integrations' -Action {
    param($root)

    # Count Provider Modules capability items.
    $pmCount = 0
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty, 'ProviderCapabilitiesListView')
    $lv = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    if ($lv) {
        $itemCond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::ListItem)
        try { $pmCount = @($lv.FindAll([System.Windows.Automation.TreeScope]::Descendants, $itemCond)).Count } catch { }
    }

    # Known status chip x:Names on the Providers surface.
    $chipIds = @(
        'ClaudeStatusChipText','ChatGptStatusChipText','GrokConnectStatusText',
        'ClaudeSignInStatusText','ChatGptSignInStatusText','QuickConnectStatusText',
        'QuickConnectSummaryText'
    )
    $chipValues = @{}
    foreach ($id in $chipIds) {
        $v = Get-AutomationIdText -Root $root -AutomationId $id
        if ($v) { $chipValues[$id] = $v }
    }
    $panelCount = $chipValues.Keys.Count

    # Try to click a provider-related button and observe summary change.
    $summaryBefore = Get-AutomationIdText -Root $root -AutomationId 'QuickConnectSummaryText'
    $btn = $null
    foreach ($name in @('Auto-Connect','Install Claude Code CLI','Sign in with Claude','Sign in with OpenAI','Install Codex CLI')) {
        $btn = Find-ButtonByName -Root $root -Name $name
        if ($btn) { break }
    }
    $actionLog = ''
    if ($btn) {
        $btnName = $btn.Current.Name
        if (Try-Invoke $btn) {
            Start-Sleep -Milliseconds 900
            $summaryAfter = Get-AutomationIdText -Root (Get-FreshRoot) -AutomationId 'QuickConnectSummaryText'
            $delta = if ($summaryBefore -eq $summaryAfter) { 'no-change' } else { 'CHANGED' }
            $actionLog = "clicked='$btnName'; summary='$summaryBefore' -> '$summaryAfter' ($delta)"
        } else {
            $actionLog = "clicked='$btnName'; invoke-failed"
        }
    } else {
        $actionLog = 'no-provider-button-found'
    }

    $panelKeys = ($chipValues.Keys -join ',')
    return "provider-modules-capabilities=$pmCount; provider-panels-found=$panelCount keys=[$panelKeys]; $actionLog"
}

# 5 - Imports: verify the Import editor controls exist (ImportModeSelector,
# RunPackageImportButton). Capture top section text.
Exercise-Page -TabName 'Imports' -Action {
    param($root)
    $hits = @()
    foreach ($id in @('ImportModeSelector','RunPackageImportButton','PackageSourceTextBox','RepoUrlTextBox','ZipSourceTextBox')) {
        $v = Get-AutomationIdText -Root $root -AutomationId $id
        if ($v -ne $null) { $hits += "$id='$v'" }
        # ComboBox/TextBox often have empty Name (placeholder-only). Detect presence by Find, not value.
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
        $e = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($e -and ($v -eq $null)) { $hits += "$id=<present>" }
    }
    if ($hits.Count -eq 0) { return 'NO-IMPORT-CONTROLS-FOUND' }
    return "import-controls-present: " + ($hits -join ' | ')
}

# 6 - Exports: similar anchor check.
Exercise-Page -TabName 'Exports' -Action {
    param($root)
    $hits = @()
    foreach ($id in @('ExportSelector','SelectedExportFileNameText','RefreshExportsButton','ExportStatusText','ExportArtifactCountText','ExportsNarrativeText')) {
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
        $e = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($e) {
            $v = $e.Current.Name
            if ([string]::IsNullOrWhiteSpace($v)) { $hits += "$id=<present>" }
            else { $hits += "$id='$v'" }
        }
    }
    if ($hits.Count -eq 0) { return 'NO-EXPORT-CONTROLS-FOUND' }
    return "export-controls-present: " + ($hits -join ' | ')
}

# 7 - Security
Exercise-Page -TabName 'Security' -Action {
    param($root)
    $hits = @()
    foreach ($id in @('SecurityProtocolsToggle','EnableTlsToggle','SaveSecurityButton','SecurityEditorStatusText','BindAddressText','SecurityNarrativeText')) {
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
        $e = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($e) {
            $v = $e.Current.Name
            if ([string]::IsNullOrWhiteSpace($v)) { $hits += "$id=<present>" }
            else { $hits += "$id='$v'" }
        }
    }
    if ($hits.Count -eq 0) { return 'NO-SECURITY-CONTROLS-FOUND' }
    return "security-controls-present: " + ($hits -join ' | ')
}

# 8 - Settings
Exercise-Page -TabName 'Settings' -Action {
    param($root)
    $hits = @()
    foreach ($id in @('InstanceNameTextBox','BindAddressTextBox','BrowserPortTextBox','ApplySettingsButton','SettingsSummaryText','SettingsStatusText')) {
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
        $e = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($e) {
            $v = $e.Current.Name
            if ([string]::IsNullOrWhiteSpace($v)) { $hits += "$id=<present>" }
            else { $hits += "$id='$v'" }
        }
    }
    if ($hits.Count -eq 0) { return 'NO-SETTINGS-CONTROLS-FOUND' }
    return "settings-controls-present: " + ($hits -join ' | ')
}

# 9 - CLU: Governance Posture text must match /api/clu posture ('warning').
Exercise-Page -TabName 'CLU' -Action {
    param($root)
    $uiPosture    = Get-AutomationIdText -Root $root -AutomationId 'CluPostureText'
    $heroPosture  = Get-AutomationIdText -Root $root -AutomationId 'HeroGovernanceText'
    $narrative    = Get-AutomationIdText -Root $root -AutomationId 'CluNarrativeText'
    $apiPosture = '<api-unreachable>'
    try {
        $r = Invoke-WebRequest -Uri 'http://127.0.0.1:7300/api/clu' -TimeoutSec 3 -UseBasicParsing
        $j = $r.Content | ConvertFrom-Json
        if ($j.posture) { $apiPosture = $j.posture }
    } catch { }
    # Normalize: HeroGovernanceText is multi-line; first token is the posture word.
    $heroPostureFirstWord = if ($heroPosture) { ($heroPosture -split '\s|\r?\n')[0] } else { $null }
    $uiHasPostureToken = ($uiPosture -match '(?i)warning') -or ($heroPostureFirstWord -match '(?i)warning')
    $match = if ($uiHasPostureToken -and ($apiPosture -eq 'warning')) { 'MATCH' }
             elseif (-not $uiHasPostureToken) { 'UI-MISSING' }
             elseif ($apiPosture -ne 'warning') { "API-MISMATCH(api='$apiPosture')" }
             else { 'UNKNOWN' }
    return "ui-posture='$uiPosture' hero-posture-first='$heroPostureFirstWord' api='$apiPosture' result=$match"
}

# ------------------------------------------------------------------
# 6. Verdict
# ------------------------------------------------------------------
Write-Section 'VERDICT'
if ($script:FailedPages.Count -eq 0 -and $script:PartialPages.Count -eq 0) {
    Write-Output 'ALL 9 PAGES EXERCISED'
} elseif ($script:FailedPages.Count -eq 0) {
    Write-Output 'PARTIAL'
    Write-Output ('  forsetti-unavailable: ' + ($script:PartialPages -join ', '))
} else {
    Write-Output 'PARTIAL'
    if ($script:FailedPages.Count -gt 0) {
        Write-Output ('  nav-fail: ' + ($script:FailedPages -join ', '))
    }
    if ($script:PartialPages.Count -gt 0) {
        Write-Output ('  unavailable: ' + ($script:PartialPages -join ', '))
    }
}
Write-Output 'Done.'
exit 0
