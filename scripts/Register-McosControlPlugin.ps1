# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
Registers the bundled mcos-control Claude Code plugin with the current user's
Claude Code installation.

.DESCRIPTION
The MSI installs the plugin source under
    C:\Program Files\Master Control Orchestration Server\share\claude-plugins\mcos-control
This script copies (or symlinks, with -Symlink) it into the per-user Claude Code
plugins directory at
    %USERPROFILE%\.claude\plugins\mcos-control
so Claude Code picks up the bridge MCP server, sub-agents, slash commands, and
skill on next launch.

The plugin gives Claude direct read+write access to the running MCOS via the
admin HTTP API. Trust posture: LAN-trusted (ADR-001 §3 / ADR-002 §1).
Destructive operations require explicit confirm:true. See
share\claude-plugins\mcos-control\README.md.

.PARAMETER Symlink
Use a directory junction instead of a copy. Updates to the bundled plugin
under Program Files (e.g., from an MSI upgrade) are then picked up
automatically without re-running this script. Requires no special privilege
(directory junctions don't need SeCreateSymbolicLinkPrivilege).

.PARAMETER Unregister
Remove the plugin registration from the current user's Claude Code plugins
directory. Does not touch the bundled source under Program Files.

.PARAMETER PluginsDir
Override the destination plugins directory. Default is
%USERPROFILE%\.claude\plugins.

.PARAMETER SourceDir
Override the source directory. Default is the path Program Files installs to.

.EXAMPLE
.\Register-McosControlPlugin.ps1
Copies the plugin into ~/.claude/plugins/mcos-control.

.EXAMPLE
.\Register-McosControlPlugin.ps1 -Symlink
Creates a junction at ~/.claude/plugins/mcos-control pointing at the bundled
source. Future MSI upgrades replace the source in place.

.EXAMPLE
.\Register-McosControlPlugin.ps1 -Unregister
Removes the plugin from ~/.claude/plugins.
#>

[CmdletBinding()]
param(
    [switch]$Symlink,
    [switch]$Unregister,
    [string]$PluginsDir,
    [string]$SourceDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Resolve paths
# ---------------------------------------------------------------------------
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Default source: the share/claude-plugins/mcos-control directory the MSI
# installs alongside this script. If the script lives at the install root,
# the source is one directory deeper.
if (-not $SourceDir) {
    $candidate = Join-Path $scriptDir 'share\claude-plugins\mcos-control'
    if (Test-Path -LiteralPath $candidate -PathType Container) {
        $SourceDir = (Resolve-Path -LiteralPath $candidate).Path
    } else {
        # Fallback: assume we're being run from the repo root (developer use)
        $candidate = Join-Path $scriptDir '..\.claude-plugin\mcos-control'
        if (Test-Path -LiteralPath $candidate -PathType Container) {
            $SourceDir = (Resolve-Path -LiteralPath $candidate).Path
        } else {
            throw "Cannot locate mcos-control plugin source. Tried: $($scriptDir)\share\claude-plugins\mcos-control and $($scriptDir)\..\.claude-plugin\mcos-control. Pass -SourceDir explicitly."
        }
    }
}

if (-not $PluginsDir) {
    $PluginsDir = Join-Path $env:USERPROFILE '.claude\plugins'
}

$DestDir = Join-Path $PluginsDir 'mcos-control'

Write-Host "Source: $SourceDir"
Write-Host "Dest:   $DestDir"
Write-Host ""

# ---------------------------------------------------------------------------
# Unregister mode
# ---------------------------------------------------------------------------
if ($Unregister) {
    if (-not (Test-Path -LiteralPath $DestDir)) {
        Write-Host "Plugin is not currently registered (nothing at $DestDir)."
        exit 0
    }

    # Detect junction so we don't accidentally Remove-Item -Recurse the source
    $item = Get-Item -LiteralPath $DestDir -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        # It's a junction or symlink. Use Remove-Item without -Recurse so we
        # only remove the link itself, not the target.
        Remove-Item -LiteralPath $DestDir -Force
        Write-Host "Removed junction: $DestDir"
    } else {
        Remove-Item -LiteralPath $DestDir -Recurse -Force
        Write-Host "Removed plugin directory: $DestDir"
    }
    Write-Host ""
    Write-Host "Plugin unregistered. Restart Claude Code to drop the tools."
    exit 0
}

# ---------------------------------------------------------------------------
# Validate source
# ---------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
    throw "Source directory not found: $SourceDir"
}
$manifest = Join-Path $SourceDir 'plugin.json'
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Source does not contain a plugin.json manifest at $manifest. The plugin source is corrupted or this is the wrong directory."
}

