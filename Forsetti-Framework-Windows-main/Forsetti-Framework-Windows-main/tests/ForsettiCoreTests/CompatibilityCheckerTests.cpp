// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "CppUnitTest.h"
#include "ForsettiCore/CompatibilityChecker.h"
#include "ForsettiCore/ForsettiVersion.h"
#include <memory>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Forsetti;

namespace {
    ModuleManifest makeCompatibleManifest() {
        return ModuleManifest{
            .schemaVersion         = "1.0",
            .moduleID              = "com.test.compatible",
            .displayName           = "Compatible Module",
            .moduleVersion         = SemVer{1, 0, 0},
            .moduleType            = ModuleType::Service,
            .supportedPlatforms    = { Platform::Windows },
            .minForsettiVersion    = SemVer{0, 1, 0},
            .maxForsettiVersion    = std::nullopt,
            .capabilitiesRequested = { Capability::Storage },
            .iapProductID          = std::nullopt,
            .entryPoint            = "CompatibleModule"
        };
    }
}

TEST_CLASS(CompatibilityCheckerTests)
{
public:

    TEST_METHOD(CompatibleModule_NoIssues)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        CompatibilityChecker checker(ForsettiVersion::current, policy);

        auto report = checker.checkCompatibility(makeCompatibleManifest());
        Assert::IsTrue(report.isCompatible());
        Assert::AreEqual(size_t(0), report.issues.size());
    }

    TEST_METHOD(UnsupportedSchemaVersion)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        CompatibilityChecker checker(ForsettiVersion::current, policy);

        auto manifest = makeCompatibleManifest();
        manifest.schemaVersion = "2.0";

        auto report = checker.checkCompatibility(manifest);
        Assert::IsFalse(report.isCompatible());

        bool hasSchemaIssue = false;
        for (const auto& issue : report.issues) {
            if (issue.code == CompatibilityIssueCode::UnsupportedSchemaVersion) {
                hasSchemaIssue = true;
            }
        }
        Assert::IsTrue(hasSchemaIssue);
    }

    TEST_METHOD(UnsupportedPlatform_Empty)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        CompatibilityChecker checker(ForsettiVersion::current, policy);

        auto manifest = makeCompatibleManifest();
        manifest.supportedPlatforms.clear();

        auto report = checker.checkCompatibility(manifest);
        Assert::IsFalse(report.isCompatible());

        bool hasPlatformIssue = false;
        for (const auto& issue : report.issues) {
            if (issue.code == CompatibilityIssueCode::UnsupportedPlatform) {
                hasPlatformIssue = true;
            }
        }
        Assert::IsTrue(hasPlatformIssue);
    }

    TEST_METHOD(IncompatibleMinForsettiVersion)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        // Use a very old framework version
        CompatibilityChecker checker(SemVer{0, 0, 1}, policy);

        auto manifest = makeCompatibleManifest();
        manifest.minForsettiVersion = SemVer{1, 0, 0};

        auto report = checker.checkCompatibility(manifest);
        Assert::IsFalse(report.isCompatible());

        bool hasVersionIssue = false;
        for (const auto& issue : report.issues) {
            if (issue.code == CompatibilityIssueCode::IncompatibleForsettiVersion) {
                hasVersionIssue = true;
            }
        }
        Assert::IsTrue(hasVersionIssue);
    }

    TEST_METHOD(IncompatibleMaxForsettiVersion)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        // Use a newer framework version than the module supports
        CompatibilityChecker checker(SemVer{2, 0, 0}, policy);

        auto manifest = makeCompatibleManifest();
        manifest.maxForsettiVersion = SemVer{1, 0, 0};

        auto report = checker.checkCompatibility(manifest);
        Assert::IsFalse(report.isCompatible());
    }

    TEST_METHOD(MaxForsettiVersion_NotExceeded)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        CompatibilityChecker checker(SemVer{0, 1, 0}, policy);

        auto manifest = makeCompatibleManifest();
        manifest.maxForsettiVersion = SemVer{1, 0, 0};

        auto report = checker.checkCompatibility(manifest);
        Assert::IsTrue(report.isCompatible());
    }

    TEST_METHOD(DeniedCapability)
    {
        // Policy that denies everything
        auto policy = std::make_shared<FixedCapabilityPolicy>(std::set<Capability>{});
        CompatibilityChecker checker(ForsettiVersion::current, policy);

        auto manifest = makeCompatibleManifest();
        manifest.capabilitiesRequested = { Capability::Networking };

        auto report = checker.checkCompatibility(manifest);
        Assert::IsFalse(report.isCompatible());

        bool hasDeniedIssue = false;
        for (const auto& issue : report.issues) {
            if (issue.code == CompatibilityIssueCode::DeniedCapability &&
                issue.severity == CompatibilitySeverity::Error) {
                hasDeniedIssue = true;
            }
        }
        Assert::IsTrue(hasDeniedIssue);
    }

    TEST_METHOD(AllowedCapability)
    {
        auto policy = std::make_shared<FixedCapabilityPolicy>(
            std::set<Capability>{ Capability::Storage });
        CompatibilityChecker checker(ForsettiVersion::current, policy);

        auto report = checker.checkCompatibility(makeCompatibleManifest());
        Assert::IsTrue(report.isCompatible());
    }

    TEST_METHOD(UIThemeMask_ReservedWarning)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        CompatibilityChecker checker(ForsettiVersion::current, policy);

        auto manifest = makeCompatibleManifest();
        manifest.capabilitiesRequested = { Capability::UIThemeMask };

        auto report = checker.checkCompatibility(manifest);

        // Should have a warning about UIThemeMask being reserved
        bool hasWarning = false;
        for (const auto& issue : report.issues) {
            if (issue.severity == CompatibilitySeverity::Warning &&
                issue.code == CompatibilityIssueCode::DeniedCapability) {
                hasWarning = true;
            }
        }
        Assert::IsTrue(hasWarning);
    }

    TEST_METHOD(ReportModuleID_MatchesManifest)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        CompatibilityChecker checker(ForsettiVersion::current, policy);

        auto manifest = makeCompatibleManifest();
        auto report = checker.checkCompatibility(manifest);

        Assert::AreEqual(std::string("com.test.compatible"), report.moduleID);
    }

    TEST_METHOD(NoCapabilities_NoIssues)
    {
        auto policy = std::make_shared<AllowAllCapabilityPolicy>();
        CompatibilityChecker checker(ForsettiVersion::current, policy);

        auto manifest = makeCompatibleManifest();
        manifest.capabilitiesRequested.clear();

        auto report = checker.checkCompatibility(manifest);
        Assert::IsTrue(report.isCompatible());
        Assert::AreEqual(size_t(0), report.issues.size());
    }

    TEST_METHOD(MultipleIssues_AllReported)
    {
        auto policy = std::make_shared<FixedCapabilityPolicy>(std::set<Capability>{});
        CompatibilityChecker checker(SemVer{0, 0, 1}, policy);

        auto manifest = makeCompatibleManifest();
        manifest.schemaVersion = "2.0";
        manifest.supportedPlatforms.clear();
        manifest.minForsettiVersion = SemVer{1, 0, 0};
        manifest.capabilitiesRequested = { Capability::Storage };

        auto report = checker.checkCompatibility(manifest);
        Assert::IsFalse(report.isCompatible());
        // Should have at least 4 issues: schema, platform, version, capability
        Assert::IsTrue(report.issues.size() >= 4);
    }
};
