// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <FnAsset/plugin/FnAsset.h>

namespace utilities
{

// Returns true if args contains the "versionUp" argument
// indicating that the user has requested a new version is created.
bool shouldVersionUp(const FnKat::Asset::StringMap& args);

// Returns true if args contains the "publish" argument
// indicating that the user has requested this version set as the latest
// version.
bool shouldPublish(const FnKat::Asset::StringMap& args);
}  // namespace utilities
