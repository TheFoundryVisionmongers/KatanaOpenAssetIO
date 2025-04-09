// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <FnAsset/plugin/FnAsset.h>

#include <openassetio/trait/TraitsData.hpp>
#include <openassetio/trait/collection.hpp>

class PublishStrategy
{
public:
    virtual ~PublishStrategy() = default;
    /**
     * @return the TraitSet related to publishing this type of asset.
     */
    [[nodiscard]] virtual const openassetio::trait::TraitSet& assetTraitSet() const = 0;

    [[nodiscard]] virtual openassetio::trait::TraitsDataPtr prePublishTraitData(
        const FnKat::Asset::StringMap& args) const = 0;

    [[nodiscard]] virtual openassetio::trait::TraitsDataPtr postPublishTraitData(
        const FnKat::Asset::StringMap& args) const = 0;
};

class PublishStrategies
{
public:
    PublishStrategies();

    const PublishStrategy& strategyForAssetType(const std::string& assetType) const;

private:
    std::unordered_map<std::string, std::unique_ptr<PublishStrategy>> strategies_;
};
