// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once

#include "ForsettiCore/ModuleModels.h"
#include "ForsettiCore/SemVer.h"
#include "ForsettiCore/CapabilityPolicy.h"
#include <vector>
#include <string>
#include <memory>

namespace Forsetti {

// MARK: - CompatibilitySeverity

enum class CompatibilitySeverity {
    Info,
    Warning,
    Error
};

NLOHMANN_JSON_SERIALIZE_ENUM(CompatibilitySeverity, {
    { CompatibilitySeverity::Info, "info" },
    { CompatibilitySeverity::Warning, "warning" },
    { CompatibilitySeverity::Error, "error" },
})

// MARK: - CompatibilityIssueCode

enum class CompatibilityIssueCode {
    UnsupportedSchemaVersion,
    UnsupportedPlatform,
    IncompatibleForsettiVersion,
    DeniedCapability
};

NLOHMANN_JSON_SERIALIZE_ENUM(CompatibilityIssueCode, {
    { CompatibilityIssueCode::UnsupportedSchemaVersion, "unsupported_schema_version" },
    { CompatibilityIssueCode::UnsupportedPlatform, "unsupported_platform" },
    { CompatibilityIssueCode::IncompatibleForsettiVersion, "incompatible_forsetti_version" },
    { CompatibilityIssueCode::DeniedCapability, "denied_capability" },
})

// MARK: - CompatibilityIssue

struct CompatibilityIssue final {
    std::string message;
    CompatibilitySeverity severity;
    CompatibilityIssueCode code;
};

// MARK: - CompatibilityReport

struct CompatibilityReport final {
    std::string moduleID;
    std::vector<CompatibilityIssue> issues;

    /// Returns true if no issues have Error severity.
    bool isCompatible() const;
};

// MARK: - CompatibilityChecker

class CompatibilityChecker final {
public:
    CompatibilityChecker(SemVer frameworkVersion, std::shared_ptr<ICapabilityPolicy> policy);

    /// Checks the given manifest for compatibility issues.
    /// Returns a report containing all detected issues.
    CompatibilityReport checkCompatibility(const ModuleManifest& manifest) const;

private:
    SemVer frameworkVersion_;
    std::shared_ptr<ICapabilityPolicy> policy_;
};

} // namespace Forsetti
