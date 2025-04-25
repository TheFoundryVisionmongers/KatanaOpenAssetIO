// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#include "PublishStrategies.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <FnAsset/plugin/FnAsset.h>
#include <FnAsset/suite/FnAssetSuite.h>

#include <openassetio/trait/TraitsData.hpp>
#include <openassetio/trait/collection.hpp>

#include <openassetio_mediacreation/specifications/application/WorkfileSpecification.hpp>
#include <openassetio_mediacreation/specifications/threeDimensional/SceneGeometryResourceSpecification.hpp>
#include <openassetio_mediacreation/specifications/threeDimensional/SceneLightingResourceSpecification.hpp>
#include <openassetio_mediacreation/specifications/threeDimensional/ShaderResourceSpecification.hpp>
#include <openassetio_mediacreation/specifications/twoDimensional/DeepBitmapImageResourceSpecification.hpp>

#include "constants.hpp"

PublishStrategy::PublishStrategy(FileUrlPathConverterPtr fileUrlPathConverter)
    : fileUrlPathConverter_{std::move(fileUrlPathConverter)}
{
}

namespace
{

using openassetio::trait::TraitsDataPtr;
using openassetio_mediacreation::specifications::application::WorkfileSpecification;

/**
 * Generic publishing strategy.
 *
 * Imbues the trait set of the templated Specification.
 *
 * Also sets the LocatableContentTrait location to the path from the
 * manager driven value encoded in the asset ID, if present.
 *
 * @tparam T OpenAssetIO Specification to imbue.
 */
template <typename T>
struct MediaCreationPublishStrategy : PublishStrategy
{
    using PublishStrategy::PublishStrategy;

    [[nodiscard]] const openassetio::trait::TraitSet& assetTraitSet() const override
    {
        return T::kTraitSet;
    }

    /**
     * Retrieve the trait data to be passed to `preflight()`.
     *
     * @param fields Dictionary from `getAssetFields()`.
     *
     * @param args Dictionary of args passed to `createAssetAndPath()`.
     */
    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        // TODO(DH): Populate with manager driven trait values
        (void)fields;
        (void)args;
        const auto specification = T::create();
        return specification.traitsData();
    }

    /**
     * Retrieve the trait data to be passed to `register()`.
     *
     * @param fields Dictionary from `getAssetFields()`.
     *
     * @param args Dictionary of args passed to `postCreateAsset()`.
     */
    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        // TODO(DH): Populate with manager katana driven trait values
        (void)args;
        const auto specification = T::create();

        if (const auto managerDrivenValueIter =
                fields.find(std::string{constants::kManagerDrivenValue});
            managerDrivenValueIter != fields.end())
        {
            // Assume that the managerDrivenValue is a path.
            specification.locatableContentTrait().setLocation(
                fileUrlPathConverter_->pathToUrl(managerDrivenValueIter->second));
        }

        return specification.traitsData();
    }
};

/**
 * Publish strategy for Katana LookFiles.
 *
 * These can be published either as a .klf archive, or as a directory
 * containing per-pass .klf and .attr files. So we disambiguate by
 * making use of the MIME type on the published LocatableContent trait.
 */
struct LookfileAssetPublisher : MediaCreationPublishStrategy<WorkfileSpecification>
{
    using MediaCreationPublishStrategy::MediaCreationPublishStrategy;

    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::prePublishTraitData(fields, args);
        imbueMimeType(args, traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueMimeType(args, traitsData);
        return traitsData;
    }

private:
    static void imbueMimeType(const FnKat::Asset::StringMap& args, const TraitsDataPtr& traitsData)
    {
        if (const auto keyAndValue = args.find("outputFormat"); keyAndValue != cend(args))
        {
            if (keyAndValue->second == "as directory")
            {
                WorkfileSpecification{traitsData}.locatableContentTrait().setMimeType(
                    "inode/directory");  // From xdg/shared-mime-info
            }
            else if (keyAndValue->second == "as archive")
            {
                WorkfileSpecification{traitsData}.locatableContentTrait().setMimeType(
                    "application/vnd.foundry.katana.lookfile");  // Invented
            }
        }
    }
};

// Utility declarations
using WorkfileAssetPublisher = MediaCreationPublishStrategy<WorkfileSpecification>;

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

PublishStrategies::PublishStrategies(const FileUrlPathConverterPtr& fileUrlPathConverter)
{
    strategies_[kFnAssetTypeKatanaScene] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    // TODO(DH): This would be better as something like ApplicationExtensionAssetPublisher...
    strategies_[kFnAssetTypeMacro] = std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLiveGroup] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeImage] = std::make_unique<ImageAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLookFile] =
        std::make_unique<LookfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLookFileMgrSettings] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeAlembic] =
        std::make_unique<SceneGeometryAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeCastingSheet] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeAttributeFile] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeFCurveFile] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeGafferThreeRig] =
        std::make_unique<SceneLightingAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeScenegraphBookmarks] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeShader] =
        std::make_unique<ShaderResourceAssetPublisher>(fileUrlPathConverter);
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