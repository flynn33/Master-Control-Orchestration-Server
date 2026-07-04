// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// McpToolNameResolver.h - shared gateway tool-name resolution policy.

#pragma once

#include "MasterControl/MasterControlModels.h"

#include <string>
#include <vector>

namespace MasterControl {

enum class McpToolNameResolutionStatus {
    Found,
    NotFound,
    Ambiguous
};

struct McpToolNameResolution final {
    McpToolNameResolutionStatus status = McpToolNameResolutionStatus::NotFound;
    std::string poolId;
    std::string localToolName;
    std::vector<std::string> requiredCapabilities;
};

class McpToolNameResolver final {
public:
    static McpToolNameResolution resolve(
        const std::vector<McpToolDescriptor>& catalog,
        const std::string& requestedName) {
        for (const auto& descriptor : catalog) {
            if (qualifiedName(descriptor) == requestedName) {
                return found(descriptor);
            }
        }

        McpToolNameResolution match;
        std::size_t unqualifiedMatches = 0;
        for (const auto& descriptor : catalog) {
            if (descriptor.toolName != requestedName) {
                continue;
            }
            ++unqualifiedMatches;
            if (unqualifiedMatches == 1) {
                match = found(descriptor);
            }
        }
        if (unqualifiedMatches > 1) {
            return McpToolNameResolution{ McpToolNameResolutionStatus::Ambiguous };
        }
        return match;
    }

private:
    static std::string qualifiedName(const McpToolDescriptor& descriptor) {
        return descriptor.serverName + "__" + descriptor.toolName;
    }

    static McpToolNameResolution found(const McpToolDescriptor& descriptor) {
        McpToolNameResolution resolution;
        resolution.status = McpToolNameResolutionStatus::Found;
        resolution.poolId = descriptor.serverName;
        resolution.localToolName = descriptor.toolName;
        resolution.requiredCapabilities = descriptor.requiredCapabilities;
        return resolution;
    }
};

} // namespace MasterControl
