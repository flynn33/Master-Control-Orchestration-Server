// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Shared user-facing copy for readiness and environment-hint UI. This is the
// single source of truth consumed by the WinUI shell, the MasterControlApp
// runtime, and (mirrored in JavaScript) the browser surface. A WS8 test
// asserts byte-for-byte equality between this header and the
// `READINESS_COPY` mirror in resources/web/app.js.

#pragma once

namespace MasterControl::ReadinessCopy {

// Environment-hint badges (WS3).
inline constexpr const char* kHintDetected = "Credential detected";
inline constexpr const char* kHintNeeded   = "Additional information needed";
inline constexpr const char* kHintNone     = "No configuration detected";

// Readiness category states (WS1, WS5, WS6).
inline constexpr const char* kReadinessReady          = "Ready";
inline constexpr const char* kReadinessNeedsAttention = "Needs Attention";
inline constexpr const char* kReadinessMissing        = "Missing";
inline constexpr const char* kReadinessFailed         = "Failed";

// Recommended-next-step stable ids (WS1).
inline constexpr const char* kNextConnectFirstProvider   = "connect-first-provider";
inline constexpr const char* kNextAddMcp                 = "add-mcp";
inline constexpr const char* kNextCreateSpecialist       = "create-specialist";
inline constexpr const char* kNextCreateStarterWorkflow  = "create-starter-workflow";
inline constexpr const char* kNextReview                 = "review";
inline constexpr const char* kNextComplete               = "complete";

} // namespace MasterControl::ReadinessCopy
