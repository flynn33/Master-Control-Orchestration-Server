# Master Control Orchestration Server - Forsetti compliance guardrails
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

Set-StrictMode -Version Latest
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
    "com.mastercontrol.export",
    "com.mastercontrol.command-logic-unit",
    "com.mastercontrol.gateway-windows",
    "com.mastercontrol.gateway-macos",
    "com.mastercontrol.gateway-ios",
    "com.mastercontrol.governance-windows",
    "com.mastercontrol.governance-macos",
    "com.mastercontrol.governance-ios",
    "com.mastercontrol.beacon-gateway",
    "com.mastercontrol.dashboard-ui"
)

$cluManifestPath = Join-Path $repoRoot "src\MasterControlModules\Resources\ForsettiManifests\CommandLogicUnitModule.json"

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

$shellWindowXamlPath = Join-Path $repoRoot "src\MasterControlShell\MainWindow.xaml"
$shellWindowCppPath = Join-Path $repoRoot "src\MasterControlShell\MainWindow.xaml.cpp"
$shellRuntimePath = Join-Path $repoRoot "src\MasterControlShell\ShellRuntime.cpp"
$contractsPath = Join-Path $repoRoot "include\MasterControl\MasterControlContracts.h"
$modelsPath = Join-Path $repoRoot "include\MasterControl\MasterControlModels.h"
$modulesCppPath = Join-Path $repoRoot "src\MasterControlModules\MasterControlModules.cpp"
$runtimePath = Join-Path $repoRoot "src\MasterControlApp\MasterControlRuntime.cpp"
$webIndexPath = Join-Path $repoRoot "resources\web\index.html"
$webAppPath = Join-Path $repoRoot "resources\web\app.js"

if (-not (Test-Path $shellWindowXamlPath)) {
    $violations += "src/MasterControlShell/MainWindow.xaml is missing."
} else {
    $shellWindowXaml = Get-Content $shellWindowXamlPath -Raw
    Assert-Contains $shellWindowXaml 'SectionContentHost' "MainWindow.xaml must expose a dynamic Forsetti section content host."
    Assert-NotContains $shellWindowXaml '<NavigationView\.MenuItems>' "MainWindow.xaml must not hardcode NavigationView.MenuItems."
}

if (-not (Test-Path $shellWindowCppPath)) {
    $violations += "src/MasterControlShell/MainWindow.xaml.cpp is missing."
} else {
    $shellWindowCpp = Get-Content $shellWindowCppPath -Raw
    Assert-Contains $shellWindowCpp 'ApplySurfaceNavigation' "MainWindow.xaml.cpp must rebuild navigation from Forsetti surface state."
    Assert-Contains $shellWindowCpp 'ApplySurfaceToolbar' "MainWindow.xaml.cpp must rebuild the toolbar from Forsetti surface state."
    Assert-Contains $shellWindowCpp 'ResolvePrimaryViewForDestination' "MainWindow.xaml.cpp must resolve section content from Forsetti view injections."
    Assert-Contains $shellWindowCpp 'OpenOverlayRouteAsync' "MainWindow.xaml.cpp must host Forsetti overlay routes."
    Assert-Contains $shellWindowCpp 'dashboard-clu' "MainWindow.xaml.cpp must expose a CLU bootstrap toolbar fallback."
    Assert-Contains $shellWindowCpp 'clu-nav' "MainWindow.xaml.cpp must expose a CLU bootstrap navigation fallback."
    Assert-Contains $shellWindowCpp 'clu-surface' "MainWindow.xaml.cpp must expose a CLU bootstrap view injection fallback."
}

if (-not (Test-Path $shellRuntimePath)) {
    $violations += "src/MasterControlShell/ShellRuntime.cpp is missing."
} else {
    $shellRuntime = Get-Content $shellRuntimePath -Raw
    Assert-Contains $shellRuntime 'viewInjectionsBySlot' "ShellRuntime.cpp must parse Forsetti view injection surface state."
    Assert-Contains $shellRuntime 'overlayRoutes' "ShellRuntime.cpp must parse Forsetti overlay route surface state."
    Assert-Contains $shellRuntime 'toolbarItems' "ShellRuntime.cpp must parse Forsetti toolbar surface state."
}

