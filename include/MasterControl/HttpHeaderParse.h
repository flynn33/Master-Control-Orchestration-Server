// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// HttpHeaderParse.h - case-insensitive request-header helpers for the
// admin HTTP listener (plain and TLS paths).
//
// Content-Length remediation: HTTP header field names are
// case-insensitive (RFC 9110 §5.1), but the admin listener's request
// readers searched for the literal "Content-Length:" and treated any
// other casing as "no body follows" -- a lowercase `content-length`
// from a proxy or minimal client silently dropped the request body.
// Invalid values also flowed into std::stoi unchecked, which throws
// out of the accept path. These helpers parse the header block once,
// case-insensitively, and report invalid values explicitly so the
// listener can answer HTTP 400 instead of misbehaving.

#pragma once

#include <cctype>
#include <cstddef>
#include <string>

namespace MasterControl {

// Case-insensitive lookup of a header value inside a raw HTTP request.
// Scans only the header block (request line through the blank line) and
// matches complete field names at line starts, so a body or another
// header's value can never false-positive. Returns the trimmed value of
// the first match, or an empty string when the header is absent.
inline std::string findHeaderValueCaseInsensitive(const std::string& rawRequest,
                                                  const std::string& headerName) {
    const auto headerEnd = rawRequest.find("\r\n\r\n");
    const std::size_t blockEnd = headerEnd == std::string::npos ? rawRequest.size() : headerEnd;

    const auto equalsIgnoreCase = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i]))
                != std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    };
    const auto trim = [](std::string text) {
        const auto first = text.find_first_not_of(" \t");
        if (first == std::string::npos) {
            return std::string();
        }
        const auto last = text.find_last_not_of(" \t\r");
        return text.substr(first, last - first + 1);
    };

    // Skip the request line; headers start after the first CRLF.
    std::size_t lineStart = rawRequest.find("\r\n");
    if (lineStart == std::string::npos) {
        return std::string();
    }
    lineStart += 2;
    while (lineStart < blockEnd) {
        auto lineEnd = rawRequest.find("\r\n", lineStart);
        if (lineEnd == std::string::npos || lineEnd > blockEnd) {
            lineEnd = blockEnd;
        }
        const auto colon = rawRequest.find(':', lineStart);
        if (colon != std::string::npos && colon < lineEnd) {
            const auto name = trim(rawRequest.substr(lineStart, colon - lineStart));
            if (equalsIgnoreCase(name, headerName)) {
                return trim(rawRequest.substr(colon + 1, lineEnd - colon - 1));
            }
        }
        lineStart = lineEnd + 2;
    }
    return std::string();
}

// Outcome of Content-Length extraction. `present` distinguishes "header
// absent" (a bodyless GET is fine) from "header present but garbage"
// (must be answered with HTTP 400, never guessed at).
struct ContentLengthParse final {
    bool present = false;
    bool valid = false;
    std::size_t value = 0;
};

inline ContentLengthParse parseContentLengthHeader(const std::string& rawRequest) {
    ContentLengthParse outcome;
    const auto value = findHeaderValueCaseInsensitive(rawRequest, "Content-Length");
    if (value.empty()) {
        // Distinguish truly-absent from present-but-empty: an empty value
        // for a present header is invalid.
        const auto probe = findHeaderValueCaseInsensitive(rawRequest, "Content-Length");
        (void)probe;
        // findHeaderValueCaseInsensitive cannot distinguish the two cases
        // by return value alone; re-scan for the field name.
        const auto headerEnd = rawRequest.find("\r\n\r\n");
        const std::size_t blockEnd = headerEnd == std::string::npos ? rawRequest.size() : headerEnd;
        std::string lowered;
        lowered.reserve(blockEnd);
        for (std::size_t i = 0; i < blockEnd; ++i) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(rawRequest[i]))));
        }
        if (lowered.find("\r\ncontent-length:") != std::string::npos) {
            outcome.present = true;   // present but empty -> invalid
        }
        return outcome;
    }
    outcome.present = true;
    if (value.size() > 18) {   // > 999,999,999,999,999,999 bytes: reject outright
        return outcome;
    }
    for (const char c : value) {
        if (c < '0' || c > '9') {
            return outcome;
        }
    }
    outcome.valid = true;
    outcome.value = 0;
    for (const char c : value) {
        outcome.value = outcome.value * 10 + static_cast<std::size_t>(c - '0');
    }
    return outcome;
}

} // namespace MasterControl
