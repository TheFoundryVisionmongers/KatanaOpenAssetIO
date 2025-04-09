// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#include "PublishStrategies.hpp"

#include <memory>
#include <stdexcept>
#include <string>

#include <FnAsset/plugin/FnAsset.h>
#include <FnAsset/suite/FnAssetSuite.h>

#include <openassetio/trait/TraitsData.hpp>
#include <openassetio/trait/collection.hpp>

#include <openassetio_mediacreation/specifications/application/WorkfileSpecification.hpp>
#include <openassetio_mediacreation/specifications/threeDimensional/SceneGeometryResourceSpecification.hpp>
#include <openassetio_mediacreation/specifications/threeDimensional/SceneLightingResourceSpecification.hpp>
#include <openassetio_mediacreation/specifications/threeDimensional/ShaderResourceSpecification.hpp>
#include <openassetio_mediacreation/specifications/twoDimensional/DeepBitmapImageResourceSpecification.hpp>

namespace
{
template <typename T>
struct MediaCreationPublishStrategy : PublishStrategy
{
    [[nodiscard]] const openassetio::trait::TraitSet& assetTraitSet() const override
    {
        return T::kTraitSet;
    }

    [[nodiscard]] openassetio::trait::TraitsDataPtr prePublishTraitData(
        const FnKat::Asset::StringMap& args) const override
    {
        // TODO(DH): Populate with manager driven trait values
        (void)args;
        const auto specification = T::create();
        return specification.traitsData();
    }

    [[nodiscard]] openassetio::trait::TraitsDataPtr postPublishTraitData(
        const FnKat::Asset::StringMap& args) const override
    {
        // TODO(DH): Populate with manager katana driven trait values
        (void)args;
        const auto specification = T::create();
        return specification.traitsData();
    }
};

// Utility declarations
using WorkfileAssetPublisher = MediaCreationPublishStrategy<
    openassetio_mediacreation::specifications::application::WorkfileSpecification>;

using ImageAssetPublisher =
    MediaCreationPublishStrategy<openassetio_mediacreation::specifications::twoDimensional::
                                     DeepBitmapImageResourceSpecification>;

using SceneGeometryAssetPublisher =
    MediaCreationPublishStrategy<openassetio_mediacreation::specifications::threeDimensional::
                                     SceneGeometryResourceSpecification>;

using ShaderResourceAssetPublisher = MediaCreationPublishStrategy<
    openassetio_mediacreation::specifications::threeDimensional::ShaderResourceSpecification>;

using SceneLightingAssetPublisher =
    MediaCreationPublishStrategy<openassetio_mediacreation::specifications::threeDimensional::
                                     SceneLightingResourceSpecification>;

}  // anonymous namespace

PublishStrategies::PublishStrategies()
{
    strategies_[kFnAssetTypeKatanaScene] = std::make_unique<WorkfileAssetPublisher>();
    // TODO(DH): This would be better as something like ApplicationExtensionAssetPublisher...
    strategies_[kFnAssetTypeMacro] = std::make_unique<WorkfileAssetPublisher>();
    strategies_[kFnAssetTypeLiveGroup] = std::make_unique<WorkfileAssetPublisher>();
    strategies_[kFnAssetTypeImage] = std::make_unique<ImageAssetPublisher>();
    strategies_[kFnAssetTypeLookFile] = std::make_unique<WorkfileAssetPublisher>();
    strategies_[kFnAssetTypeLookFileMgrSettings] = std::make_unique<WorkfileAssetPublisher>();
    strategies_[kFnAssetTypeAlembic] = std::make_unique<SceneGeometryAssetPublisher>();
    strategies_[kFnAssetTypeCastingSheet] = std::make_unique<WorkfileAssetPublisher>();
    strategies_[kFnAssetTypeAttributeFile] = std::make_unique<WorkfileAssetPublisher>();
    strategies_[kFnAssetTypeFCurveFile] = std::make_unique<WorkfileAssetPublisher>();
    strategies_[kFnAssetTypeGafferThreeRig] = std::make_unique<SceneLightingAssetPublisher>();
    strategies_[kFnAssetTypeScenegraphBookmarks] = std::make_unique<WorkfileAssetPublisher>();
    strategies_[kFnAssetTypeShader] = std::make_unique<ShaderResourceAssetPublisher>();
}

const PublishStrategy& PublishStrategies::strategyForAssetType(const std::string& assetType) const
{
    try
    {
        return *strategies_.at(assetType);
    }
    catch (const std::out_of_range&)
    {
        // TODO(DH): Handle default asset publisher.
        throw std::runtime_error("Publishing '" + assetType + "' is currently unsupported.");
    }
}