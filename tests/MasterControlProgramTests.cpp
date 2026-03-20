// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModels.h"
#include "MasterControl/MasterControlRuntime.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

bool expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "Test failed: " << message << '\n';
        return false;
    }
    return true;
}

class ScopedEnvironmentOverride final {
public:
    ScopedEnvironmentOverride(std::wstring name, std::wstring value)
        : name_(std::move(name)) {
        const DWORD required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
        if (required > 0) {
            originalValue_.emplace(static_cast<size_t>(required - 1), L'\0');
            GetEnvironmentVariableW(name_.c_str(), originalValue_->data(), required);
        }

        SetEnvironmentVariableW(name_.c_str(), value.c_str());
    }

    ~ScopedEnvironmentOverride() {
        SetEnvironmentVariableW(name_.c_str(), originalValue_.has_value() ? originalValue_->c_str() : nullptr);
    }

private:
    std::wstring name_;
    std::optional<std::wstring> originalValue_;
};

std::filesystem::path makeTempRoot() {
    const auto root = std::filesystem::temp_directory_path() /
        ("MasterControlProgramTests_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeTextFile(const std::filesystem::path& filePath, const std::string& contents) {
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
    output << contents;
}

std::filesystem::path currentExecutablePath() {
    wchar_t buffer[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, length));
}

std::filesystem::path bootstrapperBinaryPath() {
    const auto executablePath = currentExecutablePath();
    const auto configurationName = executablePath.parent_path().filename();
    const auto buildRoot = executablePath.parent_path().parent_path().parent_path();
    return buildRoot / "src" / "MasterControlBootstrapper" / configurationName / "MasterControlBootstrapper.exe";
}

int runProcess(const std::wstring& command, const std::filesystem::path& workingDirectory) {
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInformation{};
    std::wstring mutableCommand = command;
    if (CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.wstring().c_str(),
            &startupInfo,
            &processInformation) == 0) {
        return static_cast<int>(GetLastError());
    }

    WaitForSingleObject(processInformation.hProcess, INFINITE);

    DWORD exitCode = 1;
    GetExitCodeProcess(processInformation.hProcess, &exitCode);
    CloseHandle(processInformation.hThread);
    CloseHandle(processInformation.hProcess);
    return static_cast<int>(exitCode);
}

bool toolExists(const wchar_t* executableName) {
    wchar_t pathBuffer[MAX_PATH]{};
    return SearchPathW(nullptr, executableName, nullptr, MAX_PATH, pathBuffer, nullptr) > 0;
}

std::optional<MasterControl::RuntimeEndpoint> findEndpoint(const std::vector<MasterControl::RuntimeEndpoint>& endpoints,
                                                           const std::string& id) {
    const auto iterator = std::find_if(
        endpoints.begin(),
        endpoints.end(),
        [&id](const MasterControl::RuntimeEndpoint& endpoint) { return endpoint.id == id; });
    if (iterator == endpoints.end()) {
        return std::nullopt;
    }
    return *iterator;
}

bool hasProvider(const std::vector<MasterControl::ProviderConnection>& providers, const std::string& id) {
    return std::any_of(
        providers.begin(),
        providers.end(),
        [&id](const MasterControl::ProviderConnection& provider) { return provider.id == id; });
}

std::optional<MasterControl::ProviderConnection> findProvider(
    const std::vector<MasterControl::ProviderConnection>& providers,
    const std::string& id) {
    const auto iterator = std::find_if(
        providers.begin(),
        providers.end(),
        [&id](const MasterControl::ProviderConnection& provider) { return provider.id == id; });
    if (iterator == providers.end()) {
        return std::nullopt;
    }
    return *iterator;
}

bool hasExport(const std::vector<MasterControl::ExportArtifact>& artifacts, const std::string& fileName) {
    return std::any_of(
        artifacts.begin(),
        artifacts.end(),
        [&fileName](const MasterControl::ExportArtifact& artifact) { return artifact.fileName == fileName; });
}

