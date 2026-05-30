<#
.SYNOPSIS
  MCOS remediation validation helper.
.DESCRIPTION
  Read-only static checks for safe fresh-install security defaults.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$RepoRoot = (Get-Location).Path,

    [Parameter(Mandatory=$false)]
    [string]$LogDirectory = (Join-Path (Get-Location).Path 'artifacts\mcos-remediation-gates')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function New-LogDirectory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Write-Log {
    param(
        [Parameter(Mandatory=$true)][string]$Message,
        [ValidateSet('INFO','WARN','ERROR','PASS','FAIL')][string]$Level = 'INFO'
    )
    $timestamp = (Get-Date).ToString('s')
    $line = "[$timestamp][$Level] $Message"
    Write-Host $line
    Add-Content -LiteralPath $script:LogPath -Value $line
}

function Resolve-RepoFile {
    param([Parameter(Mandatory=$true)][string]$RelativePath)
    return Join-Path $RepoRoot $RelativePath
}

New-LogDirectory -Path $LogDirectory
$script:LogPath = Join-Path $LogDirectory ("{0}-{1}.log" -f $MyInvocation.MyCommand.Name, (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType File -Path $script:LogPath -Force | Out-Null
Write-Log "RepoRoot=$RepoRoot"
Write-Log "LogDirectory=$LogDirectory"

if (-not (Test-Path -LiteralPath $RepoRoot)) {
    Write-Log "RepoRoot does not exist: $RepoRoot" 'ERROR'
    exit 2
}

$failures = New-Object System.Collections.Generic.List[string]
$defaultsPath = Resolve-RepoFile 'src/MasterControlApp/MasterControlDefaults.cpp'
$modelsPath = Resolve-RepoFile 'include/MasterControl/MasterControlModels.h'

if (-not (Test-Path -LiteralPath $defaultsPath)) {
    Write-Log "Missing defaults file: $defaultsPath" 'ERROR'
    exit 2
}

$text = Get-Content -LiteralPath $defaultsPath -Raw

$knownBad = @(
    @{ Pattern = 'configuration.bindAddress = "0.0.0.0";'; Reason = 'admin/API bind default must be local-only' },
    @{ Pattern = 'configuration.beaconEnabled = true;'; Reason = 'beacon must be disabled by default' },
    @{ Pattern = 'configuration.security.enableAuthentication = false;'; Reason = 'authentication may not be globally disabled without local-only bootstrap semantics' },
    @{ Pattern = 'configuration.security.allowTroubleshootingBypass = true;'; Reason = 'troubleshooting bypass must not be broadly enabled by default' },
    @{ Pattern = 'configuration.security.allowOpenLanAccess = true;'; Reason = 'open LAN access must be disabled by default' },
    @{ Pattern = 'configuration.mcpGateway.enabled = true;'; Reason = 'MCP gateway must not be LAN-advertised by default before setup validation' },
    @{ Pattern = 'configuration.mcpGateway.listenHost = "0.0.0.0";'; Reason = 'MCP gateway must not bind all interfaces by default unless LAN mode explicitly enabled' },
    @{ Pattern = 'configuration.mcpGateway.mode = "lan-trusted";'; Reason = 'LAN-trusted must not be the implicit fresh-install default' }
)

foreach ($item in $knownBad) {
    if ($text.Contains($item.Pattern)) {
        $message = "Known unsafe default remains: $($item.Pattern) :: $($item.Reason)"
        $failures.Add($message) | Out-Null
        Write-Log $message 'FAIL'
    } else {
        Write-Log "Unsafe default pattern absent: $($item.Pattern)" 'PASS'
    }
}

if (Test-Path -LiteralPath $modelsPath) {
    $models = Get-Content -LiteralPath $modelsPath -Raw
    if ($models.Contains('bool allowOpenLanAccess = true;')) {
        $message = 'SecuritySettings.allowOpenLanAccess still defaults true in model.'
        $failures.Add($message) | Out-Null
        Write-Log $message 'FAIL'
    }
}

if ($failures.Count -gt 0) {
    Write-Log "Security default failures: $($failures.Count)" 'ERROR'
    exit 1
}

Write-Log 'Security default static checks passed.' 'PASS'
exit 0