if (-not (Test-Path $contractsPath)) {
    $violations += "include/MasterControl/MasterControlContracts.h is missing."
} else {
    $contracts = Get-Content $contractsPath -Raw
    Assert-Contains $contracts 'IModuleControlSurfaceService' "MasterControlContracts.h must define a framework control-surface registry service."
    Assert-Contains $contracts 'upsertControlSurfaceRequest' "MasterControlContracts.h must let modules publish control-surface requests through the framework."
    Assert-Contains $contracts 'removeControlSurfaceRequestsForModule' "MasterControlContracts.h must let the framework remove all control requests for a module during lifecycle changes."
    Assert-Contains $contracts 'IPlatformServiceCatalogService' "MasterControlContracts.h must define a platform gateway and governance catalog service."
    Assert-Contains $contracts 'IPlatformGovernanceToolService' "MasterControlContracts.h must define a platform governance tool catalog and execution service."
}

if (-not (Test-Path $modelsPath)) {
    $violations += "include/MasterControl/MasterControlModels.h is missing."
} else {
    $models = Get-Content $modelsPath -Raw
    Assert-Contains $models 'overlayRouteId' "MasterControlModels.h must let modules describe overlay routing requirements through the framework control contract."
    Assert-Contains $models 'registeredControlSurfaceRequests' "MasterControlModels.h must expose registered control-surface requests in the runtime surface snapshot."
    Assert-Contains $models 'publishedByModuleId' "MasterControlModels.h must expose which UI module registered the composed framework surface."
    Assert-Contains $models 'PlatformGatewayDescriptor' "MasterControlModels.h must expose platform gateway descriptors through the shared data model."
    Assert-Contains $models 'GovernanceServerDescriptor' "MasterControlModels.h must expose platform governance server descriptors through the shared data model."
}

if (-not (Test-Path $webIndexPath)) {
    $violations += "resources/web/index.html is missing."
} else {
    $webIndex = Get-Content $webIndexPath -Raw
    Assert-Contains $webIndex 'id="surfaceToolbar"' "resources/web/index.html must expose a Forsetti surface toolbar host."
    Assert-Contains $webIndex 'id="surfaceNavigation"' "resources/web/index.html must expose a Forsetti surface navigation host."
    Assert-Contains $webIndex 'id="surfaceContentHost"' "resources/web/index.html must expose a Forsetti surface content host."
    Assert-Contains $webIndex 'id="surfaceOverlayDialog"' "resources/web/index.html must expose a Forsetti overlay host."
    Assert-NotContains $webIndex 'id="telemetryGrid"' "resources/web/index.html must not hardcode legacy telemetry sections."
    Assert-NotContains $webIndex 'id="endpointTable"' "resources/web/index.html must not hardcode legacy runtime tables."
}

if (-not (Test-Path $webAppPath)) {
    $violations += "resources/web/app.js is missing."
} else {
    $webApp = Get-Content $webAppPath -Raw

    # PHASE-05 (ADR-002 §6) compliance update: ADR-001 §1's browser
    # rebuild dropped the provider-era Forsetti surface bootstrap in
    # favor of a hand-authored LAN-client dashboard. The earlier
    # Forsetti-bootstrap assertions (ensureBootstrapSurface,
    # renderSurfaceNavigation, renderSurfaceToolbar,
    # resolvePrimaryViewForDestination, openOverlayRoute,
    # CommandLogicUnitSectionView) are retired here. PHASE-09 (Tron
    # dashboard realignment) will reintroduce a gateway-first dashboard;
    # at that point, the assertions for the new surface land here.
    #
    # Until then, the rule is content-shape: app.js must remain the
    # post-ADR-001 LAN-client dashboard (no Forsetti-surface hardcoding,
    # no provider-era surfaces).
    Assert-NotContains $webApp "dashboard-clu" "resources/web/app.js must not hardcode the CLU toolbar item in bootstrap surface data."
    Assert-NotContains $webApp "clu-nav" "resources/web/app.js must not hardcode the CLU navigation pointer in bootstrap surface data."
    Assert-NotContains $webApp "clu-surface" "resources/web/app.js must not hardcode the CLU view injection in bootstrap surface data."

    # PHASE-01 supersession: provider-era browser surfaces stay forbidden.
    Assert-NotContains $webApp "renderSignInCards" "resources/web/app.js must not host the provider-era sign-in cards (PHASE-01)."
    Assert-NotContains $webApp "/api/providers" "resources/web/app.js must not call the provider-era API."
}

