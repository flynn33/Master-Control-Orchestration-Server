# Test-ShellAllPages.ps1
# -------------------------------------------------------------------------
# Drives the Master Control Orchestration Server Shell (WinUI 3) across all
# 9 known nav destinations, exercising an interactive action on each and
# capturing a visible-text sample so the verifier can see the view actually
# rendered (not "Forsetti View Unavailable").
#
# Depends on Invoke-ShellUiProbe.ps1 (same folder).
# Output is written to STDOUT; the caller redirects to a file.
#
# Exit codes:
#   0  run completed (per-page pass/fail is in the text body)
#   1  Shell not running and auto-start failed
#   3  UIAutomation failure (assembly load / unreachable root)
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
        Write-Output "MasterControlShell.exe not found in any known location."
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
        Write-Output "Shell still not visible after 8s wait."
        exit 1
    }
}

Write-Output "MasterControlShell PID=$($shellProc.Id) Title='$($shellProc.MainWindowTitle)'"

# ------------------------------------------------------------------
# 2. Dot-source helper and acquire the root ONCE (reuse across pages).
# ------------------------------------------------------------------
. $probePath | Out-Null  # prints its banner; that's fine
[void](Get-ShellMainWindow)

# The helper caches $script:ShellRoot inside Invoke-ShellUiProbe.ps1's
# scope when the two scripts share the same invocation scope. For a
# script-run (not dot-source) context we re-acquire the root locally so
# the depth-bounded walkers below work even if the inner cache is empty.
$proc = Get-Process -Name MasterControlShell -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
        Select-Object -First 1
$rootEl = [System.Windows.Automation.AutomationElement]::FromHandle($proc.MainWindowHandle)

# ------------------------------------------------------------------
# 3. Page text enumerator — breadth-first, depth-bounded, name-safe,
#    element-count-capped.
# ------------------------------------------------------------------
function Get-PageVisibleText {
    param(
        [System.Windows.Automation.AutomationElement]$Root,
        [int]$MaxElements = 50,
        [int]$MaxDepth    = 8
    )
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
            $isOff = $false
            try { $isOff = $n.E.Current.IsOffscreen } catch { $isOff = $false }
            if (-not $isOff -and -not [string]::IsNullOrWhiteSpace($name)) {
                $short = $name.Trim()
                if ($short.Length -gt 120) { $short = $short.Substring(0,120) + '...' }
                # Filter noisy nav/LIVE command stream items to keep the
                # sample focused on page content.
                if ($ct -in @('text','pane','group','button','edit','combo box','list','data grid','custom')) {
                    if ($short -notmatch '^admin_api_request' -and
                        $short -notmatch '^\d\d:\d\d:\d\d ') {
                        [void]$texts.Add("[$ct] $short")
                    }
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
    return ,($texts | Where-Object { $_ -ne '' } | Select-Object -Unique)
}

function Find-ButtonByName {
    param([System.Windows.Automation.AutomationElement]$Root, [string]$Name)
    $cond = New-Object System.Windows.Automation.AndCondition(
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty, $Name)),
        (New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button))
    )
    try {
        return $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    } catch { return $null }
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
            [System.Windows.Automation.TreeScope]::Children, $cond)
    } catch { return $null }
}

function Get-FirstNonEmptyText {
    param([System.Windows.Automation.AutomationElement]$Root, [int]$MaxDepth = 4)
    if (-not $Root) { return $null }
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    $q = New-Object System.Collections.Queue
    $q.Enqueue(@{ E = $Root; D = 0 })
    while ($q.Count -gt 0) {
        $n = $q.Dequeue()
        if ($n.D -gt $MaxDepth) { continue }
        try {
            $nm = $n.E.Current.Name
            if (-not [string]::IsNullOrWhiteSpace($nm)) { return $nm }
        } catch { }
        try {
            $c = $walker.GetFirstChild($n.E); while ($c) { $q.Enqueue(@{ E = $c; D = $n.D + 1 }); $c = $walker.GetNextSibling($c) }
        } catch { }
    }
    return $null
}

