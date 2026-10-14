// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley, Raven Forge Software.
// SPDX-License-Identifier: MIT Patent Pending.

#pragma once
#include "ForsettiCore/ForsettiServiceContainer.h"
#include <memory>

namespace Forsetti {

// ---------------------------------------------------------------------------
// DefaultForsettiPlatformServices — registers all default Windows platform
// service implementations into a ServiceContainer.
//
// Port of DefaultForsettiPlatformServices from Swift.
// ---------------------------------------------------------------------------
class DefaultForsettiPlatformServices final {
public:
    /// Creates and registers all platform services into the given container:
    ///   - INetworkingService    -> WinHttpNetworkingService
    ///   - IStorageService       -> RegistryStorageService
    ///   - ISecureStorageService -> DpapiSecureStorageService
    ///   - IFileExportService    -> LocalFileExportService
    ///   - ITelemetryService     -> NoopTelemetryService
    static void registerAll(ServiceContainer& container);
};

} // namespace Forsetti
