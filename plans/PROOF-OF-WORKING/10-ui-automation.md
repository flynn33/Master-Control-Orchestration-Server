# 10 - UI Automation Helper (UIAutomationClient)

Replaces fragile pixel-coordinate `left_click(x,y)` driving of the Master
Control Orchestration Server Shell (WinUI 3 desktop app) with a reusable
PowerShell helper that drives the accessibility tree directly.

## Script

- **Path:** `G:\Claude\Master-Control-Orchastration-Server\master-control-dashboard-main\scripts\Invoke-ShellUiProbe.ps1`
- **Size:** 9,917 bytes (~298 lines)
- **Backend:** `UIAutomationClient` + `UIAutomationTypes` (Windows built-in,
  no external dependencies)

## Functions exposed

| Function | Purpose |
|---|---|
| `Find-ShellElement -Name <string>` | First descendant `AutomationElement` with matching accessible Name |
| `Find-ShellElementByAutomationId -Id <string>` | First descendant matching an AutomationId |
| `Invoke-ShellElement -Element <AutomationElement>` | Tries `InvokePattern.Invoke()`, falls back to `SelectionItemPattern.Select()` (WinUI 3 NavigationViewItem uses the latter) |
| `Get-ShellSelectedNavItem` | Walks descendants, returns the first `list item`/`tab item` whose `SelectionItemPattern.IsSelected == true`; output is a `pscustomobject` with `Name`, `AutomationId`, `ControlType` |
| `Select-ShellNavTab -TabName <string>` | Composes the above: find-by-name -> invoke -> sleep 400 ms for Frame swap -> return the newly-selected Name |

## Flags

- `-TestOverview` — demo that prints before/after selected item after clicking
  "Overview".
- `-Verbose` — each step emits a `Write-Verbose` trace.

## Exit codes

| Code | Condition |
|---|---|
| 1 | `MasterControlShell.exe` not running (or no main window) |
| 2 | Requested element not found (error includes visible nav names) |
| 3 | UIAutomation failure (assembly load, `FromHandle` failure, timeout, unsupported pattern) |

## Environment note

The Shell was already running when the proof ran (two instances, PIDs 8724
and 15772, both with titled main windows). The script does not need the
invoker to start it, but if the Shell is absent it exits 1 cleanly with a
write-error — it will **not** silently "succeed". Starting the Shell from an
unelevated shell is out of scope for the helper and is documented here
rather than attempted, per the task brief.

## Demo output — `-TestOverview`

Captured to `G:\Claude\mcos_proof_uia_test.txt`:

```text
Invoke-ShellUiProbe.ps1 loaded. Functions:
  Find-ShellElement               -Name <string>
  Find-ShellElementByAutomationId -Id   <string>
  Invoke-ShellElement             -Element <AutomationElement>
  Get-ShellSelectedNavItem
  Select-ShellNavTab              -TabName <string>

Flags:
  -TestOverview   demo: click "Overview" tab and print selected nav name
  -Verbose        print each step

Exit codes: 1 shell not running | 2 element not found | 3 UIAutomation failure

--- -TestOverview demo ---
Before invoke : selected = 'Imports'
After  invoke : selected = 'Overview'
RESULT: OK (Overview is selected)
```

Exit code: 0.

## Imports switch demo

Captured to `G:\Claude\mcos_proof_uia_imports.txt` (dot-sourced the script
first, then called `Select-ShellNavTab -TabName 'Imports'`):

```text
--- Select-ShellNavTab -TabName Imports ---
Before: selected = 'Overview'
After : selected = 'Imports'
RESULT: OK (Imports is selected)
```

**Observed Name before -> after: `Overview` -> `Imports`.** Selection verified
via `Get-ShellSelectedNavItem`, so the click landed on the intended nav
item — not merely fired into the void.

Exit code: 0.

## Known limitations

1. **WinUI 3 NavigationViewItem doesn't expose InvokePattern consistently.**
   Empirically the Shell's items advertise `SelectionItemPattern` but not
   `InvokePattern`; the helper handles this by falling through to
   `SelectionItemPattern.Select()`. If a future XAML change adds a true
   `InvokePattern`, the fast path triggers first automatically.
2. **Tree-walk depth caps.** `Get-ShellSelectedNavItem` walks to depth 6 and
   `_Get-NavRootChildrenNames` to depth 4. The current Shell NavigationView
   sits at depth ~3–4, so this covers it, but deeply-nested future controls
   may need tuning.
3. **Name-based lookup is locale-sensitive.** If the Shell is ever
   localized, "Overview" / "Imports" will be translated. Prefer adding
   `AutomationId` values in XAML and using `Find-ShellElementByAutomationId`
   for CI tests.
4. **Module-scope root is cached per process invocation.** The cached
   `AutomationElement` is only valid for the lifetime of one PowerShell
   process. Cross-process callers should start a fresh dot-source rather
   than trying to persist the element.