# ------------------------------------------------------------------
# 4. Navigate helper — returns a pscustomobject per page.
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
    Write-KV 'nav-switch'       $navResult
    $selectedSafe = if ($selected) { $selected } else { '<null>' }
    Write-KV 'selected-tab'     $selectedSafe

    Start-Sleep -Milliseconds $PostNavSettleMs

    # Re-acquire root after navigation; the snapshotted element can go stale
    # when WinUI 3 swaps frame content.
    $localRoot = $null
    try { $localRoot = [System.Windows.Automation.AutomationElement]::FromHandle($proc.MainWindowHandle) } catch { }
    if (-not $localRoot) { $localRoot = $rootEl }

    $texts = Get-PageVisibleText -Root $localRoot -MaxElements $MaxElementsPerPage -MaxDepth 8
    $sample = $texts | Select-Object -First 10
    Write-Output '  visible-text (top 10):'
    if ($sample.Count -eq 0) { Write-Output '    (none)' } else { $sample | ForEach-Object { Write-Output "    $_" } }

    $unavailHits = @($sample | Where-Object { $_ -match 'Forsetti View Unavailable' })
    $unavailable = ($unavailHits.Count -gt 0)
    Write-KV 'forsetti-unavailable' ([string]$unavailable)

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
Write-Section "RUN SUMMARY"
Write-KV 'hostname'     ([System.Net.Dns]::GetHostName())
Write-KV 'timestamp'    ((Get-Date).ToString('o'))
Write-KV 'shell-pid'    $proc.Id
Write-KV 'cap-per-page' $MaxElementsPerPage

# 1 - Overview
Exercise-Page -TabName 'Overview' -Action {
    param($root)
    # Click the top-bar "New MCP Server" wizard button - it navigates to
    # Runtime. We use a button guaranteed to exist on Overview so the click
    # verifies the hero card's quick-action row is wired.
    $btn = Find-ButtonByName -Root $root -Name 'New MCP Server'
    if (-not $btn) { return 'no-new-mcp-button' }
    $before = Get-ShellSelectedNavItem
    if (-not (Try-Invoke $btn)) { return 'invoke-failed' }
    Start-Sleep -Milliseconds 900
    $after = Get-ShellSelectedNavItem
    $beforeName = if ($before) { $before.Name } else { '<none>' }
    $afterName  = if ($after)  { $after.Name  } else { '<none>' }
    return "nav '$beforeName' -> '$afterName'"
}

# 2 - Telemetry: verify hostname appears in Host Identity card.
Exercise-Page -TabName 'Telemetry' -Action {
    param($root)
    $machineName = [System.Net.Dns]::GetHostName()
    $texts = Get-PageVisibleText -Root $root -MaxElements 60 -MaxDepth 10
    $hit = $texts | Where-Object { $_ -match [regex]::Escape($machineName) } | Select-Object -First 1
    if ($hit) { return "host-identity-found: $hit" } else { return "host-identity-NOT-FOUND expected='$machineName'" }
}

# 3 - Runtime: click "New Apple Host" wizard button and try to detect a
# dialog (legacy design) OR confirm the nav-based guided workflow status
# banner updates (current design). Either outcome is valid evidence.
Exercise-Page -TabName 'Runtime' -Action {
    param($root)
    $btn = Find-ButtonByName -Root $root -Name 'New Apple Host'
    if (-not $btn) { return 'no-new-apple-host-button' }
    if (-not (Try-Invoke $btn)) { return 'invoke-failed' }
    Start-Sleep -Milliseconds 900
    # Check for a ContentDialog with title 'New Apple Host Wizard'.
    $dlg = Find-WindowByTitle -Title 'New Apple Host Wizard'
    if ($dlg) {
        $title = $dlg.Current.Name
        # Close via Close button (ContentDialog exposes it with Name='Close').
        $closed = $false
        try {
            $closeBtn = Find-ButtonByName -Root $dlg -Name 'Close'
            if ($closeBtn -and (Try-Invoke $closeBtn)) { $closed = $true }
        } catch { }
        if (-not $closed) {
            # Root-element scan as fallback.
            $rootDesk = [System.Windows.Automation.AutomationElement]::RootElement
            try {
                $closeBtn = Find-ButtonByName -Root $rootDesk -Name 'Close'
                if ($closeBtn -and (Try-Invoke $closeBtn)) { $closed = $true }
            } catch { }
        }
        return "dialog-opened title='$title' closed=$closed"
    }
    # No dialog - inspect the guided workflow status banner text.
    $texts2 = Get-PageVisibleText -Root $root -MaxElements 80 -MaxDepth 10
    $banner = $texts2 | Where-Object { $_ -match 'Apple Host|apple host|Apple hosts' } | Select-Object -First 1
    if ($banner) { return "no-dialog; status-banner='$banner'" } else { return 'no-dialog; no-banner-update' }
}

