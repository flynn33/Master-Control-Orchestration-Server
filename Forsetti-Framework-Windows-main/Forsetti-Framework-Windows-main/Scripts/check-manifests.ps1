# Forsetti Framework - Module Manifest Validation Script
# Copyright (c) 2026 James Daley. All Rights Reserved.
#
# Validates all ForsettiManifests JSON files for correct schema,
# required fields, and naming conventions.

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$violations = @()
$seenModuleIDs = @{}

Write-Host "=== Manifest Validation ===" -ForegroundColor Cyan

$requiredFields = @("schemaVersion", "moduleID", "displayName", "moduleVersion", "moduleType", "supportedPlatforms", "minForsettiVersion", "entryPoint")
$validModuleTypes = @("service", "ui", "app")
$validCapabilities = @("networking", "storage", "secure_storage", "file_export", "telemetry", "routing_overlay", "toolbar_items", "view_injection", "ui_theme_mask", "event_publishing")

# Find all manifest JSON files
$manifestDirs = Get-ChildItem -Path "$repoRoot\src" -Recurse -Directory -Filter "ForsettiManifests" -ErrorAction SilentlyContinue
$manifestFiles = @()
foreach ($dir in $manifestDirs) {
    $manifestFiles += Get-ChildItem -Path $dir.FullName -Filter "*.json" -ErrorAction SilentlyContinue
}

if ($manifestFiles.Count -eq 0) {
    Write-Host "No manifest files found." -ForegroundColor Yellow
    exit 0
}

Write-Host "Found $($manifestFiles.Count) manifest file(s).`n" -ForegroundColor Yellow

foreach ($file in $manifestFiles) {
    $rel = $file.FullName.Replace("$repoRoot\", "")
    Write-Host "Checking: $rel" -ForegroundColor Gray

    # Parse JSON
    try {
        $manifest = Get-Content $file.FullName -Raw | ConvertFrom-Json
    } catch {
        $violations += "$rel - Invalid JSON: $($_.Exception.Message)"
        continue
    }

    # Check required fields
    foreach ($field in $requiredFields) {
        $value = $manifest.PSObject.Properties[$field]
        if (-not $value -or $null -eq $value.Value) {
            $violations += "$rel - Missing required field: '$field'"
        }
    }

    # Validate schemaVersion
    if ($manifest.schemaVersion -and $manifest.schemaVersion -ne "1.0") {
        $violations += "$rel - schemaVersion must be '1.0', got '$($manifest.schemaVersion)'"
    }

    # Validate moduleType
    if ($manifest.moduleType -and $manifest.moduleType -notin $validModuleTypes) {
        $violations += "$rel - Invalid moduleType '$($manifest.moduleType)' (must be one of: $($validModuleTypes -join ', '))"
    }

    # Validate supportedPlatforms includes "windows"
    if ($manifest.supportedPlatforms) {
        $platforms = @($manifest.supportedPlatforms)
        if ("windows" -notin $platforms) {
            $violations += "$rel - supportedPlatforms must include 'windows'"
        }
    }

    # Validate capabilitiesRequested (if present)
    if ($manifest.capabilitiesRequested) {
        foreach ($cap in $manifest.capabilitiesRequested) {
            if ($cap -notin $validCapabilities) {
                $violations += "$rel - Unknown capability '$cap' (valid: $($validCapabilities -join ', '))"
            }
        }
    }

    # Check for duplicate moduleID
    if ($manifest.moduleID) {
        if ($seenModuleIDs.ContainsKey($manifest.moduleID)) {
            $violations += "$rel - Duplicate moduleID '$($manifest.moduleID)' (also in $($seenModuleIDs[$manifest.moduleID]))"
        } else {
            $seenModuleIDs[$manifest.moduleID] = $rel
        }
    }

    # Validate moduleVersion is an object with major/minor/patch
    if ($manifest.moduleVersion) {
        $mv = $manifest.moduleVersion
        if ($null -eq $mv.major -or $null -eq $mv.minor -or $null -eq $mv.patch) {
            $violations += "$rel - moduleVersion must have 'major', 'minor', and 'patch' fields"
        }
    }

    # Validate minForsettiVersion is an object with major/minor/patch
    if ($manifest.minForsettiVersion) {
        $mfv = $manifest.minForsettiVersion
        if ($null -eq $mfv.major -or $null -eq $mfv.minor -or $null -eq $mfv.patch) {
            $violations += "$rel - minForsettiVersion must have 'major', 'minor', and 'patch' fields"
        }
    }

    # Check key naming convention (camelCase)
    $props = $manifest.PSObject.Properties | Select-Object -ExpandProperty Name
    foreach ($prop in $props) {
        if ($prop -match '_') {
            $violations += "$rel - Key '$prop' uses snake_case (manifests must use camelCase)"
        }
    }
}

# --- Report ---
Write-Host ""
if ($violations.Count -eq 0) {
    Write-Host "All manifest checks passed ($($manifestFiles.Count) file(s) validated)." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Manifest violations found:" -ForegroundColor Red
    foreach ($v in $violations) {
        Write-Host "  - $v" -ForegroundColor Red
    }
    Write-Host "`n$($violations.Count) violation(s) found." -ForegroundColor Red
    exit 1
}