bool hasNavigationDestination(const std::optional<Forsetti::OverlaySchema>& overlaySchema,
                              const std::string& destinationId) {
    if (!overlaySchema.has_value()) {
        return false;
    }

    return std::any_of(
        overlaySchema->navigationPointers.begin(),
        overlaySchema->navigationPointers.end(),
        [&destinationId](const Forsetti::NavigationPointer& pointer) {
            return pointer.baseDestinationID == destinationId;
        });
}

bool hasToolbarItemId(const std::vector<Forsetti::ToolbarItemDescriptor>& toolbarItems,
                      const std::string& itemId) {
    return std::any_of(
        toolbarItems.begin(),
        toolbarItems.end(),
        [&itemId](const Forsetti::ToolbarItemDescriptor& item) {
            return item.itemID == itemId;
        });
}

bool hasOverlayRouteId(const std::optional<Forsetti::OverlaySchema>& overlaySchema,
                       const std::string& routeId) {
    if (!overlaySchema.has_value()) {
        return false;
    }

    return std::any_of(
        overlaySchema->overlayRoutes.begin(),
        overlaySchema->overlayRoutes.end(),
        [&routeId](const Forsetti::OverlayRoute& route) {
            return route.routeID == routeId;
        });
}

bool hasControlRequestsForModules(const std::vector<MasterControl::ModuleControlSurfaceRequest>& requests,
                                  const std::vector<std::string>& moduleIds) {
    return std::all_of(
        moduleIds.begin(),
        moduleIds.end(),
        [&requests](const std::string& moduleId) {
            return std::any_of(
                requests.begin(),
                requests.end(),
                [&moduleId](const MasterControl::ModuleControlSurfaceRequest& request) {
                    return request.moduleId == moduleId;
                });
        });
}

bool hasHistoryEntry(const std::vector<MasterControl::InstallProvenance>& entries,
                     MasterControl::InstallerKind kind,
                     const std::string& source) {
    return std::any_of(
        entries.begin(),
        entries.end(),
        [kind, &source](const MasterControl::InstallProvenance& entry) {
            return entry.kind == kind && entry.source == source;
        });
}

std::wstring escapePowerShellLiteral(std::wstring value) {
    size_t position = 0;
    while ((position = value.find(L'\'', position)) != std::wstring::npos) {
        value.insert(position, L"'");
        position += 2;
    }
    return value;
}

bool createGitImportFixture(const std::filesystem::path& repoRoot) {
    writeTextFile(repoRoot / "bootstrap.ps1", "New-Item -Path (Join-Path $PSScriptRoot 'repo-bootstrap.ok') -ItemType File -Force | Out-Null\nexit 0\n");

    const nlohmann::json manifest = {
        { "version", "2.0.0" },
        { "bootstrapScript", "bootstrap.ps1" },
        { "seededEndpoints", nlohmann::json::array({
            {
                { "id", "repo-import-endpoint" },
                { "displayName", "Repo Import Endpoint" },
                { "kind", "mcp_server" },
                { "host", "" },
                { "port", 7420 },
                { "protocol", "http" },
                { "routePath", "/health" },
                { "description", "Installed from repository contract" }
            }
        }) },
        { "providers", nlohmann::json::array({
            {
                { "id", "repo-import-provider" },
                { "kind", "generic" },
                { "displayName", "Repo Import Provider" },
                { "baseUrl", "https://repo.example.test" },
                { "enabled", true },
                { "allowAutonomousControl", false }
            }
        }) }
    };

    writeTextFile(repoRoot / "mcp-bootstrap.json", manifest.dump(2));

    return runProcess(L"git init", repoRoot) == 0 &&
        runProcess(L"git config user.email master-control-tests@example.com", repoRoot) == 0 &&
        runProcess(L"git config user.name \"Master Control Tests\"", repoRoot) == 0 &&
        runProcess(L"git add .", repoRoot) == 0 &&
        runProcess(L"git commit -m \"Initial bootstrap fixture\"", repoRoot) == 0 &&
        runProcess(L"git branch -M main", repoRoot) == 0;
}