# 4 - AI Integrations: count Provider Modules capabilities, click one card,
# observe summary text update.
Exercise-Page -TabName 'AI Integrations' -Action {
    param($root)
    $texts = Get-PageVisibleText -Root $root -MaxElements 80 -MaxDepth 10
    $pmMatches = @($texts | Where-Object { $_ -match 'Provider Modules' })
    $providerModulesPanel = ($pmMatches.Count -gt 0)
    $capsRaw = @($texts | Where-Object {
        $_ -match '\[button\]' -and $_ -match '(Code|Image|Video|Audio|Text|Embed|Vision|Search|Plan|Tool|Multimodal|Chat)'
    })
    $capCount = ($capsRaw | Select-Object -Unique).Count
    $msg = "provider-modules-panel=$providerModulesPanel caps-seen=$capCount"
    # Click the first cap-like button and observe a text change elsewhere.
    $firstCap = ($capsRaw | Select-Object -First 1)
    if ($firstCap) {
        $capName = ($firstCap -replace '^\[button\]\s+', '')
        $btn = Find-ButtonByName -Root $root -Name $capName
        $before = Get-PageVisibleText -Root $root -MaxElements 40 -MaxDepth 10
        if ($btn -and (Try-Invoke $btn)) {
            Start-Sleep -Milliseconds 700
            $after = Get-PageVisibleText -Root $root -MaxElements 40 -MaxDepth 10
            $delta = (Compare-Object -ReferenceObject $before -DifferenceObject $after | Measure-Object).Count
            $msg += " clicked='$capName' text-delta=$delta"
        } else {
            $msg += " clicked=<invoke-failed>"
        }
    }
    return $msg
}

# 5 - Imports: navigate + capture top text. Check for Guided Import cue.
Exercise-Page -TabName 'Imports' -Action {
    param($root)
    $texts = Get-PageVisibleText -Root $root -MaxElements 60 -MaxDepth 10
    $hits = @($texts | Where-Object { $_ -match '(?i)guided import|\bimport\b' })
    return "guided-import-text-hits=$($hits.Count) total=$($texts.Count)"
}

# 6 - Exports
Exercise-Page -TabName 'Exports' -Action {
    param($root)
    $texts = Get-PageVisibleText -Root $root -MaxElements 60 -MaxDepth 10
    return "text-items=$($texts.Count)"
}

# 7 - Security
Exercise-Page -TabName 'Security' -Action {
    param($root)
    $texts = Get-PageVisibleText -Root $root -MaxElements 60 -MaxDepth 10
    return "text-items=$($texts.Count)"
}

# 8 - Settings
Exercise-Page -TabName 'Settings' -Action {
    param($root)
    $texts = Get-PageVisibleText -Root $root -MaxElements 60 -MaxDepth 10
    return "text-items=$($texts.Count)"
}

# 9 - CLU: Governance Posture text should match /api/clu posture (== 'warning').
Exercise-Page -TabName 'CLU' -Action {
    param($root)
    $texts = Get-PageVisibleText -Root $root -MaxElements 100 -MaxDepth 12
    # The posture word lives in the hero card (HeroGovernanceText) AND
    # somewhere on the CLU surface itself. 'warning' must appear.
    $postureHit = $texts | Where-Object { $_ -match '(?i)\bwarning\b' } | Select-Object -First 1
    $apiPosture = '<api-unreachable>'
    try {
        $r = Invoke-WebRequest -Uri 'http://127.0.0.1:7300/api/clu' -TimeoutSec 2 -UseBasicParsing
        $j = $r.Content | ConvertFrom-Json
        if ($j.posture) { $apiPosture = $j.posture }
    } catch { }
    if ($postureHit) {
        return "ui-posture-token='warning' (via '$postureHit'); api-posture='$apiPosture'"
    } else {
        return "ui-posture-token-MISSING; api-posture='$apiPosture'"
    }
}

# ------------------------------------------------------------------
# 6. Verdict
# ------------------------------------------------------------------
Write-Section "VERDICT"
if ($script:FailedPages.Count -eq 0 -and $script:PartialPages.Count -eq 0) {
    Write-Output 'ALL 9 PAGES EXERCISED'
} elseif ($script:FailedPages.Count -eq 0) {
    Write-Output 'PARTIAL'
    Write-Output ('  Forsetti-unavailable: ' + ($script:PartialPages -join ', '))
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
