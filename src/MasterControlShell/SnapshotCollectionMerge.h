// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include <algorithm>
#include <vector>

namespace MasterControlShell {

template <typename T, typename KeyFn>
std::vector<T> mergeAuthoritativeSnapshotCollection(
    const std::vector<T>& cached,
    std::vector<T> authoritative,
    const bool authoritativePresent,
    KeyFn keyFn) {
    if (!authoritativePresent) {
        return cached;
    }

    if (authoritative.empty()) {
        return {};
    }

    for (const auto& cachedItem : cached) {
        const auto cachedKey = keyFn(cachedItem);
        const auto duplicate = std::find_if(
            authoritative.begin(),
            authoritative.end(),
            [&keyFn, &cachedKey](const auto& authoritativeItem) {
                return keyFn(authoritativeItem) == cachedKey;
            });
        if (duplicate == authoritative.end()) {
            authoritative.push_back(cachedItem);
        }
    }

    return authoritative;
}

} // namespace MasterControlShell
