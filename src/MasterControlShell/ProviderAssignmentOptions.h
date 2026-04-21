// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace MasterControlShell {

struct AssignableProviderOption final {
    std::wstring providerId;
    std::wstring displayName;
};

template <typename Capability, typename Provider>
inline const Capability* findCapabilityForAssignment(
    const std::vector<Capability>& capabilities,
    const Provider& provider) {
    if (!provider.id.empty()) {
        const auto exact = std::find_if(
            capabilities.begin(),
            capabilities.end(),
            [&provider](const auto& capability) { return capability.providerId == provider.id; });
        if (exact != capabilities.end()) {
            return &(*exact);
        }

        const auto prefix = std::find_if(
            capabilities.begin(),
            capabilities.end(),
            [&provider](const auto& capability) {
                return !capability.providerId.empty() &&
                    provider.id.size() > capability.providerId.size() &&
                    provider.id[capability.providerId.size()] == L'-' &&
                    provider.id.compare(0, capability.providerId.size(), capability.providerId) == 0;
            });
        if (prefix != capabilities.end()) {
            return &(*prefix);
        }
    }

    const auto kindMatch = std::find_if(
        capabilities.begin(),
        capabilities.end(),
        [&provider](const auto& capability) { return capability.kind == provider.kind; });
    return kindMatch == capabilities.end() ? nullptr : &(*kindMatch);
}

template <typename Capability, typename Target>
inline bool providerSupportsAssignmentTarget(
    const Capability* capability,
    const Target& target) {
    if (capability == nullptr || target.kind != L"role") {
        return true;
    }

    return capability->supportedTargets.empty() ||
        std::find(capability->supportedTargets.begin(), capability->supportedTargets.end(), target.targetId) !=
            capability->supportedTargets.end();
}

template <typename Provider, typename Capability, typename Target>
inline bool isProviderAssignable(
    const Provider& provider,
    const std::vector<Capability>& capabilities,
    const Target& target) {
    if (!provider.enabled || !provider.credentialsConfigured) {
        return false;
    }

    return providerSupportsAssignmentTarget(findCapabilityForAssignment(capabilities, provider), target);
}

template <typename Provider, typename Capability, typename Target>
inline std::vector<AssignableProviderOption> buildAssignableProviderOptions(
    const std::vector<Provider>& providers,
    const std::vector<Capability>& capabilities,
    const Target& target) {
    std::vector<AssignableProviderOption> options;
    options.reserve(providers.size());
    for (const auto& provider : providers) {
        if (!isProviderAssignable(provider, capabilities, target)) {
            continue;
        }

        options.push_back(AssignableProviderOption{
            provider.id,
            provider.displayName.empty() ? provider.id : provider.displayName
        });
    }
    return options;
}

inline std::optional<size_t> findPreferredAssignableProviderIndex(
    const std::vector<AssignableProviderOption>& options,
    std::wstring_view primaryProviderId,
    std::wstring_view secondaryProviderId) {
    const auto findIndex = [&options](std::wstring_view providerId) -> std::optional<size_t> {
        if (providerId.empty()) {
            return std::nullopt;
        }

        const auto iterator = std::find_if(
            options.begin(),
            options.end(),
            [providerId](const auto& option) { return option.providerId == providerId; });
        if (iterator == options.end()) {
            return std::nullopt;
        }

        return static_cast<size_t>(std::distance(options.begin(), iterator));
    };

    if (const auto primaryIndex = findIndex(primaryProviderId); primaryIndex.has_value()) {
        return primaryIndex;
    }
    return findIndex(secondaryProviderId);
}

} // namespace MasterControlShell
