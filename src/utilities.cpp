// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#include "utilities.hpp"

#include <string>

#include <FnAsset/plugin/FnAsset.h>

namespace utilities
{

namespace
{
bool getBooleanValue(const FnKat::Asset::StringMap& args, const std::string& key)
{
    const auto iter = args.find(key);
    if (iter == args.cend())
    {
        return false;
    }
    return iter->second == "True";
}
}  // anonymous namespace

bool shouldVersionUp(const FnKat::Asset::StringMap& args)
{
    return getBooleanValue(args, "versionUp");
}

bool shouldPublish(const FnKat::Asset::StringMap& args)
{
    return getBooleanValue(args, "publish");
}
}  // namespace utilities
