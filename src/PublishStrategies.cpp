// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#include "PublishStrategies.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <FnAsset/FnDefaultFileSequencePlugin.h>
#include <FnAsset/plugin/FnAsset.h>
#include <FnAsset/suite/FnAssetSuite.h>
#include <pystring/pystring.h>

#include <openassetio/trait/collection.hpp>

#include <openassetio_mediacreation/specifications/application/WorkfileSpecification.hpp>
#include <openassetio_mediacreation/specifications/threeDimensional/SceneLightingResourceSpecification.hpp>
#include <openassetio_mediacreation/specifications/twoDimensional/BitmapImageResourceSpecification.hpp>
#include <openassetio_mediacreation/traits/application/ConfigTrait.hpp>
#include <openassetio_mediacreation/traits/color/OCIOColorManagedTrait.hpp>
#include <openassetio_mediacreation/traits/content/LocatableContentTrait.hpp>
#include <openassetio_mediacreation/traits/identity/DisplayNameTrait.hpp>
#include <openassetio_mediacreation/traits/timeDomain/FrameRangedTrait.hpp>
#include <openassetio_mediacreation/traits/twoDimensional/DeepTrait.hpp>

#include <katana_openassetio/traits/application/LookFileTrait.hpp>
#include <katana_openassetio/traits/application/MacroTrait.hpp>
#include <katana_openassetio/traits/application/ProjectTrait.hpp>
#include <katana_openassetio/traits/application/SceneGraphBookmarksTrait.hpp>
#include <katana_openassetio/traits/nodes/GafferThreeTrait.hpp>
#include <katana_openassetio/traits/nodes/LiveGroupTrait.hpp>
#include <katana_openassetio/traits/nodes/LookFileManagerTrait.hpp>
#include <katana_openassetio/traits/timeDomain/FCurveTrait.hpp>
#include <katana_openassetio/traits/twoDimensional/PresetResolutionTrait.hpp>

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
using openassetio_mediacreation::traits::timeDomain::FrameRangedTrait;
using openassetio_mediacreation::traits::twoDimensional::DeepTrait;

