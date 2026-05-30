<#
.SYNOPSIS
  MCOS remediation validation helper.
.DESCRIPTION
  Read-only static checks for known remediation regressions.
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

function Assert-FileExists {
    param([string]$RelativePath)
    $path = Resolve-RepoFile $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        $failures.Add("Missing expected file: $RelativePath") | Out-Null
        Write-Log "Missing expected file: $RelativePath" 'FAIL'
        return $false
    }
    return $true
}

function Assert-NoLiteral {
    param([string]$RelativePath, [string]$Literal, [string]$Reason)
    if (-not (Assert-FileExists $RelativePath)) { return }
    $path = Resolve-RepoFile $RelativePath
    $text = Get-Content -LiteralPath $path -Raw -ErrorAction Stop
    if ($text.Contains($Literal)) {
        $message = "Known-bad literal remains in $RelativePath :: $Literal :: $Reason"
        $failures.Add($message) | Out-Null
        Write-Log $message 'FAIL'
    } else {
        Write-Log "Known-bad literal absent: $RelativePath :: $Literal" 'PASS'
    }
}

Assert-NoLiteral 'src/MasterControlApp/MasterControlDefaults.cpp' 'configuration.bindAddress = "0.0.0.0";' 'Fresh install admin bind must be local-only by default.'
Assert-NoLiteral 'src/MasterControlApp/MasterControlDefaults.cpp' 'configuration.beaconEnabled = true;' 'Beacon must not be enabled by default.'
Assert-NoLiteral 'src/MasterControlApp/MasterControlDefaults.cpp' 'configuration.security.allowOpenLanAccess = true;' 'Open LAN access must not be enabled by default.'
Assert-NoLiteral 'src/MasterControlApp/MasterControlDefaults.cpp' 'configuration.mcpGateway.enabled = true;' 'MCP gateway LAN advertisement/start must not be enabled by default without setup validation.'
Assert-NoLiteral 'src/MasterControlApp/MasterControlRuntime.cpp' 'AuthenticatedRequestContext context = makeOperatorContext();' 'Request path must not default remote requests to operator.'
Assert-NoLiteral 'src/MasterControlBaselineToolsWorker/main.cpp' "Select-String -Pattern '" 'Known unsafe PowerShell fallback pattern must be removed or safely replaced.'
Assert-NoLiteral 'src/MasterControlBaselineToolsWorker/main.cpp' 'psBody <<' 'Known unsafe PowerShell command construction must be removed or safely replaced.'
Assert-NoLiteral 'src/MasterControlApp/MasterControlRuntime.cpp' 'commandLine += L" " + wideFromUtf8(arg);' 'Worker args must be safely quoted.'
Assert-NoLiteral 'src/MasterControlBootstrapper/main.cpp' 'WaitForSingleObject(processInformation.hProcess, INFINITE)' 'Bootstrapper child process helpers must not wait forever.'
Assert-NoLiteral 'src/MasterControlBaselineToolsWorker/main.cpp' 'out["workerVersion"] = "0.9.4";' 'Worker version must come from generated VERSION.json header.'
Assert-NoLiteral 'src/MasterControlBaselineToolsWorker/main.cpp' 'http://localhost:7300' 'Worker admin bridge URL must be injected, not hardcoded.'
Assert-NoLiteral 'src/MasterControlApp/MasterControlRuntime.cpp' 'result.workflowsReadyCount = 0;' 'Workflow readiness must not be hard-coded missing.'
Assert-NoLiteral 'src/MasterControlApp/MasterControlRuntime.cpp' 'result.workflowsMissingCount = 1;' 'Workflow readiness must not be hard-coded missing.'
$bodyOptionalLiteral = '// Body is optional ' + [char]0x2014 + ' treat parse errors as empty body.'
Assert-NoLiteral 'src/MasterControlApp/MasterControlRuntime.cpp' $bodyOptionalLiteral 'Setup completion must reject malformed JSON.'

if ($failures.Count -gt 0) {
    Write-Log "Static gate failures: $($failures.Count)" 'ERROR'
    $failures | ForEach-Object { Write-Log $_ 'ERROR' }
    exit 1
}

Write-Log 'All static gates passed.' 'PASS'
exit 0
