// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// JsonMerge.h - deep-merge and full-document helpers backing the
// PATCH /api/config partial-update route.
//
// Added for the config-safety remediation. Pre-remediation the only
// configuration write path was POST /api/config with full-replacement
// semantics: `nlohmann::json::parse(body).get<AppConfiguration>()`
// defaults every omitted section, so a partial body silently reset
// unrelated configuration (bind address, seeded endpoints, security
// settings) at rest. PATCH deep-merges the patch body into the current
// document instead, and POST now rejects partial top-level bodies.
//
// The helpers are header-only free functions (same posture as
// JsonStrictness.h) so both the AdminApiService implementation and the
// bool-style test suite exercise the exact production logic.

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace MasterControl {

// Object-recursive deep merge: nested objects merge key-by-key; arrays,
// scalars, and nulls replace the target value wholesale. Non-object
// targets (or patches) are replaced entirely.
inline void deepMergeObject(nlohmann::json& target, const nlohmann::json& patch) {
    if (!target.is_object() || !patch.is_object()) {
        target = patch;
        return;
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (target.contains(it.key()) && target[it.key()].is_object() && it.value().is_object()) {
            deepMergeObject(target[it.key()], it.value());
        } else {
            target[it.key()] = it.value();
        }
    }
}

// Outcome of validating + merging a configuration patch body. `valid` is
// false when the patch is not a JSON object; `merged` carries the deep-merged
// document only when valid.
struct ConfigurationPatchOutcome final {
    bool valid = false;
    std::string message;
    nlohmann::json merged;
};

// Validates and applies a configuration patch against the current document.
// This is the exact logic behind PATCH /api/config: object-recursive deep
// merge of the patch into the current serialized configuration.
inline ConfigurationPatchOutcome mergeConfigurationPatch(const nlohmann::json& current,
                                                         const nlohmann::json& patch) {
    ConfigurationPatchOutcome outcome;
    if (!patch.is_object()) {
        outcome.message = "Configuration patch body must be a JSON object of fields to merge.";
        return outcome;
    }
    outcome.merged = current;
    deepMergeObject(outcome.merged, patch);
    outcome.valid = true;
    return outcome;
}

// Top-level keys a full-replacement document must carry, derived from the
// canonical serialization of a model instance. A body missing any of these
// keys is a partial document: deserializing it as the full model would
// reset each omitted section to defaults. Returns the missing key names
// (empty when the body is complete). Non-object bodies return empty --
// callers reject those separately with a clearer message.
template <typename T>
inline std::vector<std::string> missingTopLevelKeysForFullDocument(const nlohmann::json& body,
                                                                   const T& model) {
    std::vector<std::string> missing;
    if (!body.is_object()) {
        return missing;
    }
    const nlohmann::json echoed = model;
    if (!echoed.is_object()) {
        return missing;
    }
    for (auto it = echoed.begin(); it != echoed.end(); ++it) {
        if (!body.contains(it.key())) {
            missing.push_back(it.key());
        }
    }
    return missing;
}

} // namespace MasterControl
