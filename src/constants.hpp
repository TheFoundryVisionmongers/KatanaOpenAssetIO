// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace constants
{
inline const std::string kEntityReference = "__entityReference";
inline const std::string kManagerDrivenValue = "__managerDrivenValue";
constexpr std::string_view kAssetIdManagerDrivenValueSep = "#value=";
constexpr std::size_t kPageSize{256};
};  // namespace constants