# ---------------------------------------------------------------------------
# Ensure destination parent exists
# ---------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $PluginsDir)) {
    New-Item -ItemType Directory -Force -Path $PluginsDir | Out-Null
    Write-Host "Created Claude Code plugins directory: $PluginsDir"
}

# ---------------------------------------------------------------------------
# Handle existing destination
# ---------------------------------------------------------------------------
if (Test-Path -LiteralPath $DestDir) {
    $item = Get-Item -LiteralPath $DestDir -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        Write-Host "Existing junction at destination — replacing."
        Remove-Item -LiteralPath $DestDir -Force
    } else {
        Write-Host "Existing plugin directory at destination — replacing."
        Remove-Item -LiteralPath $DestDir -Recurse -Force
    }
}

# ---------------------------------------------------------------------------
# Register
# ---------------------------------------------------------------------------
if ($Symlink) {
    # Use a directory junction (mklink /J equivalent). Junctions do not
    # require admin privilege, unlike symbolic links.
    Write-Host "Creating junction $DestDir -> $SourceDir ..."
    New-Item -ItemType Junction -Path $DestDir -Target $SourceDir | Out-Null
    Write-Host "Junction created. Future MSI upgrades to the source are picked up automatically."
} else {
    Write-Host "Copying $SourceDir -> $DestDir ..."
    Copy-Item -Recurse -Force -Path $SourceDir -Destination $DestDir
    Write-Host "Plugin files copied."
}

# ---------------------------------------------------------------------------
# Smoke check the bridge
# ---------------------------------------------------------------------------
$bridge = Join-Path $DestDir 'mcp-servers\mcos-bridge\server.py'
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) {
    Write-Warning "Plugin registered but bridge server.py is missing at $bridge. Plugin will not function."
    exit 1
}

$python = Get-Command -Name 'py' -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command -Name 'python' -ErrorAction SilentlyContinue
}
if (-not $python) {
    Write-Warning "Python (py / python) is not on PATH. The bridge requires Python 3.9+. Install Python 3 and re-run this script, or open Claude Code anyway and the plugin will fail-fast on first call."
} else {
    Write-Host "Python found at: $($python.Source)"
}

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "===================================================================="
Write-Host " mcos-control Claude Code plugin REGISTERED"
Write-Host "===================================================================="
Write-Host " Source:        $SourceDir"
Write-Host " Registered at: $DestDir"
Write-Host " Mode:          $(if ($Symlink) { 'Junction (auto-updates with MSI)' } else { 'Copy (re-run after MSI upgrade)' })"
Write-Host ""
Write-Host " Next steps:"
Write-Host "   1. Set MCOS_BASE_URL if MCOS is on a different host (default localhost:7300)."
Write-Host "      `$env:MCOS_BASE_URL = 'http://eng-lab-1.local:7300'"
Write-Host ""
Write-Host "   2. Open Claude Code. The bridge auto-loads. Try:"
Write-Host "        /mcos:status"
Write-Host ""
Write-Host "   3. To unregister:"
Write-Host "        $($MyInvocation.MyCommand.Path) -Unregister"
Write-Host ""
Write-Host " Plugin README: $SourceDir\README.md"
Write-Host "===================================================================="
