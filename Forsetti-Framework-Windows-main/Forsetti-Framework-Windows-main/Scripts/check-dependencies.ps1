# Forsetti Framework - Dependency Audit Script (R001, R006)
# Copyright (c) 2026 James Daley. All Rights Reserved.
#
# R001: Native Technologies Only - no Boost, no non-Microsoft third-party.
# R006: One-way dependencies - CMake link targets must follow the layer model.

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$violations = @()

Write-Host "=== Dependency Audit ===" -ForegroundColor Cyan

# --- R001: Forbidden includes ---
Write-Host "`nChecking for forbidden third-party includes..." -ForegroundColor Yellow

$forbiddenPatterns = @(
    @{ Pattern = '#include\s+[<"]boost/';       Label = "Boost" },
    @{ Pattern = '#include\s+[<"]Qt';           Label = "Qt" },
    @{ Pattern = '#include\s+[<"]Poco/';        Label = "Poco" },
    @{ Pattern = '#include\s+[<"]absl/';        Label = "Abseil" },
    @{ Pattern = '#include\s+[<"]fmt/';         Label = "fmtlib" },
    @{ Pattern = '#include\s+[<"]spdlog/';      Label = "spdlog" },
    @{ Pattern = '#include\s+[<"]gtest/';       Label = "Google Test" },
    @{ Pattern = '#include\s+[<"]gmock/';       Label = "Google Mock" },
    @{ Pattern = '#include\s+[<"]catch2/';      Label = "Catch2" },
    @{ Pattern = '#include\s+[<"]doctest/';     Label = "doctest" }
)

$allSourceFiles = Get-ChildItem -Path "$repoRoot\include", "$repoRoot\src" -Recurse -Include "*.h", "*.cpp" -ErrorAction SilentlyContinue

foreach ($file in $allSourceFiles) {
    $content = Get-Content $file.FullName -Raw -ErrorAction SilentlyContinue
    foreach ($fp in $forbiddenPatterns) {
        if ($content -match $fp.Pattern) {
            $rel = $file.FullName.Replace("$repoRoot\", "")
            $violations += "R001: $rel includes $($fp.Label) (forbidden third-party dependency)"
        }
    }
}

# --- R001: vcpkg.json audit ---
Write-Host "Checking vcpkg.json for forbidden dependencies..." -ForegroundColor Yellow

$allowedPackages = @("nlohmann-json")
$vcpkgPath = Join-Path $repoRoot "vcpkg.json"

if (Test-Path $vcpkgPath) {
    $vcpkg = Get-Content $vcpkgPath -Raw | ConvertFrom-Json
    if ($vcpkg.dependencies) {
        foreach ($dep in $vcpkg.dependencies) {
            $depName = if ($dep -is [string]) { $dep } else { $dep.name }
            if ($depName -notin $allowedPackages) {
                $violations += "R001: vcpkg.json contains forbidden dependency '$depName' (only $($allowedPackages -join ', ') allowed)"
            }
        }
    }
}

# --- R006: CMake link target audit ---
Write-Host "Checking CMake link targets for layer violations..." -ForegroundColor Yellow

# ForsettiCore must not link ForsettiPlatform, ForsettiHostTemplate, or ForsettiModulesExample
$coreCMake = Join-Path $repoRoot "src\ForsettiCore\CMakeLists.txt"
if (Test-Path $coreCMake) {
    $content = Get-Content $coreCMake -Raw
    if ($content -match 'target_link_libraries\s*\(\s*ForsettiCore[^)]*ForsettiPlatform') {
        $violations += "R006: ForsettiCore/CMakeLists.txt links ForsettiPlatform (forbidden)"
    }
    if ($content -match 'target_link_libraries\s*\(\s*ForsettiCore[^)]*ForsettiHostTemplate') {
        $violations += "R006: ForsettiCore/CMakeLists.txt links ForsettiHostTemplate (forbidden)"
    }
    if ($content -match 'target_link_libraries\s*\(\s*ForsettiCore[^)]*ForsettiModulesExample') {
        $violations += "R006: ForsettiCore/CMakeLists.txt links ForsettiModulesExample (forbidden)"
    }
}

# ForsettiPlatform must not link ForsettiHostTemplate or ForsettiModulesExample
$platCMake = Join-Path $repoRoot "src\ForsettiPlatform\CMakeLists.txt"
if (Test-Path $platCMake) {
    $content = Get-Content $platCMake -Raw
    if ($content -match 'target_link_libraries\s*\(\s*ForsettiPlatform[^)]*ForsettiHostTemplate') {
        $violations += "R006: ForsettiPlatform/CMakeLists.txt links ForsettiHostTemplate (forbidden)"
    }
    if ($content -match 'target_link_libraries\s*\(\s*ForsettiPlatform[^)]*ForsettiModulesExample') {
        $violations += "R006: ForsettiPlatform/CMakeLists.txt links ForsettiModulesExample (forbidden)"
    }
}

# ForsettiModulesExample must not link ForsettiPlatform or ForsettiHostTemplate
$exCMake = Join-Path $repoRoot "src\ForsettiModulesExample\CMakeLists.txt"
if (Test-Path $exCMake) {
    $content = Get-Content $exCMake -Raw
    if ($content -match 'target_link_libraries\s*\(\s*ForsettiModulesExample[^)]*ForsettiPlatform') {
        $violations += "R006: ForsettiModulesExample/CMakeLists.txt links ForsettiPlatform (forbidden)"
    }
    if ($content -match 'target_link_libraries\s*\(\s*ForsettiModulesExample[^)]*ForsettiHostTemplate') {
        $violations += "R006: ForsettiModulesExample/CMakeLists.txt links ForsettiHostTemplate (forbidden)"
    }
}

# --- Report ---
Write-Host ""
if ($violations.Count -eq 0) {
    Write-Host "All dependency checks passed." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Dependency violations found:" -ForegroundColor Red
    foreach ($v in $violations) {
        Write-Host "  - $v" -ForegroundColor Red
    }
    Write-Host "`n$($violations.Count) violation(s) found." -ForegroundColor Red
    exit 1
}