bool createZipImportFixture(const std::filesystem::path& sourceRoot, const std::filesystem::path& zipPath) {
    writeTextFile(sourceRoot / "bootstrap.ps1", "New-Item -Path (Join-Path $PSScriptRoot 'zip-bootstrap.ok') -ItemType File -Force | Out-Null\nexit 0\n");

    const nlohmann::json manifest = {
        { "version", "3.1.0" },
        { "bootstrapScript", "bootstrap.ps1" },
        { "seededEndpoints", nlohmann::json::array({
            {
                { "id", "zip-import-endpoint" },
                { "displayName", "Zip Import Endpoint" },
                { "kind", "sub_agent" },
                { "host", "" },
                { "port", 7421 },
                { "protocol", "http" },
                { "routePath", "/status" },
                { "description", "Installed from zip contract" }
            }
        }) },
        { "providers", nlohmann::json::array({
            {
                { "id", "zip-import-provider" },
                { "kind", "generic" },
                { "displayName", "Zip Import Provider" },
                { "baseUrl", "https://zip.example.test" },
                { "enabled", true },
                { "allowAutonomousControl", true }
            }
        }) }
    };

    writeTextFile(sourceRoot / "mcp-bootstrap.json", manifest.dump(2));

    const auto command = L"pwsh -NoProfile -ExecutionPolicy Bypass -Command \"Compress-Archive -Path '" +
        escapePowerShellLiteral((sourceRoot / L"*").wstring()) +
        L"' -DestinationPath '" + escapePowerShellLiteral(zipPath.wstring()) + L"' -Force\"";

    return runProcess(command, sourceRoot.parent_path()) == 0;
}

} // namespace

