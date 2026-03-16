// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include "ForsettiCore/EntitlementProviders.h"

// Re-exports. On Windows, StaticEntitlementProvider and AllowAllEntitlementProvider
// from ForsettiCore are used directly.
//
// A future WindowsStoreEntitlementProvider would be added here to integrate
// with the Microsoft Store licensing and in-app purchase APIs.

namespace Forsetti {

// All entitlement providers are currently defined in ForsettiCore/EntitlementProviders.h:
//   - AllowAllEntitlementProvider
//   - StaticEntitlementProvider
//
// When Windows Store integration is implemented, add:
//   class WindowsStoreEntitlementProvider final : public IEntitlementProvider { ... };

} // namespace Forsetti
