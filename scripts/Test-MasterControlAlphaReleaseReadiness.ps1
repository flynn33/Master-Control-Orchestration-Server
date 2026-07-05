<#
.SYNOPSIS
  Validates MCOS alpha release-readiness metadata, docs, and product-gate rules.

.DESCRIPTION
  Non-operator-gated self-audit for the A3.11.0 alpha release-readiness
  remediation. The script does not build, package, install, start services,
  change firewall rules, change URL ACLs, publish releases, push, or mutate tags.
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$ExpectedVersion = "A3.11.0",
    [switch]$SkipFetch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path -LiteralPath $RepoRoot).Path
$failures = New-Object System.Collections.Generic.List[string]
$legacyJsonFlag = "--json" + "-output"
$legacyJsonWideLiteral = 'L"--json' + '-output"'
$removedReleaseWorkflow = "release" + ".yml"

function Add-Failure {
    param([Parameter(Mandatory = $true)][string]$Message)
    $script:failures.Add($Message) | Out-Null
}

function Read-Text {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        Add-Failure "Missing required file: $RelativePath"
        return ""
    }
    return [System.IO.File]::ReadAllText($path)
}

function Invoke-Git {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $output = & git -C $root @Arguments 2>&1
    $exit = $LASTEXITCODE
    return [pscustomobject][ordered]@{
        ExitCode = $exit
        Text = (($output | Out-String).Trim())
    }
}

function Get-ActiveTextFiles {
    $activeRoots = @(
        "README.md",
        "CHANGELOG.md",
        "VERSION.json",
        ".github/workflows",
        "docs/wiki",
        "docs/implementation",
        "scripts",
        "installer",
        "src",
        "tests"
    )
    $extensions = @(".md", ".json", ".yml", ".yaml", ".ps1", ".psm1", ".cpp", ".h", ".hpp", ".txt", ".wxs")
    $files = New-Object System.Collections.Generic.List[object]
    foreach ($relative in $activeRoots) {
        $path = Join-Path $root $relative
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path
        if (-not $item.PSIsContainer) {
            $files.Add($item) | Out-Null
            continue
        }
        Get-ChildItem -LiteralPath $path -Recurse -File -Force | Where-Object {
            $extensions -contains $_.Extension.ToLowerInvariant()
        } | ForEach-Object { $files.Add($_) | Out-Null }
    }
    return $files
}

