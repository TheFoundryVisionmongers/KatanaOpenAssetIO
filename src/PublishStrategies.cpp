// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#include "PublishStrategies.hpp"

#include <memory>
#include <set>
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
#include <openassetio_mediacreation/specifications/twoDimensional/BitmapImageResourceSpecification.hpp>
#include <openassetio_mediacreation/traits/application/ConfigTrait.hpp>
#include <openassetio_mediacreation/traits/color/OCIOColorManagedTrait.hpp>
#include <openassetio_mediacreation/traits/content/LocatableContentTrait.hpp>
#include <openassetio_mediacreation/traits/identity/DisplayNameTrait.hpp>
#include <openassetio_mediacreation/traits/twoDimensional/DeepTrait.hpp>

#include "constants.hpp"

PublishStrategy::PublishStrategy(FileUrlPathConverterPtr fileUrlPathConverter)
    : fileUrlPathConverter_{std::move(fileUrlPathConverter)}
{
}

namespace
{

using openassetio::trait::TraitsDataPtr;
using openassetio_mediacreation::specifications::application::WorkfileSpecification;
using openassetio_mediacreation::specifications::threeDimensional::
    SceneLightingResourceSpecification;
using openassetio_mediacreation::specifications::twoDimensional::BitmapImageResourceSpecification;
using openassetio_mediacreation::traits::application::ConfigTrait;
using openassetio_mediacreation::traits::color::OCIOColorManagedTrait;
using openassetio_mediacreation::traits::content::LocatableContentTrait;
using openassetio_mediacreation::traits::identity::DisplayNameTrait;
using openassetio_mediacreation::traits::twoDimensional::DeepTrait;

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
 * Katana scene file `.katana` publishing.
 *
 * `args` passed to `createAssetAndPath()` (from
 * KatanaFile.CreateSceneAsset):
 * - versionUp: Flag that controls whether to create a new version.
 * - publish: Flag that controls whether to publish the resulting
 *   scene as the current version.
 *
 * These are set as follows:
 * - "File"->"Version Up and Save" sets both to true.
 * - "File"->"Save" sets both to false.
 * - "File"->"Save As" sets both to false by default, but can be
 *   modified by asset browser.
 * - "File"->"Export Selection" sets both to false by default, but
 *   can be modified by asset browser.
 *
 * The "versionUp" flag is handled generically in `createAssetAndPath()`
 * using a relationship query to signal to the manager that we want
 * to target an explicit version.
 */
struct KatanaSceneAssetPublisher : MediaCreationPublishStrategy<WorkfileSpecification>
{
    using MediaCreationPublishStrategy::MediaCreationPublishStrategy;

    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::prePublishTraitData(fields, args);
        imbueMimeType(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueMimeType(traitsData);
        return traitsData;
    }

private:
    static void imbueMimeType(const TraitsDataPtr& traitsData)
    {
        LocatableContentTrait(traitsData)
            .setMimeType("application/vnd.foundry.katana.project");  // Invented
    }
};

/**
 * Publish strategy for LiveGroups.
 *
 * These are Katana scene files containing a single group, exported as
 * XML.
 *
 * No additional metadata is given when publishing, so we just set a
 * MIME type.
 */
struct LiveGroupAssetPublisher : MediaCreationPublishStrategy<WorkfileSpecification>
{
    using MediaCreationPublishStrategy::MediaCreationPublishStrategy;

    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::prePublishTraitData(fields, args);
        imbueMimeType(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueMimeType(traitsData);
        return traitsData;
    }

private:
    static void imbueMimeType(const TraitsDataPtr& traitsData)
    {
        LocatableContentTrait(traitsData)
            .setMimeType("application/vnd.foundry.katana.livegroup+xml");  // Invented
    }
};

/**
 * Publish strategy for Katana LookFiles.
 *
 * By default, these can be published either as a .klf archive, or as a
 * directory containing per-pass .klf and .attr files.
 *
 * LookFileBakeAPI.RegisterOutputFormat() can be used to add yet more
 * output formats.
 *
 * An output format is usually expected to create multiple files, so the
 * asset system should return a writeable directory. The default "as
 * archive" is a special case.
 *
 * We disambiguate between "as archive" and other formats using the
 * MIME type.
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
            if (keyAndValue->second == "as archive")
            {
                LocatableContentTrait{traitsData}.setMimeType(
                    "application/vnd.foundry.katana.lookfile");  // Invented
            }
            else
            {
                LocatableContentTrait{traitsData}.setMimeType(
                    "inode/directory");  // From xdg/shared-mime-info
            }
        }
    }
};

/**
 * Publish strategy for exported LookFileManager settings.
 *
 * I.e. LookFileManager parameters->(right-click)->Import/Export->Export
 * Manager Settings.
 *
 * This is an XML document, though with a `.lfmsexport` file extension.
 *
 * We add a MIME type, as well as imbue the `Config` trait to signal
 * that this is purely settings.
 */
struct LookFileManagerSettingsPublisher : MediaCreationPublishStrategy<WorkfileSpecification>
{
    using MediaCreationPublishStrategy::MediaCreationPublishStrategy;

    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::prePublishTraitData(fields, args);
        imbueTraitAndMime(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueTraitAndMime(traitsData);
        return traitsData;
    }

private:
    static void imbueTraitAndMime(const TraitsDataPtr& traitsData)
    {
        ConfigTrait::imbueTo(traitsData);

        LocatableContentTrait(traitsData)
            .setMimeType("application/vnd.foundry.katana.lookfilemanager-settings+xml");  // Invented
    }
};

/**
 * Publish strategy for GafferThree exported rigs.
 *
 * I.e. GafferThree parameters->(right-click)->Export Rig.
 *
 * This is an XML document, though with a `.rig` file extension.
 */
struct GafferThreeRigPublisher : MediaCreationPublishStrategy<SceneLightingResourceSpecification>
{
    using MediaCreationPublishStrategy::MediaCreationPublishStrategy;

    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::prePublishTraitData(fields, args);
        imbueMimeType(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueMimeType(traitsData);
        return traitsData;
    }

private:
    static void imbueMimeType(const TraitsDataPtr& traitsData)
    {
        LocatableContentTrait(traitsData)
            .setMimeType("application/vnd.foundry.katana.rig+xml");  // Invented
    }
};

/**
 * Publish strategy for images.
 */
struct ImageAssetPublisher final : MediaCreationPublishStrategy<BitmapImageResourceSpecification>
{
    using MediaCreationPublishStrategy::MediaCreationPublishStrategy;

    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::prePublishTraitData(fields, args);
        updateTraitsFromArgs(args, traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        updateTraitsFromArgs(args, traitsData);
        return traitsData;
    }

private:
    static inline const FnKat::Asset::StringMap kExtToMimeMap{
        {"exr", "image/x-exr"},                                    // From xdg/shared-mime-info
        {"deepexr", "image/x-exr"},                                // Assume same as .exr
        {"png", "image/png"},                                      // From iana.org
        {"tif", "image/tiff"},                                     // From iana.org
        {"jpg", "image/jpeg"},                                     // From iana.org
        {"rla", "image/x-rla"},                                    // Unofficial
        {"dtex", "image/x-dtex"},                                  // Invented
        {"deepshad", "image/x-deepshad"},                          // Invented
        {"hist", "application/vnd.foundry.katana.histogram+xml"},  // Invented
    };
    static inline const std::set<std::string> kDeepExts{"deepexr", "deepshad", "dtex"};

