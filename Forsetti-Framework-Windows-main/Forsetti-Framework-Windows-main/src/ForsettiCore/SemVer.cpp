// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/SemVer.h"
#include <sstream>
#include <stdexcept>

namespace Forsetti {

SemVer::SemVer(int major, int minor, int patch, std::optional<std::string> prerelease)
    : major(major), minor(minor), patch(patch), prerelease(std::move(prerelease)) {}

std::optional<SemVer> SemVer::fromString(const std::string& versionString) {
    if (versionString.empty()) {
        return std::nullopt;
    }

    SemVer result;
    std::string working = versionString;

    // Split off prerelease at first hyphen
    auto hyphenPos = working.find('-');
    if (hyphenPos != std::string::npos) {
        result.prerelease = working.substr(hyphenPos + 1);
        working = working.substr(0, hyphenPos);
    }

    // Parse major.minor.patch
    std::istringstream stream(working);
    char dot1 = 0, dot2 = 0;

    stream >> result.major;
    if (stream.fail()) return std::nullopt;

    stream >> dot1;
    if (dot1 != '.') return std::nullopt;

    stream >> result.minor;
    if (stream.fail()) return std::nullopt;

    stream >> dot2;
    if (dot2 != '.') return std::nullopt;

    stream >> result.patch;
    if (stream.fail()) return std::nullopt;

    // Ensure nothing extra remains
    std::string remaining;
    stream >> remaining;
    if (!remaining.empty()) return std::nullopt;

    return result;
}

std::string SemVer::toString() const {
    std::string result = std::to_string(major) + "." +
                         std::to_string(minor) + "." +
                         std::to_string(patch);
    if (prerelease.has_value()) {
        result += "-" + prerelease.value();
    }
    return result;
}

std::strong_ordering SemVer::operator<=>(const SemVer& other) const {
    // Compare major.minor.patch first
    if (auto cmp = major <=> other.major; cmp != 0) return cmp;
    if (auto cmp = minor <=> other.minor; cmp != 0) return cmp;
    if (auto cmp = patch <=> other.patch; cmp != 0) return cmp;

    // Prerelease handling per SemVer spec:
    // - A version without prerelease has higher precedence than one with prerelease
    // - When both have prerelease, compare lexicographically
    bool hasPreA = prerelease.has_value();
    bool hasPreB = other.prerelease.has_value();

    if (!hasPreA && !hasPreB) return std::strong_ordering::equal;
    if (!hasPreA && hasPreB) return std::strong_ordering::greater;  // release > prerelease
    if (hasPreA && !hasPreB) return std::strong_ordering::less;     // prerelease < release

    // Both have prerelease — compare lexicographically
    if (prerelease.value() < other.prerelease.value()) return std::strong_ordering::less;
    if (prerelease.value() > other.prerelease.value()) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

// JSON serialization
void to_json(nlohmann::json& j, const SemVer& v) {
    j = nlohmann::json{
        {"major", v.major},
        {"minor", v.minor},
        {"patch", v.patch}
    };
    if (v.prerelease.has_value()) {
        j["prerelease"] = v.prerelease.value();
    } else {
        j["prerelease"] = nullptr;
    }
}

void from_json(const nlohmann::json& j, SemVer& v) {
    j.at("major").get_to(v.major);
    j.at("minor").get_to(v.minor);
    j.at("patch").get_to(v.patch);

    if (j.contains("prerelease") && !j["prerelease"].is_null()) {
        v.prerelease = j["prerelease"].get<std::string>();
    } else {
        v.prerelease = std::nullopt;
    }
}

} // namespace Forsetti
