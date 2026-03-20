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
    "com.mastercontrol.provider-codex",
    "com.mastercontrol.provider-claude-code",
    "com.mastercontrol.provider-xai",
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
    Assert-NotContains $shellWindowCpp 'dashboard-clu' "MainWindow.xaml.cpp must not hardcode the CLU toolbar item in bootstrap surface data."
    Assert-NotContains $shellWindowCpp 'clu-nav' "MainWindow.xaml.cpp must not hardcode the CLU navigation pointer in bootstrap surface data."
    Assert-NotContains $shellWindowCpp 'clu-surface' "MainWindow.xaml.cpp must not hardcode the CLU view injection in bootstrap surface data."
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
    Assert-Contains $contracts 'IProviderCatalogService' "MasterControlContracts.h must define a provider capability catalog service."
    Assert-Contains $contracts 'IProviderCredentialStore' "MasterControlContracts.h must define a provider credential store service."
    Assert-Contains $contracts 'IProviderAssignmentService' "MasterControlContracts.h must define a provider ownership assignment service."
    Assert-Contains $contracts 'IPlatformServiceCatalogService' "MasterControlContracts.h must define a platform gateway and governance catalog service."
}

if (-not (Test-Path $modelsPath)) {
    $violations += "include/MasterControl/MasterControlModels.h is missing."
} else {
    $models = Get-Content $modelsPath -Raw
    Assert-Contains $models 'overlayRouteId' "MasterControlModels.h must let modules describe overlay routing requirements through the framework control contract."
    Assert-Contains $models 'registeredControlSurfaceRequests' "MasterControlModels.h must expose registered control-surface requests in the runtime surface snapshot."
    Assert-Contains $models 'publishedByModuleId' "MasterControlModels.h must expose which UI module registered the composed framework surface."
    Assert-Contains $models 'ProviderCapabilityDescriptor' "MasterControlModels.h must expose provider capability descriptors through the framework data model."
    Assert-Contains $models 'ProviderAssignment' "MasterControlModels.h must expose provider ownership assignments through the shared data model."
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
    Assert-Contains $webApp 'ensureBootstrapSurface' "resources/web/app.js must bootstrap the Forsetti browser surface."
    Assert-Contains $webApp 'renderSurfaceNavigation' "resources/web/app.js must rebuild browser navigation from Forsetti surface state."
    Assert-Contains $webApp 'renderSurfaceToolbar' "resources/web/app.js must rebuild the browser toolbar from Forsetti surface state."
    Assert-Contains $webApp 'resolvePrimaryViewForDestination' "resources/web/app.js must resolve browser content from Forsetti view injections."
    Assert-Contains $webApp 'openOverlayRoute' "resources/web/app.js must host Forsetti overlay routes."
    Assert-Contains $webApp 'CommandLogicUnitSectionView' "resources/web/app.js must expose a renderer for the CLU view when Forsetti publishes it."
    Assert-NotContains $webApp "dashboard-clu" "resources/web/app.js must not hardcode the CLU toolbar item in bootstrap surface data."
    Assert-NotContains $webApp "clu-nav" "resources/web/app.js must not hardcode the CLU navigation pointer in bootstrap surface data."
    Assert-NotContains $webApp "clu-surface" "resources/web/app.js must not hardcode the CLU view injection in bootstrap surface data."
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
    Assert-Contains $modulesCpp 'makeProviderIntegrationControlSurfaceRequests' "ProviderIntegrationModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeCodexProviderControlSurfaceRequests' "CodexProviderModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeClaudeCodeProviderControlSurfaceRequests' "ClaudeCodeProviderModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeXAIProviderControlSurfaceRequests' "XAIProviderModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'registerProviderCapability' "Provider modules must publish their capability descriptors through the framework."
    Assert-Contains $modulesCpp 'makeExportControlSurfaceRequests' "ExportModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeWindowsGatewayControlSurfaceRequests' "WindowsGatewayModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeMacGatewayControlSurfaceRequests' "MacGatewayModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeIOSGatewayControlSurfaceRequests' "IOSGatewayModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeWindowsGovernanceControlSurfaceRequests' "Windows governance module must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeMacGovernanceControlSurfaceRequests' "Mac governance module must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'makeIOSGovernanceControlSurfaceRequests' "iOS governance module must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'registerPlatformGateway' "Platform gateway modules must publish their LAN service descriptors through the framework."
    Assert-Contains $modulesCpp 'registerGovernanceServer' "Governance server modules must publish their MCP server descriptors through the framework."
    Assert-Contains $modulesCpp 'makeBeaconGatewayControlSurfaceRequests' "BeaconGatewayModule must register its control-surface needs through the framework."
    Assert-Contains $modulesCpp 'return Forsetti::UIContributions{};' "DashboardUIModule must bootstrap from framework control requests instead of shipping hardcoded UI contributions."
    Assert-Contains $modulesCpp 'mastercontrol.dashboard.surface.registered' "DashboardUIModule must publish startup surface registration metadata."
    Assert-NotContains $modulesCpp 'makeCommandLogicUnitSurfaceContributions' "CLU must not build raw UI contribution payloads directly."
}

if (-not (Test-Path $runtimePath)) {
    $violations += "src/MasterControlApp/MasterControlRuntime.cpp is missing."
} else {
    $runtime = Get-Content $runtimePath -Raw
    Assert-Contains $runtime 'class FileBackedEntitlementProvider' "MasterControlRuntime.cpp must use a reconciled entitlement provider."
    Assert-Contains $runtime 'buildDefaultEntitlementStateDocument' "MasterControlRuntime.cpp must seed entitlement state for runtime reconciliation."
    Assert-Contains $runtime 'paths_.entitlementsFile' "MasterControlRuntime.cpp must use the app entitlement state file."
    Assert-Contains $runtime 'paths_.providerCredentialsFile' "MasterControlRuntime.cpp must use the app provider credential state file."
    Assert-Contains $runtime 'ProviderCatalogService' "MasterControlRuntime.cpp must host a provider capability catalog."
    Assert-Contains $runtime 'ProviderCredentialStore' "MasterControlRuntime.cpp must host a secure provider credential store."
    Assert-Contains $runtime 'ProviderAssignmentService' "MasterControlRuntime.cpp must host provider ownership assignment logic."
    Assert-Contains $runtime 'PlatformServiceCatalogService' "MasterControlRuntime.cpp must host the platform gateway and governance catalog."
    Assert-Contains $runtime 'DnsServiceRegister' "MasterControlRuntime.cpp must advertise platform gateway services on the LAN."
    Assert-Contains $runtime '/api/platform-services/config/' "MasterControlRuntime.cpp must expose platform-specific client configuration endpoints."
    Assert-Contains $runtime '/mcp/gateway/' "MasterControlRuntime.cpp must expose platform-specific gateway routes."
    Assert-Contains $runtime '/mcp/governance/' "MasterControlRuntime.cpp must expose platform-specific governance MCP routes."
    Assert-NotContains $runtime 'AllowAllEntitlementProvider' "MasterControlRuntime.cpp must not bypass Forsetti entitlement gating with AllowAllEntitlementProvider."
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
    "com.mastercontrol.provider-integration" = "mastercontrol.iap.provider-integration"
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
