// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/CompatibilityChecker.h"
#include <algorithm>

namespace Forsetti {

// MARK: - CompatibilityReport

bool CompatibilityReport::isCompatible() const {
    return std::none_of(issues.begin(), issues.end(), [](const CompatibilityIssue& issue) {
        return issue.severity == CompatibilitySeverity::Error;
    });
}

// MARK: - CompatibilityChecker

CompatibilityChecker::CompatibilityChecker(SemVer frameworkVersion, std::shared_ptr<ICapabilityPolicy> policy)
    : frameworkVersion_(std::move(frameworkVersion))
    , policy_(std::move(policy)) {}

CompatibilityReport CompatibilityChecker::checkCompatibility(const ModuleManifest& manifest) const {
    CompatibilityReport report;
    report.moduleID = manifest.moduleID;

    // 1. Schema version must be "1.0"
    if (manifest.schemaVersion != "1.0") {
        report.issues.push_back({
            "Unsupported schema version: " + manifest.schemaVersion + ". Only \"1.0\" is supported.",
            CompatibilitySeverity::Error,
            CompatibilityIssueCode::UnsupportedSchemaVersion
        });
    }

    // 2. Platform::Windows must be in supportedPlatforms
    {
        bool windowsSupported = std::any_of(
            manifest.supportedPlatforms.begin(),
            manifest.supportedPlatforms.end(),
            [](Platform p) { return p == Platform::Windows; }
        );

        if (!windowsSupported) {
            report.issues.push_back({
                "Module \"" + manifest.displayName + "\" does not support Windows.",
                CompatibilitySeverity::Error,
                CompatibilityIssueCode::UnsupportedPlatform
            });
        }
    }

    // 3. Framework version >= minForsettiVersion
    if (frameworkVersion_ < manifest.minForsettiVersion) {
        report.issues.push_back({
            "Module requires Forsetti version " + manifest.minForsettiVersion.toString()
                + " or later, but framework version is " + frameworkVersion_.toString() + ".",
            CompatibilitySeverity::Error,
            CompatibilityIssueCode::IncompatibleForsettiVersion
        });
    }

    // 4. If maxForsettiVersion exists, framework version <= maxForsettiVersion
    if (manifest.maxForsettiVersion.has_value()) {
        if (manifest.maxForsettiVersion.value() < frameworkVersion_) {
            report.issues.push_back({
                "Module supports Forsetti version up to " + manifest.maxForsettiVersion->toString()
                    + ", but framework version is " + frameworkVersion_.toString() + ".",
                CompatibilitySeverity::Error,
                CompatibilityIssueCode::IncompatibleForsettiVersion
            });
        }
    }

    // 5. Each capability checked against policy
    for (const auto& capability : manifest.capabilitiesRequested) {
        if (policy_) {
            auto decision = policy_->evaluate(manifest.moduleID, capability);
            if (decision == CapabilityPolicyDecision::Denied) {
                report.issues.push_back({
                    "Capability \"" + to_string(capability) + "\" is denied by the current policy.",
                    CompatibilitySeverity::Error,
                    CompatibilityIssueCode::DeniedCapability
                });
            }
        }

        // 6. If UIThemeMask capability requested, add a warning
        if (capability == Capability::UIThemeMask) {
            report.issues.push_back({
                "UIThemeMask is reserved for the framework.",
                CompatibilitySeverity::Warning,
                CompatibilityIssueCode::DeniedCapability
            });
        }
    }

    return report;
}

} // namespace Forsetti
