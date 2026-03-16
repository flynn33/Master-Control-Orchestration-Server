// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/CapabilityPolicy.h"
#include <utility>

namespace Forsetti {

// ---------------------------------------------------------------------------
// AllowAllCapabilityPolicy
// ---------------------------------------------------------------------------
CapabilityPolicyDecision AllowAllCapabilityPolicy::evaluate(
    const std::string& /*moduleID*/,
    Capability /*capability*/) const
{
    return CapabilityPolicyDecision::Allowed;
}

// ---------------------------------------------------------------------------
// FixedCapabilityPolicy
// ---------------------------------------------------------------------------
FixedCapabilityPolicy::FixedCapabilityPolicy(std::set<Capability> allowedCapabilities)
    : allowedCapabilities_(std::move(allowedCapabilities))
{
}

CapabilityPolicyDecision FixedCapabilityPolicy::evaluate(
    const std::string& /*moduleID*/,
    Capability capability) const
{
    if (allowedCapabilities_.contains(capability)) {
        return CapabilityPolicyDecision::Allowed;
    }
    return CapabilityPolicyDecision::Denied;
}

} // namespace Forsetti