if (-not (Test-Path $modulesCppPath)) {
    $violations += "src/MasterControlModules/MasterControlModules.cpp is missing."
} else {
    $modulesCpp = Get-Content $modulesCppPath -Raw
    Assert-Contains $modulesCpp 'CommandLogicUnitModule::descriptor' "MasterControlModules.cpp must define the CLU service module."
    Assert-Contains $modulesCpp 'upsertControlSurfaceRequest' "MasterControlModules.cpp must let CLU publish control-surface requests through the framework registry."
    Assert-Contains $modulesCpp 'GovernanceSection' "MasterControlModules.cpp must describe CLU control needs as a control-surface request, not a raw UI contribution payload."
    Assert-Contains $modulesCpp 'publishDashboardSurface' "DashboardUIModule must own the final framework surface publication."
    Assert-Contains $modulesCpp 'makeEnvironmentDiscoveryControlSurfaceRequests' "EnvironmentDiscoveryModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeHostTelemetryControlSurfaceRequests' "HostTelemetryModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeRuntimeInventoryControlSurfaceRequests' "RuntimeInventoryModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeConfigurationControlSurfaceRequests' "ConfigurationModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeInstallerImportControlSurfaceRequests' "InstallerImportModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeExportControlSurfaceRequests' "ExportModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeWindowsGatewayControlSurfaceRequests' "WindowsGatewayModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeMacGatewayControlSurfaceRequests' "MacGatewayModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeIOSGatewayControlSurfaceRequests' "IOSGatewayModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeWindowsGovernanceControlSurfaceRequests' "Windows governance module must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeMacGovernanceControlSurfaceRequests' "Mac governance module must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeIOSGovernanceControlSurfaceRequests' "iOS governance module must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'registerPlatformGateway' "Platform gateway modules must publish their LAN service descriptors through the framework."
    Assert-Contains $modulesCpp 'registerGovernanceServer' "Governance server modules must publish their MCP server descriptors through the framework."
    Assert-Contains $modulesCpp 'registerGovernanceTools' "Governance server modules must publish their governance tool descriptors through the framework."
    Assert-Contains $modulesCpp 'makeBeaconGatewayControlSurfaceRequests' "BeaconGatewayModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'return Forsetti::UIContributions{};' "DashboardUIModule must bootstrap from framework control requests instead of shipping hardcoded UI contributions."
    Assert-Contains $modulesCpp 'mastercontrol.dashboard.surface.registered' "DashboardUIModule must publish startup surface registration metadata."
    Assert-NotContains $modulesCpp 'makeCommandLogicUnitSurfaceContributions' "CLU must not build raw UI contribution payloads directly."
}

