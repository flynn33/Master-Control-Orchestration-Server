// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley, Raven Forge Software.
// SPDX-License-Identifier: Apache-2.0
// Patent pending.

#include "ForsettiPlatform/DefaultPlatformServices.h"
#include "ForsettiPlatform/PlatformServices.h"

namespace Forsetti {

void DefaultForsettiPlatformServices::registerAll(ServiceContainer& container) {
    container.registerService<INetworkingService>(
        std::make_shared<WinHttpNetworkingService>());

    container.registerService<IStorageService>(
        std::make_shared<RegistryStorageService>());

    container.registerService<ISecureStorageService>(
        std::make_shared<DpapiSecureStorageService>());

    container.registerService<IFileExportService>(
        std::make_shared<LocalFileExportService>());

    container.registerService<ITelemetryService>(
        std::make_shared<NoopTelemetryService>());
}

} // namespace Forsetti
