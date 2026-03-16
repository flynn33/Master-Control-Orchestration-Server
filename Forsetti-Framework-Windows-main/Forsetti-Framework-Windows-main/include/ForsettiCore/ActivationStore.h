// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#pragma once
#include <string>
#include <set>
#include <optional>
#include <nlohmann/json.hpp>

namespace Forsetti {

// ---------------------------------------------------------------------------
// ActivationState – persistent record of which modules are enabled / selected.
// ---------------------------------------------------------------------------
struct ActivationState final {
    std::set<std::string>          enabledServiceModuleIDs;
    std::set<std::string>          enabledUIModuleIDs;
    std::optional<std::string>     selectedUIModuleID;

    bool operator==(const ActivationState&) const = default;
};

// JSON round-trip support (nlohmann).
void to_json(nlohmann::json& j, const ActivationState& s);
void from_json(const nlohmann::json& j, ActivationState& s);

// ---------------------------------------------------------------------------
// IActivationStore – abstract persistence for ActivationState.
// ---------------------------------------------------------------------------
class IActivationStore {
public:
    virtual ActivationState loadState() const        = 0;
    virtual void            saveState(const ActivationState& state) = 0;

    virtual ~IActivationStore() = default;
};

} // namespace Forsetti
