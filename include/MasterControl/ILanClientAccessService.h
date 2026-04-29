// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "MasterControl/LanClient.h"
#include "MasterControl/MasterControlModels.h"

#include <optional>
#include <string>
#include <vector>

namespace MasterControl {

// LAN client identity service. Owns persistence and lifecycle of the
// LanClient roster on a trusted LAN.
//
// Identity is by clientId alone. Implementations must:
//   - reject upserts with empty clientId
//   - normalize clientId casing for stable lookups (lowercase by convention)
//   - emit activity events for create / update / disable / enable / remove
//   - persist changes to AppConfiguration on disk
//
// Privilege checks live in Phase 6 middleware; this service is purely
// CRUD over the roster. CLU governance hooks land in Phase 7.
class ILanClientAccessService {
public:
    virtual std::vector<LanClient> listClients() const = 0;
    virtual std::optional<LanClient> getClient(const std::string& clientId) const = 0;

    virtual OperationResult upsertClient(const LanClient& client) = 0;
    virtual OperationResult disableClient(const std::string& clientId) = 0;
    virtual OperationResult enableClient(const std::string& clientId) = 0;
    virtual OperationResult removeClient(const std::string& clientId) = 0;

    // Phase 4 - privilege model. Privileges are flat booleans; setPrivileges
    // overwrites the whole struct atomically so partial updates are the
    // caller's responsibility (read-modify-write through getClient).
    virtual OperationResult setPrivileges(const std::string& clientId,
                                          const LanClientPrivileges& privileges) = 0;

    // Toggle autonomous mode for a client. Implementations apply a soft
    // gate: enabling autonomous mode is refused until CLU governance has
    // been expanded with the AutonomousModeEnable action kind in Phase 7
    // and `AppConfiguration::aiAutonomyEnabled` is true. Disabling is
    // always allowed.
    virtual OperationResult setAutonomousMode(const std::string& clientId,
                                              bool enabled) = 0;

    // Records the most recent observation of a client (timestamp + observed
    // network address). Phase 6 middleware calls this on every authenticated
    // request. In Phase 3 this is a no-op-friendly hook that callers may
    // skip; the contract is in place so middleware lands cleanly.
    virtual void touchClient(const std::string& clientId,
                             const std::string& observedAddress) = 0;

    virtual ~ILanClientAccessService() = default;
};

} // namespace MasterControl
