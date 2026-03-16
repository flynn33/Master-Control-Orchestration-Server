// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "ForsettiCore/ModuleModels.h"

namespace Forsetti {

// MARK: - ManifestLoaderError

enum class ManifestLoaderError {
    DirectoryNotFound,
    InvalidManifest,
    DuplicateModuleID
};

class ManifestLoaderException final : public std::runtime_error {
public:
    explicit ManifestLoaderException(ManifestLoaderError code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    ManifestLoaderError code() const noexcept { return code_; }

private:
    ManifestLoaderError code_;
};

// MARK: - ManifestLoader

class ManifestLoader final {
public:
    /// Loads all valid module manifests from .json files found recursively in the given directory.
    /// Silently skips JSON files that do not look like manifests.
    /// Throws ManifestLoaderException on directory errors or duplicate module IDs.
    static std::vector<ModuleManifest> loadManifests(const std::string& directoryPath);

    /// Returns true if the JSON object contains the three required root keys:
    /// "schemaVersion", "moduleID", and "displayName".
    static bool looksLikeManifestJSON(const nlohmann::json& j);
};

} // namespace Forsetti
