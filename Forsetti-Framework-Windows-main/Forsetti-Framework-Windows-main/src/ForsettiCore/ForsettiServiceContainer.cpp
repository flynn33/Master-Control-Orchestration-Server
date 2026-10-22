// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley, Raven Forge Software.
// SPDX-License-Identifier: Apache-2.0
// Patent pending.

#include "ForsettiCore/ForsettiServiceContainer.h"

namespace Forsetti {

std::any ServiceContainer::resolveAny(const std::type_index& type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = services_.find(type);
    if (it != services_.end()) {
        return it->second;
    }
    return std::any{};
}

} // namespace Forsetti