// Katana-specific custom traits - see traits.yml.
using katana_openassetio::traits::application::LookFileTrait;
using katana_openassetio::traits::application::MacroTrait;
using katana_openassetio::traits::application::ProjectTrait;
using katana_openassetio::traits::application::SceneGraphBookmarksTrait;
using katana_openassetio::traits::nodes::GafferThreeTrait;
using katana_openassetio::traits::nodes::LiveGroupTrait;
using katana_openassetio::traits::nodes::LookFileManagerTrait;
using katana_openassetio::traits::timeDomain::FCurveTrait;
using katana_openassetio::traits::twoDimensional::PresetResolutionTrait;

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
        [[maybe_unused]] const FnKat::Asset::StringMap& fields,
        [[maybe_unused]] const FnKat::Asset::StringMap& args) const override
    {
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
        [[maybe_unused]] const FnKat::Asset::StringMap& args) const override
    {
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
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

private:
    static void imbueTraitAndMimeType(const TraitsDataPtr& traitsData)
    {
        ProjectTrait::imbueTo(traitsData);
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
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

private:
    static void imbueTraitAndMimeType(const TraitsDataPtr& traitsData)
    {
        LiveGroupTrait::imbueTo(traitsData);
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
        imbueTraitAndMimeType(args, traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueTraitAndMimeType(args, traitsData);
        return traitsData;
    }

private:
    static void imbueTraitAndMimeType(const FnKat::Asset::StringMap& args,
                                      const TraitsDataPtr& traitsData)
    {
        LookFileTrait::imbueTo(traitsData);
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
        LookFileManagerTrait::imbueTo(traitsData);
        LocatableContentTrait(traitsData)
            .setMimeType(
                "application/vnd.foundry.katana.lookfilemanager-settings+xml");  // Invented
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
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

private:
    static void imbueTraitAndMimeType(const TraitsDataPtr& traitsData)
    {
        GafferThreeTrait::imbueTo(traitsData);
        LocatableContentTrait(traitsData)
            .setMimeType("application/vnd.foundry.katana.rig+xml");  // Invented
    }
};

/**
 * Publish strategy for Macros.
 *
 * I.e. any node Parameters panel->(wrench menu)->Save as Macro.
 */
struct MacroPublisher : MediaCreationPublishStrategy<WorkfileSpecification>
{
    using MediaCreationPublishStrategy::MediaCreationPublishStrategy;

    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::prePublishTraitData(fields, args);
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

private:
    static void imbueTraitAndMimeType(const TraitsDataPtr& traitsData)
    {
        MacroTrait::imbueTo(traitsData);
        LocatableContentTrait(traitsData)
            .setMimeType("application/vnd.foundry.katana.macro");  // Invented
    }
};

/**
 * Publish strategy for FCurve files.
 *
 * I.e. any curve parameter->(right-click)->Export FCurve
 *
 * This is an XML document, though with a `.fcurve` file extension.
 */
struct FCurvePublisher : MediaCreationPublishStrategy<WorkfileSpecification>
{
    using MediaCreationPublishStrategy::MediaCreationPublishStrategy;

    [[nodiscard]] TraitsDataPtr prePublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::prePublishTraitData(fields, args);
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        imbueTraitAndMimeType(traitsData);
        return traitsData;
    }

private:
    static void imbueTraitAndMimeType(const TraitsDataPtr& traitsData)
    {
        FCurveTrait::imbueTo(traitsData);
        LocatableContentTrait(traitsData)
            .setMimeType("application/vnd.foundry.katana.fcurve+xml");  // Invented
    }
};

/**
 * Publish strategy for exported Scene Graph bookmarks.
 *
 * I.e. Scene Graph/Explorer->(bookmark icon)->Export Bookmarks.
 *
 * We add a MIME type, as well as imbue the `Config` trait to signal
 * that this is purely settings.
 */
struct SceneGraphBookmarksPublisher : MediaCreationPublishStrategy<WorkfileSpecification>
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
        SceneGraphBookmarksTrait::imbueTo(traitsData);

        LocatableContentTrait(traitsData)
            .setMimeType("application/vnd.foundry.katana.scenegraph-bookmarks+xml");  // Invented
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
        // Assume, optimistically, that we're going to render a range of
        // frames. We can't know which frames at this point, though.
        // We'll glob the directory later as part of
        // `register()`/`postCreateAsset()` to get the frame range.
        FrameRangedTrait::imbueTo(traitsData);
        return traitsData;
    }

    [[nodiscard]] TraitsDataPtr postPublishTraitData(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        const FnKat::Asset::StringMap& fields,
        const FnKat::Asset::StringMap& args) const override
    {
        auto traitsData = MediaCreationPublishStrategy::postPublishTraitData(fields, args);
        updateTraitsFromArgs(args, traitsData);

        // Check if the entity reference has a manager driven value
        // (path) encoded within it - see createAssetAndPath().
        if (const auto managerDrivenValueIter =
                fields.find(std::string{constants::kManagerDrivenValue});
            managerDrivenValueIter != fields.end())
        {
            // Extract the frame range by globbing the path.
            if (const auto maybeFrameRange =
                    findFrameRangeFromSequenceOnDisk(managerDrivenValueIter->second))
            {
                FrameRangedTrait frameRangedTrait{traitsData};
                frameRangedTrait.setStartFrame(maybeFrameRange->first);
                frameRangedTrait.setEndFrame(maybeFrameRange->second);
                frameRangedTrait.setInFrame(maybeFrameRange->first);
                frameRangedTrait.setOutFrame(maybeFrameRange->second);
            }
        }

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

        // Resolution preset.

        if (const auto keyAndValue = args.find("res"); keyAndValue != cend(args))
        {
            PresetResolutionTrait{traitsData}.setPresetName(keyAndValue->second);
        }
    }

    /**
     * Find the range of frames in a file sequence on disk.
     *
     * Effectively globs the directory of the file sequence looking for
     * files that match the default file sequence plugin's pattern, and
     * extracts the min and max frame numbers found.
     *
     * @param fileSequence Path to a frame with a placeholder token in
     * place of the frame number.
     *
     * @return A pair of min and max frame numbers, or std::nullopt if
     * no sequence was found.
     */
    static std::optional<std::pair<int, int>> findFrameRangeFromSequenceOnDisk(
        const std::string& fileSequence)
    {
        if (!FnKat::DefaultFileSequencePlugin::isFileSequence(fileSequence))
        {
            return std::nullopt;
        }

        // Create a dummy file path from the template, which we can then
        // split to use for matching. The DefaultFileSequencePlugin does
        // not otherwise provide access to the prefix/token/suffix.
        constexpr int kTokenAsInt = 9999999;
        constexpr std::string_view kTokenAsStr = "9999999";
        const std::string exampleFrame =
            FnKat::DefaultFileSequencePlugin::resolveFileSequence(fileSequence, kTokenAsInt, true);

        const auto prefixAndSuffix = pystring::split_view(exampleFrame, kTokenAsStr, 1);

        if (prefixAndSuffix.size() != 2)
        {
            return std::nullopt;
        }

        int minFrame = std::numeric_limits<int>::max();
        int maxFrame = std::numeric_limits<int>::min();

        // Loop over all files in the directory of the resolved path,
        // looking for frames.
        std::filesystem::path const directory =
            std::filesystem::path{prefixAndSuffix[0]}.parent_path();
        for (const auto& file : std::filesystem::directory_iterator(directory))
        {
            const std::string filePath = file.path().string();

            if (file.is_regular_file() && pystring::startswith(filePath, prefixAndSuffix[0]) &&
                pystring::endswith(filePath, prefixAndSuffix[1]))
            {
                const std::string_view frameStr = pystring::slice_view(
                    filePath,
                    static_cast<int>(prefixAndSuffix[0].size()),
                    static_cast<int>(filePath.size() - prefixAndSuffix[1].size()));

                int frameNum = 0;
                const char *const begin = frameStr.data();
                const char *const end = frameStr.data() + frameStr.size();
                constexpr std::errc kSuccess{};

                if (const auto result = std::from_chars(begin, end, frameNum);
                    // Successful conversion that consumed the entire substring.
                    result.ec == kSuccess && result.ptr == end)
                {
                    minFrame = std::min(minFrame, frameNum);
                    maxFrame = std::max(maxFrame, frameNum);
                }
            }
        }

        if (minFrame > maxFrame)
        {
            return std::nullopt;
        }

        return std::pair{minFrame, maxFrame};
    }
};
}  // anonymous namespace

PublishStrategies::PublishStrategies(const FileUrlPathConverterPtr& fileUrlPathConverter)
{
    strategies_[kFnAssetTypeKatanaScene] =
        std::make_unique<KatanaSceneAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeMacro] = std::make_unique<MacroPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLiveGroup] =
        std::make_unique<LiveGroupAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeImage] = std::make_unique<ImageAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLookFile] =
        std::make_unique<LookfileAssetPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeLookFileMgrSettings] =
        std::make_unique<LookFileManagerSettingsPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeFCurveFile] = std::make_unique<FCurvePublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeGafferThreeRig] =
        std::make_unique<GafferThreeRigPublisher>(fileUrlPathConverter);
    strategies_[kFnAssetTypeScenegraphBookmarks] =
        std::make_unique<SceneGraphBookmarksPublisher>(fileUrlPathConverter);

    // Katana does not publish using any of the remaining `kFnAssetType*`
    // constants - these asset types are only ever ingested. I.e.
    // - kFnAssetTypeAlembic
    // - kFnAssetTypeCastingSheet
    // - kFnAssetTypeAttributeFile
    // - kFnAssetTypeShader
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