// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include "ForsettiCore/ModuleModels.h"
#include <set>
#include <string>

namespace Forsetti {

// ---------------------------------------------------------------------------
// CapabilityPolicyDecision
// ---------------------------------------------------------------------------
enum class CapabilityPolicyDecision {
    Allowed,
    Denied
};

// ---------------------------------------------------------------------------
// ICapabilityPolicy – determines whether a module may exercise a capability.
// ---------------------------------------------------------------------------
class ICapabilityPolicy {
public:
    virtual CapabilityPolicyDecision evaluate(
        const std::string& moduleID,
        Capability capability) const = 0;

    virtual ~ICapabilityPolicy() = default;
};

// ---------------------------------------------------------------------------
// AllowAllCapabilityPolicy – unconditionally allows every capability.
// ---------------------------------------------------------------------------
class AllowAllCapabilityPolicy final : public ICapabilityPolicy {
public:
    CapabilityPolicyDecision evaluate(
        const std::string& moduleID,
        Capability capability) const override;
};

// ---------------------------------------------------------------------------
// FixedCapabilityPolicy – allows only the capabilities in a fixed set.
// ---------------------------------------------------------------------------
class FixedCapabilityPolicy final : public ICapabilityPolicy {
public:
    explicit FixedCapabilityPolicy(std::set<Capability> allowedCapabilities);

    CapabilityPolicyDecision evaluate(
        const std::string& moduleID,
        Capability capability) const override;

private:
    std::set<Capability> allowedCapabilities_;
};

} // namespace Forsetti