function Get-RelativePath {
    param([Parameter(Mandatory = $true)][string]$FullName)
    return $FullName.Substring($root.Length).TrimStart('\','/')
}

Push-Location $root
try {
    if (-not $SkipFetch) {
        $fetch = Invoke-Git -Arguments @("fetch", "--tags", "--prune")
        if ($fetch.ExitCode -ne 0) {
            Add-Failure "git fetch --tags --prune failed: $($fetch.Text)"
        }
    }

    $versionPath = Join-Path $root "VERSION.json"
    $version = $null
    try {
        $version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
    } catch {
        Add-Failure "VERSION.json parse failure: $($_.Exception.Message)"
    }

    $tagSha = ""
    if ($null -ne $version) {
        if ([string]$version.current_version -ne $ExpectedVersion) {
            Add-Failure "VERSION.json current_version is '$($version.current_version)', expected '$ExpectedVersion'."
        }
        if ([string]::IsNullOrWhiteSpace([string]$version.current_tag)) {
            Add-Failure "VERSION.json current_tag is missing."
        } else {
            $tagResult = Invoke-Git -Arguments @("rev-parse", "$($version.current_tag)^{commit}")
            if ($tagResult.ExitCode -ne 0) {
                Add-Failure "Current tag '$($version.current_tag)' does not resolve locally: $($tagResult.Text)"
            } else {
                $tagSha = $tagResult.Text.Trim()
                if ($tagSha -notmatch "^[0-9a-f]{40}$") {
                    Add-Failure "Current tag '$($version.current_tag)' resolved to a non-full SHA: $tagSha"
                }
            }
        }

        if ($tagSha) {
            if ([string]$version.last_release_commit -ne $tagSha) {
                Add-Failure "VERSION.json last_release_commit '$($version.last_release_commit)' does not match tag SHA '$tagSha'."
            }
            $currentEntry = @($version.history | Where-Object { [string]$_.version -eq [string]$version.current_version } | Select-Object -First 1)
            if ($currentEntry.Count -eq 0) {
                Add-Failure "VERSION.json has no history entry for current_version '$($version.current_version)'."
            } else {
                $entryCommit = [string]$currentEntry[0].commit
                if ($entryCommit -ne $tagSha) {
                    Add-Failure "VERSION.json current history commit '$entryCommit' does not match tag SHA '$tagSha'."
                }
                if ($entryCommit -eq "pending") {
                    Add-Failure "VERSION.json current history commit is still pending."
                }
                if ($entryCommit -and $entryCommit -notmatch "^[0-9a-f]{40}$") {
                    Add-Failure "VERSION.json current history commit is not a full SHA: $entryCommit"
                }
            }
        }
    }

    $activeFiles = @(Get-ActiveTextFiles)
    $stalePhrases = @(
        ("No GitHub releases are cut during " + "alpha"),
        ("no GitHub releases are cut during " + "alpha"),
        ("nothing has been released during " + "alpha"),
        ("GitHub Releases are deferred until MCOS leaves " + "alpha")
    )
    foreach ($file in $activeFiles) {
        $relative = Get-RelativePath -FullName $file.FullName
        $text = [System.IO.File]::ReadAllText($file.FullName)
        foreach ($phrase in $stalePhrases) {
            if ($text.Contains($phrase)) {
                Add-Failure "Stale alpha release-policy phrase remains in ${relative}: '$phrase'"
            }
        }
    }

    foreach ($file in $activeFiles) {
        $relative = Get-RelativePath -FullName $file.FullName
        $lineNumber = 0
        foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
            $lineNumber++
            if ($line -notmatch [regex]::Escape($removedReleaseWorkflow)) { continue }
            $allowed = ($line -match "(?i)removed|not part of the current|not current|not part of current|removed at A3\.11\.0")
            if (-not $allowed) {
                Add-Failure "Stale removed release workflow current-policy reference in ${relative}:$lineNumber"
            }
        }
    }

    $jsonOutputPaths = @("README.md", "docs/wiki", "scripts", ".github/workflows", "installer", "tests")
    foreach ($relativeRoot in $jsonOutputPaths) {
        $path = Join-Path $root $relativeRoot
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $items = @()
        $item = Get-Item -LiteralPath $path
        if ($item.PSIsContainer) {
            $items = @(Get-ChildItem -LiteralPath $path -Recurse -File -Force)
        } else {
            $items = @($item)
        }
        foreach ($file in $items) {
            if ($file.Extension -in @(".png", ".ico", ".bmp", ".dll", ".exe", ".pdb", ".lib", ".obj")) { continue }
            $relative = Get-RelativePath -FullName $file.FullName
            $text = [System.IO.File]::ReadAllText($file.FullName)
            if ($text.Contains($legacyJsonFlag)) {
                Add-Failure "Active docs/scripts/workflows/tests still invoke the legacy bootstrapper JSON flag in $relative"
            }
        }
    }

    $workflow = Read-Text ".github/workflows/windows-build-test-package.yml"
    if ($workflow -match "(?m)^\s+workflow_dispatch\s*:") {
        Add-Failure "Product gate must not contain workflow_dispatch."
    }
    foreach ($token in @(
        "MasterControlBootstrapper.exe",
        "Write-Error",
        "exit 1",
        '$LASTEXITCODE',
        "ConvertFrom-Json",
        "ready",
        "--skip-service",
        "--skip-firewall",
        "--skip-uninstall-registration",
        "--json",
        "bootstrapper-staged-preflight.json",
        '$payloadDir'
    )) {
        if (-not $workflow.Contains($token)) {
            Add-Failure "Product gate missing required staged-preflight token: $token"
        }
    }
    if ($workflow -match "(?i)skipping preflight") {
        Add-Failure "Product gate still contains skip-style bootstrapper preflight language."
    }
    if ($workflow -notmatch 'preflight\s+\$payloadDir') {
        Add-Failure "Product gate does not appear to run preflight against the staged payload directory."
    }

    $bootstrapperSource = Read-Text "src/MasterControlBootstrapper/main.cpp"
    if (-not $bootstrapperSource.Contains('L"--json"')) {
        Add-Failure "Bootstrapper source no longer supports canonical --json."
    }
    $aliasPattern = 'L"--json"\s*\|\|\s*argument\s*==\s*' + [regex]::Escape($legacyJsonWideLiteral)
    if ($bootstrapperSource.Contains($legacyJsonWideLiteral) -and -not ($bootstrapperSource -match $aliasPattern)) {
        Add-Failure "Bootstrapper legacy JSON alias exists but does not map through the same option branch as --json."
    }

    foreach ($requiredScript in @(
        "scripts/Test-MasterControlOrchestrationServerDeployedRuntime.ps1",
        "scripts/Test-MasterControlLanDiscoveryFromPeer.ps1"
    )) {
        $scriptText = Read-Text $requiredScript
        if (-not $scriptText.Contains("[switch]`$Strict")) {
            Add-Failure "$requiredScript must expose a -Strict switch."
        }
        if (-not $scriptText.Contains("ConvertTo-Json") -or -not $scriptText.Contains(".md")) {
            Add-Failure "$requiredScript must emit JSON and Markdown evidence."
        }
        if ($scriptText -match "(?i)Start-Service|Stop-Service|Restart-Service|New-NetFirewallRule|Remove-NetFirewallRule|netsh\s+http\s+add|msiexec\s+/i|msiexec\s+/uninstall") {
            Add-Failure "$requiredScript appears to contain mutating host operations."
        }
    }

    if ($failures.Count -gt 0) {
        Write-Error ("MCOS alpha release-readiness validation failed:`n" + ($failures -join "`n"))
        exit 1
    }

    Write-Host "MCOS alpha release-readiness validation passed."
    if ($tagSha) { Write-Host "Release tag SHA: $tagSha" }
    exit 0
} finally {
    Pop-Location
}