if (-not (Test-Path $runtimePath)) {
    $violations += "src/MasterControlApp/MasterControlRuntime.cpp is missing."
} else {
    $runtime = Get-Content $runtimePath -Raw
    Assert-Contains $runtime 'class FileBackedEntitlementProvider' "MasterControlRuntime.cpp must use a reconciled Forsetti entitlement provider."
    Assert-Contains $runtime 'buildDefaultEntitlementStateDocument' "MasterControlRuntime.cpp must seed entitlement state for runtime reconciliation."
    Assert-Contains $runtime 'paths_.entitlementsFile' "MasterControlRuntime.cpp must use the app entitlement state file."
    Assert-Contains $runtime 'PlatformServiceCatalogService' "MasterControlRuntime.cpp must host the platform gateway and governance catalog."
    Assert-Contains $runtime 'PlatformGovernanceToolService' "MasterControlRuntime.cpp must host the platform governance tool service."
    Assert-Contains $runtime 'DnsServiceRegister' "MasterControlRuntime.cpp must advertise platform gateway services on the LAN."
    Assert-Contains $runtime '/api/clu/tools' "MasterControlRuntime.cpp must expose governance tool catalog endpoints."
    Assert-Contains $runtime '/api/clu/execute' "MasterControlRuntime.cpp must expose governance tool execution endpoints."
    Assert-Contains $runtime '/api/platform-services/config/' "MasterControlRuntime.cpp must expose platform-specific client configuration endpoints."
    Assert-Contains $runtime '/mcp/gateway/' "MasterControlRuntime.cpp must expose platform-specific gateway routes."
    Assert-Contains $runtime '/mcp/governance/' "MasterControlRuntime.cpp must expose platform-specific governance MCP routes."
    Assert-NotContains $runtime 'AllowAllEntitlementProvider' "MasterControlRuntime.cpp must not bypass Forsetti entitlement gating with AllowAllEntitlementProvider."

    # v0.10.14 alignment: gateway / governance / direct-AI invariants

    # CLU governance bundles: dynamic dispatch at /api/governance/bundles/{windows|macos|ios}.
    # Asserting against the dispatcher prefix + the documenting comment that
    # lists all three platforms so this stays accurate as the dispatcher is
    # refactored.
    Assert-Contains $runtime 'startsWith\(request\.path,\s*"/api/governance/bundles/"\)' "MasterControlRuntime.cpp must wire the dynamic /api/governance/bundles/{platform} dispatcher."
    Assert-Contains $runtime '/api/governance/bundles/\{windows\|macos\|ios\}' "MasterControlRuntime.cpp must document the dispatcher's accepted platforms (Windows/macOS/iOS)."

    # Native HTTP.sys gateway adapter is the only shipping substrate (v0.9.0+).
    Assert-Contains $runtime 'NativeHttpSysGatewayAdapter' "MasterControlRuntime.cpp must reference the NativeHttpSysGatewayAdapter (v0.9.0+ shipping substrate)."

    # Direct AI plugin slots (v0.10.12+): mutually exclusive Claude / ChatGPT / Grok routes.
    Assert-Contains $runtime '/api/claude-plugin/' "MasterControlRuntime.cpp must wire the Claude Code direct-AI plugin route."
    Assert-Contains $runtime '/api/chatgpt-plugin/' "MasterControlRuntime.cpp must wire the ChatGPT direct-AI plugin route (v0.10.12+)."
    Assert-Contains $runtime '/api/grok-plugin/' "MasterControlRuntime.cpp must wire the Grok direct-AI plugin route (v0.10.12+)."

    # v0.10.13 supervisor reachability self-check.
    Assert-Contains $runtime '/api/supervisor/reachability-check' "MasterControlRuntime.cpp must wire the supervisor reachability self-check route (v0.10.13+)."

    # LAN-trust posture: onboarding profiles must declare authRequired=false (C++
    # field setter on the OnboardingProfile struct) and trust=lan (txt + JSON
    # forms in discovery / DNS-SD advertising).
    Assert-Contains $runtime 'profile\.authRequired\s*=\s*false' "MasterControlRuntime.cpp must set OnboardingProfile.authRequired = false (LAN-trust posture; ADR-002 §1)."
    Assert-Contains $runtime '"trust"\s*,\s*"lan"|"trust":\s*"lan"|txt\["trust"\]\s*=\s*"lan"' "MasterControlRuntime.cpp must declare trust=lan in DNS-SD TXT and / or discovery JSON (LAN-trust posture)."
}

