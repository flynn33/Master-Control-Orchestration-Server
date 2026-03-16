// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ManifestLoader.h"
#include <fstream>
#include <set>

namespace Forsetti {

// MARK: - ManifestLoader

std::vector<ModuleManifest> ManifestLoader::loadManifests(const std::string& directoryPath) {
    namespace fs = std::filesystem;

    // Verify the directory exists
    if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
        throw ManifestLoaderException(
            ManifestLoaderError::DirectoryNotFound,
            "Directory not found: " + directoryPath
        );
    }

    std::vector<ModuleManifest> manifests;
    std::set<std::string> seenModuleIDs;

    for (const auto& entry : fs::recursive_directory_iterator(directoryPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        if (entry.path().extension() != ".json") {
            continue;
        }

        // Read the file contents
        std::ifstream file(entry.path());
        if (!file.is_open()) {
            continue; // Silently skip files we cannot open
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(file);
        } catch (const nlohmann::json::parse_error&) {
            // Silently skip files with invalid JSON
            continue;
        }

        // Check if this JSON looks like a manifest
        if (!looksLikeManifestJSON(j)) {
            // Silently skip non-manifest JSON files
            continue;
        }

        // Parse the manifest
        ModuleManifest manifest;
        try {
            manifest = j.get<ModuleManifest>();
        } catch (const nlohmann::json::exception&) {
            throw ManifestLoaderException(
                ManifestLoaderError::InvalidManifest,
                "Invalid manifest in file: " + entry.path().string()
            );
        }

        // Check for duplicate module IDs
        if (seenModuleIDs.count(manifest.moduleID) > 0) {
            throw ManifestLoaderException(
                ManifestLoaderError::DuplicateModuleID,
                "Duplicate module ID: " + manifest.moduleID
            );
        }

        seenModuleIDs.insert(manifest.moduleID);
        manifests.push_back(std::move(manifest));
    }

    return manifests;
}

bool ManifestLoader::looksLikeManifestJSON(const nlohmann::json& j) {
    if (!j.is_object()) {
        return false;
    }

    return j.contains("schemaVersion")
        && j.contains("moduleID")
        && j.contains("displayName");
}

} // namespace Forsetti
