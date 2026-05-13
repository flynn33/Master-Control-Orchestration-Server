// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "MasterControl/MasterControlModels.h"

#include <filesystem>
#include <string>
#include <vector>

namespace MasterControl {

struct DiscoveredEnvironment final {
    std::string hostName;
    std::string operatingSystem;
    std::string preferredBindAddress;
    std::string macAddress;
};

struct AppPaths final {
    std::filesystem::path executableDirectory;
    std::filesystem::path dataDirectory;
    std::filesystem::path configurationFile;
    std::filesystem::path installHistoryFile;
    std::filesystem::path appleOperationHistoryFile;
    std::filesystem::path entitlementsFile;
    std::filesystem::path manifestsDirectory;
    std::filesystem::path webRootDirectory;
    std::filesystem::path cluProfileFile;
    std::filesystem::path workDirectory;
    // PHASE-05 (ADR-002 §6): vendored Forsetti instructions file. Used by
    // GovernanceBundleService to populate forsettiFrameworkVersion. Vendored
    // content is read-only per .claude/rules/20-forsetti-clu-governance.md.
    std::filesystem::path forsettiInstructionsFile;
};

DiscoveredEnvironment detectLocalEnvironment();
AppPaths resolveAppPaths();
std::vector<RuntimeEndpoint> buildDefaultSeededEndpoints();
std::vector<RuntimeEndpoint> buildDefaultSeededEndpointsForHost(std::string host);
AppConfiguration buildDefaultConfiguration();
std::string executableDirectoryUtf8();

} // namespace MasterControl