5. **400 ms settle delay in `Select-ShellNavTab`** is empirical, not
   event-driven. An event-sink approach using
   `Automation.AddAutomationPropertyChangedEventHandler` on the selection
   would be more robust; left as future work.
6. **Shell auto-start not attempted.** `Start-Process` against
   `C:\Program Files\Master Control Orchestration Server\MasterControlShell.exe`
   was not needed in this run (Shell already up). From a user-mode shell
   that path may require admin privileges due to the `Program Files`
   location; the helper intentionally does not embed launcher logic.

## Appendix — full script contents

Full verbatim copy of `scripts\Invoke-ShellUiProbe.ps1`:

```powershell
# Invoke-ShellUiProbe.ps1
# Reusable UIAutomation helper for the Master Control Orchestration Server Shell
# (WinUI 3). Replaces the fragile pixel-coordinate `left_click(x,y)` approach
# with accessible Name / AutomationId based driving.

[CmdletBinding()]
param(
    [switch]$TestOverview
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Load UIAutomation client
# ---------------------------------------------------------------------------
try {
    Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes -ErrorAction Stop
    Write-Verbose "Loaded UIAutomationClient + UIAutomationTypes."
} catch {
    Write-Error "Failed to load UIAutomation assemblies: $($_.Exception.Message)"
    exit 3
}

# ---------------------------------------------------------------------------
# Module-scope state
# ---------------------------------------------------------------------------
$script:ShellProcessName = 'MasterControlShell'
$script:ShellRoot        = $null   # AutomationElement for main window

function Get-ShellMainWindow {
    [CmdletBinding()]
    param()

    $proc = Get-Process -Name $script:ShellProcessName -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } |
            Select-Object -First 1

    if (-not $proc) {
        Write-Error "Shell process '$($script:ShellProcessName).exe' is not running (or has no main window)."
        exit 1
    }

    Write-Verbose "Found $($script:ShellProcessName).exe PID=$($proc.Id) HWND=$($proc.MainWindowHandle)"

    try {
        $root = [System.Windows.Automation.AutomationElement]::FromHandle($proc.MainWindowHandle)
    } catch [System.Windows.Automation.ElementNotAvailableException] {
        Write-Error "UIAutomation could not attach to HWND $($proc.MainWindowHandle) (element not available)."
        exit 3
    } catch {
        Write-Error "UIAutomation FromHandle failed: $($_.Exception.Message)"
        exit 3
    }

    if (-not $root) {
        Write-Error "UIAutomation returned null root for HWND $($proc.MainWindowHandle)."
        exit 3
    }

    $script:ShellRoot = $root
    return $root
}

function _Get-NavRootChildrenNames {
    param([System.Windows.Automation.AutomationElement]$Root)

    if (-not $Root) { return @() }
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    $names  = New-Object System.Collections.Generic.List[string]

    # Breadth-first up to depth 4, then collect NavigationViewItem / ListItem names.
    $queue = New-Object System.Collections.Queue
    $queue.Enqueue(@{ E = $Root; D = 0 })
    while ($queue.Count -gt 0) {
        $node = $queue.Dequeue()
        if ($node.D -gt 4) { continue }
        try {
            $ctype = $node.E.Current.LocalizedControlType
            $name  = $node.E.Current.Name
            if ($ctype -in @('list item','tab item','button') -and -not [string]::IsNullOrWhiteSpace($name)) {
                [void]$names.Add($name)
            }
        } catch { }

        $c = $walker.GetFirstChild($node.E)
        while ($c) {
            $queue.Enqueue(@{ E = $c; D = $node.D + 1 })
            $c = $walker.GetNextSibling($c)
        }
    }
    return ,($names | Select-Object -Unique)
}

function Find-ShellElement {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Name
    )

    if (-not $script:ShellRoot) { [void](Get-ShellMainWindow) }

    Write-Verbose "Find-ShellElement Name='$Name'"
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty, $Name
    )
    try {
        $el = $script:ShellRoot.FindFirst(
            [System.Windows.Automation.TreeScope]::Descendants, $cond
        )
    } catch [System.Windows.Automation.ElementNotAvailableException] {
        Write-Error "UIAutomation timeout / element unavailable while searching for Name='$Name'."
        exit 3
    }

    if (-not $el) {
        $visible = _Get-NavRootChildrenNames -Root $script:ShellRoot
        Write-Error ("Element not found (Name='{0}'). Visible nav names: {1}" -f $Name, ($visible -join ' | '))
        exit 2
    }
    return $el
}

function Find-ShellElementByAutomationId {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Id
    )

    if (-not $script:ShellRoot) { [void](Get-ShellMainWindow) }

    Write-Verbose "Find-ShellElementByAutomationId Id='$Id'"
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Id
    )
    try {
        $el = $script:ShellRoot.FindFirst(
            [System.Windows.Automation.TreeScope]::Descendants, $cond
        )
    } catch [System.Windows.Automation.ElementNotAvailableException] {
        Write-Error "UIAutomation timeout / element unavailable while searching for Id='$Id'."
        exit 3
    }

    if (-not $el) {
        $visible = _Get-NavRootChildrenNames -Root $script:ShellRoot
        Write-Error ("Element not found (AutomationId='{0}'). Visible nav names: {1}" -f $Id, ($visible -join ' | '))
        exit 2
    }
    return $el
}

function Invoke-ShellElement {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [System.Windows.Automation.AutomationElement]$Element
    )

    Write-Verbose "Invoke-ShellElement on '$($Element.Current.Name)' ($($Element.Current.LocalizedControlType))"

    # Prefer InvokePattern.
    $invokePatternObj = $null
    if ($Element.TryGetCurrentPattern(
            [System.Windows.Automation.InvokePattern]::Pattern,
            [ref]$invokePatternObj)) {
        try {
            $invokePatternObj.Invoke()
            return $true
        } catch {
            Write-Error "InvokePattern.Invoke() failed: $($_.Exception.Message)"
            exit 3
        }
    }

    # Fallback: SelectionItemPattern (NavigationViewItem exposes this in WinUI 3).
    $selPatternObj = $null
    if ($Element.TryGetCurrentPattern(
            [System.Windows.Automation.SelectionItemPattern]::Pattern,
            [ref]$selPatternObj)) {
        try {
            $selPatternObj.Select()
            return $true
        } catch {
            Write-Error "SelectionItemPattern.Select() failed: $($_.Exception.Message)"
            exit 3
        }
    }

    Write-Error "Element '$($Element.Current.Name)' supports neither Invoke nor SelectionItem pattern."
    exit 2
}

function Get-ShellSelectedNavItem {
    [CmdletBinding()]
    param()

    if (-not $script:ShellRoot) { [void](Get-ShellMainWindow) }

    # Walk descendants for an element with SelectionItemPattern where IsSelected = true
    # and LocalizedControlType in {list item, tab item}.
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    $queue  = New-Object System.Collections.Queue
    $queue.Enqueue(@{ E = $script:ShellRoot; D = 0 })

    while ($queue.Count -gt 0) {
        $node = $queue.Dequeue()
        if ($node.D -gt 6) { continue }

        try {
            $ct = $node.E.Current.LocalizedControlType
        } catch { $ct = $null }

        if ($ct -in @('list item','tab item')) {
            $sel = $null
            if ($node.E.TryGetCurrentPattern(
                    [System.Windows.Automation.SelectionItemPattern]::Pattern,
                    [ref]$sel)) {
                try {
                    if ($sel.Current.IsSelected) {
                        return [pscustomobject]@{
                            Name         = $node.E.Current.Name
                            AutomationId = $node.E.Current.AutomationId
                            ControlType  = $ct
                        }
                    }
                } catch { }
            }
        }

        $c = $walker.GetFirstChild($node.E)
        while ($c) {
            $queue.Enqueue(@{ E = $c; D = $node.D + 1 })
            $c = $walker.GetNextSibling($c)
        }
    }

    return $null
}

function Select-ShellNavTab {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$TabName
    )

    Write-Verbose "Select-ShellNavTab -TabName '$TabName'"
    $el = Find-ShellElement -Name $TabName
    [void](Invoke-ShellElement -Element $el)
    Start-Sleep -Milliseconds 400  # let WinUI 3 swap the Frame content

    $sel = Get-ShellSelectedNavItem
    if ($sel) { return $sel.Name } else { return $null }
}

# ---------------------------------------------------------------------------
# Usage banner (printed once on dot-source / direct run)
# ---------------------------------------------------------------------------
$__usage = @'
Invoke-ShellUiProbe.ps1 loaded. Functions:
  Find-ShellElement               -Name <string>
  Find-ShellElementByAutomationId -Id   <string>
  Invoke-ShellElement             -Element <AutomationElement>
  Get-ShellSelectedNavItem
  Select-ShellNavTab              -TabName <string>

Flags:
  -TestOverview   demo: click "Overview" tab and print selected nav name
  -Verbose        print each step

Exit codes: 1 shell not running | 2 element not found | 3 UIAutomation failure
'@
Write-Host $__usage

# ---------------------------------------------------------------------------
# Optional demo
# ---------------------------------------------------------------------------
if ($TestOverview) {
    Write-Host ''
    Write-Host '--- -TestOverview demo ---'
    [void](Get-ShellMainWindow)

    $before = Get-ShellSelectedNavItem
    $beforeName = if ($before) { $before.Name } else { '<none>' }
    Write-Host "Before invoke : selected = '$beforeName'"

    $newSel = Select-ShellNavTab -TabName 'Overview'
    Write-Host "After  invoke : selected = '$newSel'"

    if ($newSel -eq 'Overview') {
        Write-Host 'RESULT: OK (Overview is selected)'
    } else {
        Write-Host "RESULT: MISMATCH (expected 'Overview', got '$newSel')"
    }
}
```