# v0.10.14 alignment: source-tree retirement checks for MCPJungle.
# MCPJungle was retired at v0.9.0; no production code path may still
# spawn an external mcpjungle.exe child. The GatewayType::MCPJungle enum
# value is allowed only as a back-compat deserialization tombstone in
# MasterControlModels.cpp / .h (so existing on-disk configs still parse).
$bootstrapperMainPath = Join-Path $repoRoot "src\MasterControlBaselineToolsWorker\main.cpp"
$bootstrapperEntryPath = Join-Path $repoRoot "src\MasterControlBootstrapper\main.cpp"
foreach ($p in @($bootstrapperMainPath, $bootstrapperEntryPath)) {
    if (Test-Path $p) {
        $c = Get-Content $p -Raw
        Assert-NotContains $c 'mcpjungle\.exe' "MCPJungle binary reference must not survive in $($p.Replace($repoRoot + '\', ''))."
    }
}

# v0.10.14 alignment: vendored Forsetti directory integrity check.
# The vendored Forsetti tree is read-only per .claude/rules/20-forsetti-clu-governance.md.
# Local check against the working tree using git status -s on the vendored path.
# This catches accidental modifications before they reach CI.
try {
    $forsettiVendorDirty = & git status --porcelain -- "Forsetti-Framework-Windows-main/" 2>$null
    if ($LASTEXITCODE -eq 0 -and $forsettiVendorDirty) {
        # Filter out untracked files (??) — only fail on tracked modifications.
        $tracked = $forsettiVendorDirty | Where-Object { $_ -notmatch '^\?\?' }
        if ($tracked) {
            $violations += "Vendored Forsetti directory has tracked modifications (read-only per rule 20-forsetti-clu-governance.md): $(($tracked | ForEach-Object { $_.Trim() }) -join '; ')"
        }
    }
} catch {
    # git not available in this shell — skip silently; CI will catch it.
}

if (-not (Test-Path $cluManifestPath)) {
    $violations += "src/MasterControlModules/Resources/ForsettiManifests/CommandLogicUnitModule.json is missing."
} else {
    $cluManifest = Get-Content $cluManifestPath -Raw | ConvertFrom-Json
    $cluCapabilities = @($cluManifest.capabilitiesRequested)
    foreach ($uiCapability in @("routing_overlay", "toolbar_items", "view_injection", "ui_theme_mask")) {
        if ($uiCapability -in $cluCapabilities) {
            $violations += "CommandLogicUnitModule.json must not request UI capabilities directly."
        }
    }
}

$requiredIapProductIDs = @{
    "com.mastercontrol.installer-import" = "mastercontrol.iap.installer-import"
    "com.mastercontrol.export" = "mastercontrol.iap.export"
    "com.mastercontrol.command-logic-unit" = "mastercontrol.iap.command-logic-unit"
    "com.mastercontrol.gateway-windows" = "mastercontrol.iap.gateway-windows"
    "com.mastercontrol.gateway-macos" = "mastercontrol.iap.gateway-macos"
    "com.mastercontrol.gateway-ios" = "mastercontrol.iap.gateway-ios"
    "com.mastercontrol.governance-windows" = "mastercontrol.iap.governance-windows"
    "com.mastercontrol.governance-macos" = "mastercontrol.iap.governance-macos"
    "com.mastercontrol.governance-ios" = "mastercontrol.iap.governance-ios"
    "com.mastercontrol.beacon-gateway" = "mastercontrol.iap.beacon-gateway"
}

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

        if ($requiredIapProductIDs.ContainsKey($manifest.moduleID)) {
            if ($manifest.iapProductID -ne $requiredIapProductIDs[$manifest.moduleID]) {
                $violations += "$rel - iapProductID must be '$($requiredIapProductIDs[$manifest.moduleID])'"
            }
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