    static void updateTraitsFromArgs(const FnKat::Asset::StringMap& args,
                                     const TraitsDataPtr& traitsData)
    {
        // Colour space.
        if (const auto keyAndValue = args.find("colorspace"); keyAndValue != cend(args))
        {
            OCIOColorManagedTrait{traitsData}.setColorspace(keyAndValue->second);
        }

        // Display name
        if (const auto keyAndValue = args.find("outputName"); keyAndValue != cend(args))
        {
            DisplayNameTrait displayNameTrait{traitsData};
            displayNameTrait.setName(keyAndValue->second);
            displayNameTrait.setQualifiedName(keyAndValue->second);
        }

        // MIME type and Deep trait.
        if (const auto keyAndValue = args.find("ext"); keyAndValue != cend(args))
        {
            if (const auto extAndMime = kExtToMimeMap.find(keyAndValue->second);
                extAndMime != cend(kExtToMimeMap))
            {
                LocatableContentTrait{traitsData}.setMimeType(extAndMime->second);
            }

            if (kDeepExts.find(keyAndValue->second) != cend(kDeepExts))
            {
                DeepTrait::imbueTo(traitsData);
            }
        }
    }
};

// Utility declarations
using WorkfileAssetPublisher = MediaCreationPublishStrategy<WorkfileSpecification>;

using SceneGeometryAssetPublisher =
    MediaCreationPublishStrategy<openassetio_mediacreation::specifications::threeDimensional::
                                     SceneGeometryResourceSpecification>;

using ShaderResourceAssetPublisher = MediaCreationPublishStrategy<
    openassetio_mediacreation::specifications::threeDimensional::ShaderResourceSpecification>;

}  // anonymous namespace

PublishStrategies::PublishStrategies(const FileUrlPathConverterPtr& fileUrlPathConverter)
{
    strategies_[kFnAssetTypeKatanaScene] =
        std::make_unique<KatanaSceneAssetPublisher>(fileUrlPathConverter);
    // TODO(DH): This would be better as something like ApplicationExtensionAssetPublisher...
    strategies_[kFnAssetTypeMacro] = std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLiveGroup] =
        std::make_unique<LiveGroupAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeImage] = std::make_unique<ImageAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLookFile] =
        std::make_unique<LookfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLookFileMgrSettings] =
        std::make_unique<LookFileManagerSettingsPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeAlembic] =
        std::make_unique<SceneGeometryAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeCastingSheet] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeAttributeFile] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeFCurveFile] =
        std::make_unique<WorkfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeGafferThreeRig] =
        std::make_unique<GafferThreeRigPublisher>(fileUrlPathConverter);
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