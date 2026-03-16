// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include <compare>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace Forsetti {

/// Semantic version with major.minor.patch and optional prerelease tag.
/// Mirrors the Swift SemVer struct from ForsettiCore.
struct SemVer final {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::optional<std::string> prerelease = std::nullopt;

    /// Default constructor
    SemVer() = default;

    /// Construct with components
    SemVer(int major, int minor, int patch, std::optional<std::string> prerelease = std::nullopt);

    /// Parse from string "major.minor.patch[-prerelease]"
    static std::optional<SemVer> fromString(const std::string& versionString);

    /// Convert to string representation
    std::string toString() const;

    /// Spaceship operator for three-way comparison.
    /// Prerelease versions are lower than release versions (per SemVer spec).
    /// When both have prerelease, compare lexicographically.
    std::strong_ordering operator<=>(const SemVer& other) const;

    /// Equality operator
    bool operator==(const SemVer& other) const = default;
};

// nlohmann JSON serialization
void to_json(nlohmann::json& j, const SemVer& v);
void from_json(const nlohmann::json& j, SemVer& v);

} // namespace Forsetti
