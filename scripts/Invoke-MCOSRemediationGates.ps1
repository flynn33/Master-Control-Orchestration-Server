<#
.SYNOPSIS
  Runs MCOS remediation validation gates.
.DESCRIPTION
  Orchestrates static gates and optional release preset build/test commands.
  Safe by default; does not modify source.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$RepoRoot = (Get-Location).Path,

    [Parameter(Mandatory=$false)]
    [switch]$SkipBuild,

    [Parameter(Mandatory=$false)]
    [string]$LogDirectory = (Join-Path (Get-Location).Path 'artifacts\mcos-remediation-gates')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $LogDirectory)) {
    New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
}
$LogPath = Join-Path $LogDirectory ("Invoke-MCOSRemediationGates-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType File -Path $LogPath -Force | Out-Null

function Write-Log {
    param([string]$Message, [string]$Level = 'INFO')
    $line = "[$((Get-Date).ToString('s'))][$Level] $Message"
    Write-Host $line
    Add-Content -LiteralPath $LogPath -Value $line
}

function Invoke-GateScript {
    param([string]$ScriptName)
    $scriptPath = Join-Path $PSScriptRoot $ScriptName
    if (-not (Test-Path -LiteralPath $scriptPath)) {
        Write-Log "Missing gate script: $scriptPath" 'ERROR'
        return 2
    }

    Write-Log "Running gate script: $ScriptName"
    $arguments = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $scriptPath,
        '-RepoRoot', $RepoRoot,
        '-LogDirectory', $LogDirectory
    )
    & powershell.exe @arguments 2>&1 | ForEach-Object { Write-Host $_ }
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Write-Log "Gate script failed: $ScriptName exit=$code" 'ERROR'
    } else {
        Write-Log "Gate script passed: $ScriptName" 'PASS'
    }
    return $code
}

$failures = 0
foreach ($script in @('Test-MCOSSecurityDefaults.ps1', 'Test-MCOSStaticGates.ps1')) {
    $exitCode = Invoke-GateScript -ScriptName $script
    if ($exitCode -ne 0) { $failures++ }
}

if (-not $SkipBuild) {
    Write-Log 'Running optional release build/test gate commands.'
    Push-Location $RepoRoot
    try {
        $commands = @(
            @{ Name = 'cmake-configure-release'; Command = 'cmake'; Args = @('--preset', 'release') },
            @{ Name = 'cmake-build-release'; Command = 'cmake'; Args = @('--build', '--preset', 'release') },
            @{ Name = 'ctest-release'; Command = 'ctest'; Args = @('--preset', 'release', '--output-on-failure') }
        )
        foreach ($cmd in $commands) {
            $commandLine = "$($cmd.Command) $($cmd.Args -join ' ')"
            $commandLog = Join-Path $LogDirectory "$($cmd.Name).log"
            Write-Log "Running $($cmd.Name): $commandLine"
            & $cmd.Command @($cmd.Args) 2>&1 | Tee-Object -FilePath $commandLog
            if ($LASTEXITCODE -ne 0) {
                Write-Log "$($cmd.Name) failed exit=$LASTEXITCODE" 'ERROR'
                $failures++
                break
            }
            Write-Log "$($cmd.Name) passed" 'PASS'
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Log 'SkipBuild specified; build/test/package gates must be run separately or documented as environment-blocked.' 'WARN'
}

if ($failures -gt 0) {
    Write-Log "Total gate failures: $failures" 'ERROR'
    exit 1
}

Write-Log 'All invoked gates passed.' 'PASS'
exit 0
