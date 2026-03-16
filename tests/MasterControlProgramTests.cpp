// Master Control Program
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#include "MasterControl/MasterControlDefaults.h"
#include "MasterControl/MasterControlModels.h"
#include "MasterControl/MasterControlRuntime.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
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

        MasterControl::MasterControlApplication application;
        success &= expect(application.initialize(), "Application should initialize");

        auto snapshot = application.snapshot();
        success &= expect(!snapshot.endpoints.empty(), "Snapshot should include endpoints");
        success &= expect(hasExport(snapshot.exports, "Install-ClaudeGateway.ps1"), "Exports should include a Claude installer helper");
        success &= expect(hasExport(snapshot.exports, "Install-CodexGateway.ps1"), "Exports should include a Codex installer helper");
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
            snapshot.surface.viewInjectionsBySlot.contains("overview") &&
                !snapshot.surface.viewInjectionsBySlot.at("overview").empty() &&
                snapshot.surface.viewInjectionsBySlot.at("overview").front().viewID == "OverviewSectionView",
            "Forsetti overview slot should resolve to the overview section view");
        success &= expect(
            snapshot.surface.overlaySchema.has_value() &&
                std::any_of(
                    snapshot.surface.overlaySchema->overlayRoutes.begin(),
                    snapshot.surface.overlaySchema->overlayRoutes.end(),
                    [](const auto& route) { return route.routeID == "imports-overlay"; }),
            "Forsetti overlay metadata should publish the imports overlay route");

        auto unsafeConfiguration = configuration;
        unsafeConfiguration.security.securityProtocolsEnabled = false;
        const auto unsafeResult = application.applyConfigurationJson(nlohmann::json(unsafeConfiguration).dump(), false);
        success &= expect(!unsafeResult.succeeded && unsafeResult.requiresConfirmation, "Unsafe configuration should require confirmation");
        const auto confirmedResult = application.applyConfigurationJson(nlohmann::json(unsafeConfiguration).dump(), true);
        success &= expect(confirmedResult.succeeded, "Unsafe configuration should succeed after confirmation");

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

    std::filesystem::remove_all(tempRoot);
    return success ? 0 : 1;
}
