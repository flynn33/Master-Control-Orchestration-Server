// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// QueryParamParse.h - lightweight URL query-string extractor.
//
// Added in v0.10.15 to fix a silent-ignore defect on operator-natural
// alias query-parameter names (e.g. ?limit= for the canonical ?max= on
// /api/activity). Prior to v0.10.15 the route layer called
// `query.find("max=")` directly; any other natural alias an operator
// might reasonably type produced an unparameterized response (the full
// 512-event ring at 138 KB), with no log entry and no error. This
// violated the project's honest-unavailable-sentinel rule.
//
// The helpers below are boundary-aware -- the substring match is
// anchored either to the start of the query string or to a preceding
// '&'. This prevents a search for "max" from incidentally matching
// inside another parameter named "xmax" (the v0.10.x route layer had
// never exercised that edge case, but a future param could).
//
// All functions are non-throwing and return an empty string when no
// match is found.

#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace MasterControl {

// Extract a single named query parameter from the raw query string
// (no leading '?'). Returns the value (everything between '=' and
// the next '&' or end-of-string) or an empty string when not present.
//
// The match is anchored to a name boundary -- either start-of-query
// or immediately after an '&'. URL decoding is intentionally not
// performed; existing callers handled their own decoding (or, for
// numeric/identifier params, did not need it).
inline std::string extractQueryParam(std::string_view query,
                                     std::string_view name) {
    if (query.empty() || name.empty()) {
        return std::string();
    }

    // Build the "<name>=" anchor.
    std::string needle;
    needle.reserve(name.size() + 1);
    needle.append(name.data(), name.size());
    needle.push_back('=');

    std::string_view::size_type valueStart = std::string_view::npos;

    // Boundary 1: query starts with "<name>=...".
    if (query.size() >= needle.size() &&
        query.compare(0, needle.size(), needle) == 0) {
        valueStart = needle.size();
    } else {
        // Boundary 2: "&<name>=..." appears anywhere in the string.
        std::string altNeedle;
        altNeedle.reserve(needle.size() + 1);
        altNeedle.push_back('&');
        altNeedle.append(needle);
        const auto p = query.find(altNeedle);
        if (p == std::string_view::npos) {
            return std::string();
        }
        valueStart = p + altNeedle.size();
    }

    auto value = query.substr(valueStart);
    const auto amp = value.find('&');
    if (amp != std::string_view::npos) {
        value = value.substr(0, amp);
    }
    return std::string(value);
}

// Try each candidate name in order and return the value of the first
// match with a NON-EMPTY value. Returns an empty string when none
// of the candidates appear, OR when every candidate that does appear
// has an empty value (e.g. `?max=&limit=10` returns "10" because the
// `max=` candidate has an empty value).
//
// Empty-value semantics: a present-but-empty query parameter (`name=`
// with nothing after the `=`) is treated as "fall through to the
// next alias" rather than "present with empty value". This matches
// the route-handler convention upstream of this helper, where every
// caller already does `if (!value.empty()) { ... try parse ... } else
// { fall through to default cap }`. A future helper variant that
// distinguishes present-empty from absent could be added when the
// route surface requires that distinction; today no route does.
//
// Use when a canonical wire name has well-known operator-natural
// aliases. List the canonical name first so callers that supply only
// canonical (with a non-empty value) get canonical-wins semantics.
//
// Example:
//   const auto maxValue = MasterControl::extractQueryParamAny(
//       request.query, {"max", "limit", "count", "n", "top"});
inline std::string extractQueryParamAny(
    std::string_view query,
    std::initializer_list<std::string_view> candidates) {
    for (const auto& name : candidates) {
        auto v = extractQueryParam(query, name);
        if (!v.empty()) {
            return v;
        }
    }
    return std::string();
}

} // namespace MasterControl
