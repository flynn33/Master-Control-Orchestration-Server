# Master Control Program - Forsetti compliance guardrails
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$violations = @()

Write-Host "=== Master Control Forsetti Compliance ===" -ForegroundColor Cyan

$requiredModuleIDs = @(
    "com.mastercontrol.environment-discovery",
    "com.mastercontrol.host-telemetry",
    "com.mastercontrol.runtime-inventory",
    "com.mastercontrol.configuration",
    "com.mastercontrol.installer-import",
    "com.mastercontrol.provider-integration",
    "com.mastercontrol.export",
    "com.mastercontrol.beacon-gateway",
    "com.mastercontrol.dashboard-ui"
)

function Assert-Contains {
    param(
        [string]$Content,
        [string]$Pattern,
        [string]$Message
    )

    if ($Content -notmatch $Pattern) {
        $script:violations += $Message
    }
}

function Assert-NotContains {
    param(
        [string]$Content,
        [string]$Pattern,
        [string]$Message
    )

    if ($Content -match $Pattern) {
        $script:violations += $Message
    }
}

$topLevelCMake = Join-Path $repoRoot "CMakeLists.txt"
$topLevelContent = Get-Content $topLevelCMake -Raw
Assert-Contains $topLevelContent 'add_subdirectory\(src/MasterControlModules\)' "Top-level CMake must add src/MasterControlModules."
Assert-Contains $topLevelContent 'ForsettiManifests' "Top-level install must stage ForsettiManifests."

$modulesCMakePath = Join-Path $repoRoot "src\MasterControlModules\CMakeLists.txt"
if (-not (Test-Path $modulesCMakePath)) {
    $violations += "src/MasterControlModules/CMakeLists.txt is missing."
} else {
    $modulesCMake = Get-Content $modulesCMakePath -Raw
    Assert-Contains $modulesCMake 'add_library\(MasterControlModules\s+STATIC' "MasterControlModules must be a static library."
    Assert-Contains $modulesCMake 'target_link_libraries\(MasterControlModules\s+PUBLIC\s+ForsettiCore' "MasterControlModules must link ForsettiCore."
    Assert-NotContains $modulesCMake 'ForsettiPlatform' "MasterControlModules must not link ForsettiPlatform."
    Assert-NotContains $modulesCMake 'ForsettiHostTemplate' "MasterControlModules must not link ForsettiHostTemplate."
}

$appCMakePath = Join-Path $repoRoot "src\MasterControlApp\CMakeLists.txt"
$appCMake = Get-Content $appCMakePath -Raw
Assert-Contains $appCMake 'target_link_libraries\(MasterControlApp\s+PUBLIC\s+MasterControlModules' "MasterControlApp must link MasterControlModules."
Assert-Contains $appCMake 'ForsettiPlatform' "MasterControlApp must link ForsettiPlatform."
Assert-NotContains $appCMake 'MasterControlModules\.cpp' "MasterControlApp must not compile MasterControlModules.cpp directly."

$manifestDirs = Get-ChildItem -Path (Join-Path $repoRoot "src") -Recurse -Directory -Filter "ForsettiManifests" -ErrorAction SilentlyContinue
$manifestFiles = @()
foreach ($dir in $manifestDirs) {
    $manifestFiles += Get-ChildItem -Path $dir.FullName -Filter "*.json" -ErrorAction SilentlyContinue
}

if ($manifestFiles.Count -eq 0) {
    $violations += "No Master Control manifest files were found under src/**/Resources/ForsettiManifests."
} else {
    $seenModuleIDs = @{}
    $requiredFields = @("schemaVersion", "moduleID", "displayName", "moduleVersion", "moduleType", "supportedPlatforms", "minForsettiVersion", "entryPoint")
    $validModuleTypes = @("service", "ui", "app")
    $validCapabilities = @("networking", "storage", "secure_storage", "file_export", "telemetry", "routing_overlay", "toolbar_items", "view_injection", "ui_theme_mask", "event_publishing")

    foreach ($file in $manifestFiles) {
        $manifest = Get-Content $file.FullName -Raw | ConvertFrom-Json
        $rel = $file.FullName.Replace("$repoRoot\", "")

        foreach ($field in $requiredFields) {
            $value = $manifest.PSObject.Properties[$field]
            if (-not $value -or $null -eq $value.Value) {
                $violations += "$rel - Missing required field '$field'"
            }
        }

        if ($manifest.schemaVersion -ne "1.0") {
            $violations += "$rel - schemaVersion must be '1.0'"
        }
        if ($manifest.moduleType -notin $validModuleTypes) {
            $violations += "$rel - Invalid moduleType '$($manifest.moduleType)'"
        }
        if ("Windows" -notin @($manifest.supportedPlatforms)) {
            $violations += "$rel - supportedPlatforms must include 'Windows'"
        }
        if ($manifest.capabilitiesRequested) {
            foreach ($capability in $manifest.capabilitiesRequested) {
                if ($capability -notin $validCapabilities) {
                    $violations += "$rel - Unknown capability '$capability'"
                }
            }
        }
        if ($seenModuleIDs.ContainsKey($manifest.moduleID)) {
            $violations += "$rel - Duplicate moduleID '$($manifest.moduleID)'"
        } else {
            $seenModuleIDs[$manifest.moduleID] = $file.FullName
        }
    }

    foreach ($requiredModuleID in $requiredModuleIDs) {
        if (-not $seenModuleIDs.ContainsKey($requiredModuleID)) {
            $violations += "Required module manifest '$requiredModuleID' is missing."
        }
    }
}

Write-Host ""
if ($violations.Count -eq 0) {
    Write-Host "Master Control Forsetti checks passed." -ForegroundColor Green
    exit 0
}

Write-Host "Master Control Forsetti violations found:" -ForegroundColor Red
foreach ($violation in $violations) {
    Write-Host "  - $violation" -ForegroundColor Red
}
Write-Host ""
Write-Host "$($violations.Count) violation(s) found." -ForegroundColor Red
exit 1
