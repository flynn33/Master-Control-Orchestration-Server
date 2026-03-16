# Forsetti Framework — Windows Guardrails Verification Script
# Copyright (c) 2026 James Daley. All Rights Reserved.
#
# Usage: .\Scripts\verify-forsetti-guardrails.ps1
# Runs from the repository root directory.

$ErrorActionPreference = "Stop"

Write-Host "=== Forsetti Guardrails Verification ===" -ForegroundColor Cyan

# 1. Configure
Write-Host "`n[1/4] Configuring CMake..." -ForegroundColor Yellow
cmake --preset debug
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed"; exit 1 }

# 2. Build
Write-Host "`n[2/4] Building..." -ForegroundColor Yellow
cmake --build --preset debug
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

# 3. Test
Write-Host "`n[3/4] Running tests..." -ForegroundColor Yellow
ctest --preset debug --output-on-failure
if ($LASTEXITCODE -ne 0) { Write-Error "Tests failed"; exit 1 }

# 4. Architecture enforcement
Write-Host "`n[4/4] Checking architecture..." -ForegroundColor Yellow
& "$PSScriptRoot\check-architecture.ps1"
if ($LASTEXITCODE -ne 0) { Write-Error "Architecture check failed"; exit 1 }

Write-Host "`n=== All guardrails passed ===" -ForegroundColor Green
