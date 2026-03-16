// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "ForsettiCore/ActivationStore.h"

namespace Forsetti {

// ---------------------------------------------------------------------------
// to_json
// ---------------------------------------------------------------------------
void to_json(nlohmann::json& j, const ActivationState& s) {
    j = nlohmann::json{
        {"enabledServiceModuleIDs", s.enabledServiceModuleIDs},
        {"enabledUIModuleIDs",      s.enabledUIModuleIDs},
        {"selectedUIModuleID",      s.selectedUIModuleID}
    };
}

// ---------------------------------------------------------------------------
// from_json – backward-compatible: falls back to legacy key
//             "activeUIModuleID" when "selectedUIModuleID" is absent.
// ---------------------------------------------------------------------------
void from_json(const nlohmann::json& j, ActivationState& s) {
    if (j.contains("enabledServiceModuleIDs")) {
        j.at("enabledServiceModuleIDs").get_to(s.enabledServiceModuleIDs);
    }

    if (j.contains("enabledUIModuleIDs")) {
        j.at("enabledUIModuleIDs").get_to(s.enabledUIModuleIDs);
    }

    // Prefer the current key; fall back to the legacy key.
    if (j.contains("selectedUIModuleID") && !j.at("selectedUIModuleID").is_null()) {
        j.at("selectedUIModuleID").get_to(s.selectedUIModuleID);
    } else if (j.contains("activeUIModuleID") && !j.at("activeUIModuleID").is_null()) {
        s.selectedUIModuleID = j.at("activeUIModuleID").get<std::string>();
    } else {
        s.selectedUIModuleID = std::nullopt;
    }
}

} // namespace Forsetti