int main() {
    bool success = true;

    const auto environment = MasterControl::detectLocalEnvironment();
    success &= expect(!environment.hostName.empty(), "Detected environment should include a host name");
    success &= expect(!environment.operatingSystem.empty(), "Detected environment should include an operating system description");
    success &= expect(!environment.preferredBindAddress.empty(), "Detected environment should include a preferred bind address");

    const auto configuration = MasterControl::buildDefaultConfiguration();
    success &= expect(configuration.browserPort == 7300, "Default browser port should be 7300");
    success &= expect(configuration.activeProfile.preferredBindAddress == environment.preferredBindAddress, "Default profile should honor the detected bind address");
    success &= expect(!configuration.activeProfile.environmentName.empty(), "Default profile should describe the detected environment");
    success &= expect(configuration.providers.size() >= 4, "Default providers should include named adapters");

    const auto gatewayEndpoint = findEndpoint(configuration.activeProfile.seededEndpoints, "aggregator-gateway");
    success &= expect(gatewayEndpoint.has_value(), "BLADE profile should include the aggregator gateway");
    success &= expect(gatewayEndpoint.has_value() && gatewayEndpoint->host == configuration.activeProfile.preferredBindAddress, "Seeded endpoints should use the detected host");

    const nlohmann::json serialized = configuration;
    const auto roundTripped = serialized.get<MasterControl::AppConfiguration>();
    success &= expect(roundTripped.instanceName == configuration.instanceName, "Configuration should round-trip through JSON");

    const auto tempRoot = makeTempRoot();
    {
        ScopedEnvironmentOverride dataDirectoryOverride(L"MASTERCONTROL_DATA_DIR", (tempRoot / "data").wstring());
        const auto appPaths = MasterControl::resolveAppPaths();

        MasterControl::MasterControlApplication application;
        success &= expect(application.initialize(), "Application should initialize");

        auto snapshot = application.snapshot();
        const std::vector<std::string> controlSurfaceModuleIds = {
            "com.mastercontrol.environment-discovery",
            "com.mastercontrol.host-telemetry",
            "com.mastercontrol.runtime-inventory",
            "com.mastercontrol.configuration",
            "com.mastercontrol.installer-import",
            "com.mastercontrol.provider-integration",
            "com.mastercontrol.export",
            "com.mastercontrol.command-logic-unit",
            "com.mastercontrol.beacon-gateway"
        };
        success &= expect(!snapshot.endpoints.empty(), "Snapshot should include endpoints");
        success &= expect(hasExport(snapshot.exports, "Install-ClaudeGateway.ps1"), "Exports should include a Claude installer helper");
        success &= expect(hasExport(snapshot.exports, "Install-CodexGateway.ps1"), "Exports should include a Codex installer helper");
        success &= expect(snapshot.governance.unitName == "Command Logic Unit", "Snapshot should expose the CLU governance unit");
        success &= expect(!snapshot.governance.roles.empty(), "CLU snapshot should publish governance roles");
        success &= expect(!snapshot.governance.rules.empty(), "CLU snapshot should publish governance rules");
        success &= expect(!snapshot.governance.documents.empty(), "CLU snapshot should publish governance documents");
        success &= expect(snapshot.surface.overlaySchema.has_value(), "Snapshot should expose Forsetti overlay metadata");
        success &= expect(
            snapshot.surface.overlaySchema.has_value() &&
                snapshot.surface.overlaySchema->navigationPointers.size() >= 8,
            "Forsetti overlay metadata should describe the shell navigation lanes");
        success &= expect(snapshot.surface.toolbarItems.size() >= 6, "Forsetti surface snapshot should expose toolbar items");
        success &= expect(
            snapshot.surface.viewInjectionsBySlot.size() >= 8,
            "Forsetti surface snapshot should expose injected section slots");
        success &= expect(
            hasControlRequestsForModules(snapshot.surface.registeredControlSurfaceRequests, controlSurfaceModuleIds),
            "Every service module should register its control-surface needs with the framework");
        success &= expect(
            snapshot.surface.publishedByModuleId == "com.mastercontrol.dashboard-ui",
            "The dashboard UI module should publish the composed framework surface");
        success &= expect(
            !snapshot.surface.publishedAtUtc.empty(),
            "The framework surface snapshot should record when the UI module registered its composed surface");
        success &= expect(
            snapshot.surface.viewInjectionsBySlot.contains("overview") &&
                !snapshot.surface.viewInjectionsBySlot.at("overview").empty() &&
                snapshot.surface.viewInjectionsBySlot.at("overview").front().viewID == "OverviewSectionView",
            "Forsetti overview slot should resolve to the overview section view");
        success &= expect(
            hasNavigationDestination(snapshot.surface.overlaySchema, "clu"),
            "Forsetti overlay metadata should publish CLU navigation through the framework surface");
        success &= expect(
            hasToolbarItemId(snapshot.surface.toolbarItems, "command-logic-unit-dashboard"),
            "Forsetti surface snapshot should publish the CLU toolbar item");
        success &= expect(
            snapshot.surface.viewInjectionsBySlot.contains("clu") &&
                !snapshot.surface.viewInjectionsBySlot.at("clu").empty() &&
                snapshot.surface.viewInjectionsBySlot.at("clu").front().viewID == "CommandLogicUnitSectionView",
            "Forsetti CLU slot should resolve to the CLU section view");
        success &= expect(
            hasOverlayRouteId(snapshot.surface.overlaySchema, "imports-overlay"),
            "Forsetti overlay metadata should publish the imports overlay route");
        success &= expect(
            hasOverlayRouteId(snapshot.surface.overlaySchema, "settings-overlay"),
            "Forsetti overlay metadata should publish the settings overlay route");
        success &= expect(
            hasOverlayRouteId(snapshot.surface.overlaySchema, "exports-overlay"),
            "Forsetti overlay metadata should publish the exports overlay route");

        success &= expect(std::filesystem::exists(appPaths.entitlementsFile), "The runtime should seed an entitlement state file");
        writeTextFile(
            appPaths.entitlementsFile,
            nlohmann::json{
                { "unlockedModuleIDs", nlohmann::json::array({
                    "com.mastercontrol.environment-discovery",
                    "com.mastercontrol.host-telemetry",
                    "com.mastercontrol.runtime-inventory",
                    "com.mastercontrol.configuration",
                    "com.mastercontrol.dashboard-ui"
                }) },
                { "unlockedProductIDs", nlohmann::json::array({
                    "mastercontrol.iap.installer-import",
                    "mastercontrol.iap.provider-integration",
                    "mastercontrol.iap.export",
                    "mastercontrol.iap.beacon-gateway"
                }) }
            }.dump(2));

        for (int attempt = 0; attempt < 12; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            snapshot = application.snapshot();
            if (!hasNavigationDestination(snapshot.surface.overlaySchema, "clu")) {
                break;
            }
        }

        success &= expect(
            !hasNavigationDestination(snapshot.surface.overlaySchema, "clu"),
            "Forsetti entitlement reconciliation should remove CLU navigation when its product is no longer unlocked");
        success &= expect(
            !hasToolbarItemId(snapshot.surface.toolbarItems, "command-logic-unit-dashboard"),
            "Forsetti entitlement reconciliation should remove the CLU toolbar item when its product is no longer unlocked");

        writeTextFile(
            appPaths.entitlementsFile,
            nlohmann::json{
                { "unlockedModuleIDs", nlohmann::json::array({
                    "com.mastercontrol.environment-discovery",
                    "com.mastercontrol.host-telemetry",
                    "com.mastercontrol.runtime-inventory",
                    "com.mastercontrol.configuration",
                    "com.mastercontrol.dashboard-ui"
                }) },
                { "unlockedProductIDs", nlohmann::json::array({
                    "mastercontrol.iap.installer-import",
                    "mastercontrol.iap.provider-integration",
                    "mastercontrol.iap.export",
                    "mastercontrol.iap.command-logic-unit",
                    "mastercontrol.iap.beacon-gateway"
                }) }
            }.dump(2));

        for (int attempt = 0; attempt < 12; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            snapshot = application.snapshot();
            if (hasNavigationDestination(snapshot.surface.overlaySchema, "clu")) {
                break;
            }
        }

        success &= expect(
            hasNavigationDestination(snapshot.surface.overlaySchema, "clu"),
            "Forsetti entitlement reconciliation should restore CLU navigation when the product is unlocked again");

        auto unsafeConfiguration = configuration;
        unsafeConfiguration.security.securityProtocolsEnabled = false;
        const auto unsafeResult = application.applyConfigurationJson(nlohmann::json(unsafeConfiguration).dump(), false);
        success &= expect(!unsafeResult.succeeded && unsafeResult.requiresConfirmation, "Unsafe configuration should require confirmation");
        const auto confirmedResult = application.applyConfigurationJson(nlohmann::json(unsafeConfiguration).dump(), true);
        success &= expect(confirmedResult.succeeded, "Unsafe configuration should succeed after confirmation");
        snapshot = application.snapshot();
        success &= expect(snapshot.governance.posture == "blocked", "CLU should block unsafe open-LAN posture when security protocols are disabled");
        success &= expect(!snapshot.governance.findings.empty(), "CLU should record findings for blocked security posture");

        auto managedConfiguration = configuration;
        managedConfiguration.aiAutonomyEnabled = true;
        managedConfiguration.security.securityProtocolsEnabled = true;
        managedConfiguration.security.enableTls = true;
        managedConfiguration.security.enableAuthentication = true;
        managedConfiguration.security.allowTroubleshootingBypass = true;
        managedConfiguration.security.allowOpenLanAccess = false;
        managedConfiguration.security.trustedRemoteHosts = {
            "192.168.1.20",
            "builder-node.local"
        };
        const auto managedResult = application.applyConfigurationJson(nlohmann::json(managedConfiguration).dump(), false);
        success &= expect(managedResult.succeeded, "Managed security configuration should apply successfully");

        snapshot = application.snapshot();
        success &= expect(snapshot.security.securityProtocolsEnabled, "Security protocols should remain enabled after managed update");
        success &= expect(snapshot.security.enableTls, "Managed security update should enable TLS");
        success &= expect(snapshot.security.enableAuthentication, "Managed security update should enable authentication");
        success &= expect(snapshot.security.allowTroubleshootingBypass, "Managed security update should allow troubleshooting bypass");
        success &= expect(!snapshot.security.allowOpenLanAccess, "Managed security update should disable open LAN access");
        success &= expect(snapshot.security.trustedRemoteHosts.size() == 2, "Managed security update should persist trusted hosts");
        success &= expect(
            snapshot.security.trustedRemoteHosts.size() == 2 &&
                snapshot.security.trustedRemoteHosts[0] == "192.168.1.20" &&
                snapshot.security.trustedRemoteHosts[1] == "builder-node.local",
            "Managed security update should preserve trusted host ordering");

        const auto invalidProviderResult = application.upsertProviderJson(nlohmann::json{
            { "id", "invalid-provider" },
            { "kind", "generic" },
            { "displayName", "Invalid Provider" }
        }.dump());
        success &= expect(!invalidProviderResult.succeeded, "Provider updates without a base URL should be rejected");

        const auto providerUpsertResult = application.upsertProviderJson(nlohmann::json{
            { "id", "ops-provider" },
            { "kind", "openai" },
            { "displayName", "Operations Provider" },
            { "baseUrl", "https://ops.example.test/v1" },
            { "enabled", true },
            { "allowAutonomousControl", true }
        }.dump());
        success &= expect(providerUpsertResult.succeeded, "Provider upsert should succeed");

        snapshot = application.snapshot();
        const auto upsertedProvider = findProvider(snapshot.providers, "ops-provider");
        success &= expect(upsertedProvider.has_value(), "Provider upsert should be reflected in the runtime snapshot");
        success &= expect(
            upsertedProvider.has_value() && upsertedProvider->allowAutonomousControl,
            "Provider upsert should preserve autonomous control settings");
        success &= expect(
            upsertedProvider.has_value() && upsertedProvider->baseUrl == "https://ops.example.test/v1",
            "Provider upsert should persist the provider base URL");
        success &= expect(snapshot.providers.size() >= 5, "Provider upsert should extend the provider registry");
        success &= expect(hasExport(snapshot.exports, "master-control-gateway-profile.json"), "Exports should include the gateway profile");

        if (toolExists(L"pwsh.exe")) {
            const auto packageScript = tempRoot / "package-install.ps1";
            const auto markerFile = tempRoot / "package-install.ok";
            writeTextFile(
                packageScript,
                "New-Item -Path (Join-Path $PSScriptRoot 'package-install.ok') -ItemType File -Force | Out-Null\nexit 0\n");

            const auto packageSource = packageScript.string();
            const auto packageResult = application.installPackageJson(nlohmann::json{
                { "kind", "powershell" },
                { "localPath", packageSource },
                { "arguments", "" }
            }.dump());
            success &= expect(packageResult.succeeded, "Local PowerShell package install should succeed");
            success &= expect(std::filesystem::exists(markerFile), "Local PowerShell package install should execute the payload");

            snapshot = application.snapshot();
            success &= expect(
                hasHistoryEntry(snapshot.installHistory, MasterControl::InstallerKind::PowerShell, packageSource),
                "Local PowerShell package install should be recorded in history");
        } else {
            std::cout << "Skipping package import test because pwsh.exe was not found.\n";
        }

        if (toolExists(L"git.exe")) {
            const auto repoRoot = tempRoot / "repo-fixture";
            success &= expect(createGitImportFixture(repoRoot), "Repository fixture should be created successfully");
            if (success) {
                const auto repoSource = repoRoot.string();
                const auto repoResult = application.installRepoJson(nlohmann::json{
                    { "repositoryUrl", repoSource },
                    { "branch", "main" },
                    { "manifestFile", "mcp-bootstrap.json" }
                }.dump());
                success &= expect(repoResult.succeeded, "Repository import should succeed");

                snapshot = application.snapshot();
                const auto repoEndpoint = findEndpoint(snapshot.endpoints, "repo-import-endpoint");
                success &= expect(repoEndpoint.has_value(), "Repository import should register endpoints");
                success &= expect(repoEndpoint.has_value() && repoEndpoint->host == configuration.activeProfile.preferredBindAddress, "Repository import should backfill missing hosts");
                success &= expect(hasProvider(snapshot.providers, "repo-import-provider"), "Repository import should register providers");
                success &= expect(hasHistoryEntry(snapshot.installHistory, MasterControl::InstallerKind::GitBootstrapRepo, repoSource), "Repository import should be recorded in history");
            }
        } else {
            std::cout << "Skipping repository import test because git.exe was not found.\n";
        }

        if (toolExists(L"pwsh.exe")) {
            const auto zipRoot = tempRoot / "zip-fixture";
            const auto zipPath = tempRoot / "zip-fixture.zip";
            success &= expect(createZipImportFixture(zipRoot, zipPath), "Zip fixture should be created successfully");
            if (success) {
                const auto zipSource = zipPath.string();
                const auto zipResult = application.installZipJson(nlohmann::json{
                    { "source", zipSource },
                    { "manifestFile", "mcp-bootstrap.json" }
                }.dump());
                success &= expect(zipResult.succeeded, "Zip import should succeed");

                snapshot = application.snapshot();
                const auto zipEndpoint = findEndpoint(snapshot.endpoints, "zip-import-endpoint");
                success &= expect(zipEndpoint.has_value(), "Zip import should register endpoints");
                success &= expect(zipEndpoint.has_value() && zipEndpoint->host == configuration.activeProfile.preferredBindAddress, "Zip import should backfill missing hosts");
                success &= expect(hasProvider(snapshot.providers, "zip-import-provider"), "Zip import should register providers");
                success &= expect(hasHistoryEntry(snapshot.installHistory, MasterControl::InstallerKind::ZipBundle, zipSource), "Zip import should be recorded in history");
            }
        } else {
            std::cout << "Skipping zip import test because pwsh.exe was not found.\n";
        }

        application.shutdown();
    }

    const auto bootstrapperBinary = bootstrapperBinaryPath();
    success &= expect(std::filesystem::exists(bootstrapperBinary), "Bootstrapper binary should exist for installer validation");
    if (success) {
        const auto bootstrapInstallDirectory = tempRoot / "bootstrapper-install";
        const auto bootstrapDataDirectory = tempRoot / "bootstrapper-data";
        const auto bootstrapConfigurationFile = bootstrapDataDirectory / "config" / "master-control-program.json";
        const auto bootstrapInstallStateFile = bootstrapInstallDirectory / "installation-state.json";

        ScopedEnvironmentOverride bootstrapDataOverride(L"MASTERCONTROL_DATA_DIR", bootstrapDataDirectory.wstring());

        const auto installCommand = L"\"" + bootstrapperBinary.wstring() + L"\" install \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";
        success &= expect(
            runProcess(installCommand, tempRoot) == 0,
            "Bootstrapper install should succeed when system integrations are skipped");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "MasterControlServiceHost.exe"),
            "Bootstrapper install should stage the service host");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "MasterControlShell.exe"),
            "Bootstrapper install should stage the shell host");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "share" / "MasterControlProgram" / "web" / "index.html"),
            "Bootstrapper install should stage browser resources");
        success &= expect(
            std::filesystem::exists(bootstrapInstallDirectory / "share" / "MasterControlProgram" / "ForsettiManifests" / "DashboardUIModule.json"),
            "Bootstrapper install should stage Forsetti manifests");
        success &= expect(
            std::filesystem::exists(bootstrapInstallStateFile),
            "Bootstrapper install should write installation state");
        success &= expect(
            std::filesystem::exists(bootstrapConfigurationFile),
            "Bootstrapper install should seed configuration in the configured data directory");

        std::filesystem::remove(bootstrapConfigurationFile);
        const auto repairCommand = L"\"" + bootstrapperBinary.wstring() + L"\" repair \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";
        success &= expect(
            runProcess(repairCommand, tempRoot) == 0,
            "Bootstrapper repair should succeed when system integrations are skipped");
        success &= expect(
            std::filesystem::exists(bootstrapConfigurationFile),
            "Bootstrapper repair should reseed missing configuration");

        const auto uninstallCommand = L"\"" + bootstrapperBinary.wstring() + L"\" uninstall \"" +
            bootstrapInstallDirectory.wstring() +
            L"\" --purge-install-dir --purge-data --skip-service --skip-firewall --skip-shortcuts --skip-uninstall-registration";
        success &= expect(
            runProcess(uninstallCommand, tempRoot) == 0,
            "Bootstrapper uninstall should succeed when system integrations are skipped");
        success &= expect(
            !std::filesystem::exists(bootstrapInstallDirectory),
            "Bootstrapper uninstall should remove the install directory when requested");
        success &= expect(
            !std::filesystem::exists(bootstrapDataDirectory),
            "Bootstrapper uninstall should remove the data directory when requested");
    }

    std::filesystem::remove_all(tempRoot);
    return success ? 0 : 1;
}
