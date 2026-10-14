// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley, Raven Forge Software.
// SPDX-License-Identifier: MIT Patent Pending.

#pragma once

#include "ForsettiCore/SemVer.h"

namespace Forsetti {

/// Current framework version. Mirrors ForsettiVersion.swift.
struct ForsettiVersion final {
    static inline const SemVer current{0, 1, 0};
};

} // namespace Forsetti
