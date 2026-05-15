// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// JsonStrictness.h - operator-visible feedback when a JSON POST body
// carries top-level keys the destination model does not recognise.
//
// Added in v0.10.17. Pre-v0.10.17 the route layer used the canonical
// nlohmann pattern `nlohmann::json::parse(body).get<T>()` which silently
// dropped any field not declared by the model's NLOHMANN_DEFINE_TYPE
// (or hand-written from_json). An operator typing `enabledFlag` instead
// of `enabled` got HTTP 200 with the default value applied and no
// indication anything went wrong.
//
// The helper below is intentionally NON-INVASIVE:
//
//   - It does NOT change the HTTP status code (no 400 on unknowns).
//     Pre-v0.10.17 clients keep working unchanged.
//   - It does NOT prevent the model from being constructed. Whatever
//     the model parser accepted is what the route persists.
//   - It DOES surface a `droppedKeys` array in the route's response
//     body when the input carried unknown top-level fields, so the
//     operator's next dashboard view (or curl) sees the diagnostic.
//
// Detection algorithm: round-trip the typed model back through
// nlohmann::json(model) to discover the set of keys the model
// understands, then list any input keys that are not in that set.
//
// Top-level scoping: this helper does not descend into nested
// objects. A future iteration can recurse if operator feedback warrants.

#pragma once

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

namespace MasterControl {

// Compare an input JSON object against the keys produced by
// re-serialising a typed model. Returns the input keys that are not
// in the round-tripped key set, in input-iteration order.
//
// Returns an empty vector when:
//   - `input` is not a JSON object (arrays, scalars, null).
//   - The round-tripped `model` is not a JSON object.
//   - Every input key has a corresponding round-tripped key.
template <typename T>
inline std::vector<std::string> collectDroppedTopLevelKeys(
    const nlohmann::json& input, const T& model) {
    std::vector<std::string> dropped;
    if (!input.is_object()) {
        return dropped;
    }
    const nlohmann::json echoed = model; // implicit to_json via ADL
    if (!echoed.is_object()) {
        return dropped;
    }
    std::set<std::string> known;
    for (auto it = echoed.begin(); it != echoed.end(); ++it) {
        known.insert(it.key());
    }
    for (auto it = input.begin(); it != input.end(); ++it) {
        if (known.find(it.key()) == known.end()) {
            dropped.push_back(it.key());
        }
    }
    return dropped;
}

// Convenience: serialise the dropped-key list as a nlohmann::json
// array. Returns an empty array when the input list is empty so
// callers can unconditionally assign `resp["droppedKeys"] = ...`.
inline nlohmann::json droppedKeysToJson(const std::vector<std::string>& dropped) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& k : dropped) {
        arr.push_back(k);
    }
    return arr;
}

} // namespace MasterControl
