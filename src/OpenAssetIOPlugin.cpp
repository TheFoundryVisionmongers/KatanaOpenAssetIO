// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#include "OpenAssetIOAsset.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <variant>

#include <FnAsset/FnDefaultFileSequencePlugin.h>
#include <FnAsset/plugin/FnAsset.h>
#include <FnAsset/suite/FnAssetSuite.h>
#include <FnLogging/FnLogging.h>
#include <FnPluginSystem/FnPlugin.h>

#include <openassetio/EntityReference.hpp>
#include <openassetio/access.hpp>
#include <openassetio/constants.hpp>
#include <openassetio/errors/exceptions.hpp>
#include <openassetio/hostApi/ManagerFactory.hpp>
#include <openassetio/log/LoggerInterface.hpp>
#include <openassetio/pluginSystem/CppPluginSystemManagerImplementationFactory.hpp>
#include <openassetio/pluginSystem/HybridPluginSystemManagerImplementationFactory.hpp>
#include <openassetio/python/hostApi.hpp>
#include <openassetio/trait/TraitsData.hpp>
#include <openassetio/trait/property.hpp>
#include <openassetio/typedefs.hpp>
#include <openassetio/utils/path.hpp>

#include <openassetio_mediacreation/specifications/lifecycle/EntityVersionsRelationshipSpecification.hpp>
#include <openassetio_mediacreation/traits/content/LocatableContentTrait.hpp>
#include <openassetio_mediacreation/traits/identity/DisplayNameTrait.hpp>
#include <openassetio_mediacreation/traits/lifecycle/VersionTrait.hpp>
#include <openassetio_mediacreation/traits/managementPolicy/ManagedTrait.hpp>
#include <openassetio_mediacreation/traits/threeDimensional/SourcePathTrait.hpp>

#include "KatanaHostInterface.hpp"
#include "PublishStrategies.hpp"
#include "config.hpp"
#include "constants.hpp"

FnLogSetup("OpenAssetIO");

namespace
{
struct KatanaLoggerInterface : openassetio::log::LoggerInterface
{
    void log(Severity severity, const openassetio::Str& message) override
    {
        switch (severity)
        {
        case Severity::kDebugApi:
        case Severity::kDebug:
            FnLogDebug(message);
            return;
        case Severity::kInfo:
        case Severity::kProgress:
            FnLogInfo(message);
            return;
        case Severity::kWarning:
            FnLogWarn(message);
            return;
        case Severity::kError:
            FnLogError(message);
            return;
        case Severity::kCritical:
            FnLogCritical(message);
            return;
        }

        // Should never happen (check compiler warnings for unhandled `switch` case).
        FnLogError("Unhandled log severity:" + message);
    }
};

constexpr char kAssetFieldKeySep = '_';
constexpr auto kDisablePythonEnvVar = "KATANAOPENASSETIO_DISABLE_PYTHON";
}  // namespace

OpenAssetIOAsset::OpenAssetIOAsset()
{
    OpenAssetIOAsset::reset();
}

OpenAssetIOAsset::~OpenAssetIOAsset() = default;

void OpenAssetIOAsset::reset()
{
    using openassetio::hostApi::ManagerFactory;
    using openassetio::hostApi::ManagerImplementationFactoryInterfacePtr;
    using openassetio::pluginSystem::CppPluginSystemManagerImplementationFactory;
    using openassetio::pluginSystem::HybridPluginSystemManagerImplementationFactory;
    namespace pyApi = openassetio::python::hostApi;

    try
    {
        const auto logger = std::make_shared<KatanaLoggerInterface>();

        // Create the appropriate plugin system.
        const auto managerImplFactory = [&]() -> ManagerImplementationFactoryInterfacePtr
        {
            if (const char* disablePythonEnvVar = std::getenv(kDisablePythonEnvVar);
                disablePythonEnvVar && std::string_view{disablePythonEnvVar} != "0")
            {
                // User has chosen to disable Python manager plugins. So
                // just use the C++ plugin system.
                return CppPluginSystemManagerImplementationFactory::make(logger);
            }
            // Support  C++ or Python or hybrid C++/Python plugins.
            return HybridPluginSystemManagerImplementationFactory::make(
                {// Plugin systems:
                 // C++ plugin system
                 CppPluginSystemManagerImplementationFactory::make(logger),
                 // Python plugin system
                 pyApi::createPythonPluginSystemManagerImplementationFactory(logger)},
                logger);
        }();

        manager_ = ManagerFactory::defaultManagerForInterface(
            std::make_shared<KatanaHostInterface>(), managerImplFactory, logger);

        if (!manager_)
        {
            throw openassetio::errors::ConfigurationException{
                "No default OpenAssetIO manager configured. Set OPENASSETIO_DEFAULT_CONFIG."};
        }

        context_ = manager_->createContext();
    }
    catch (const std::exception& exc)
    {
        FnLogError(exc.what());
        throw;
    }
}

std::optional<openassetio::EntityReference> OpenAssetIOAsset::entityRefForAssetIdAndVersion(
    const std::string& assetId,
    const std::string& desiredVersionTag) const
{
    // The "specifiedTag" property of the "Version" trait, when used in
    // a relationship query, acts as a filter predicate. We assume that
    // it enforces a strict match to the entity reference that best
    // represents the input "version", including meta-versions.
    //
    // E.g. we assume a "specifiedTag" of "latest" will match
    // "myasset://pony?v=latest" and _not_ "myasset://pony?v=2"
    // (assuming v2 is the latest). This is (currently) not made clear
    // in the MediaCreation Version trait documentation. Without this
    // assumption, the logic would become more complex, requiring a few
    // more queries.

    using openassetio::EntityReference;
    using openassetio::access::RelationsAccess;
    using openassetio::access::ResolveAccess;
    using openassetio_mediacreation::specifications::lifecycle::
        EntityVersionsRelationshipSpecification;
    using openassetio_mediacreation::traits::lifecycle::VersionTrait;

    // Validate the asset ID and get a strongly typed wrapper for
    // subsequent queries.
    const EntityReference sourceEntityRef = manager_->createEntityReference(assetId);

    // Relationship to get references to different versions of
    // the same logical entity.
    auto relationship = EntityVersionsRelationshipSpecification::create();
    // Set a relationship predicate, such that the returned
    // reference should refer to an entity of the given version.
    relationship.versionTrait().setSpecifiedTag(desiredVersionTag);

    // We only want/expect one corresponding versioned reference.
    constexpr std::size_t kNumExpectedResults = 1;

    // Get references that point to the given version of the asset.
    const auto versionsPager = manager_->getWithRelationship(sourceEntityRef,
                                                             relationship.traitsData(),
                                                             kNumExpectedResults,
                                                             RelationsAccess::kRead,
                                                             context_,
                                                             {});
    if (versionsPager->hasNext())
    {
        FnLogDebug("OpenAssetIOAsset: more than one result querying specific version for asset '"
                   << assetId << "' and version '" << desiredVersionTag
                   << "' - ignoring remainder");
    }

    // Get first page of references, which should have a page size of 1,
    // i.e. a single-element array.
    const auto versionedRefs = versionsPager->get();

    if (versionedRefs.empty())
    {
        FnLogDebug("OpenAssetIOAsset: no results querying specific version for asset '"
                   << assetId << "' and version '" << desiredVersionTag << "'");
        return std::nullopt;
    }

    // Return the matching reference.
    return versionedRefs.front();
}

bool OpenAssetIOAsset::isAssetId(const std::string& name)
{
    const auto result = manager_->isEntityReferenceString(name);
    return result;
}

bool OpenAssetIOAsset::containsAssetId(const std::string& name)
{
    using openassetio::constants::kInfoKey_EntityReferencesMatchPrefix;
    const auto info = manager_->info();
    // NOLINTNEXTLINE(*-suspicious-stringview-data-usage)
    const auto prefixKey = info.find(kInfoKey_EntityReferencesMatchPrefix.data());
    if (prefixKey == info.end())
    {
        throw std::runtime_error("OpenAssetIO does not provide entity reference prefix.");
    }

    const auto prefix = std::get<std::string>(prefixKey->second);
    return name.find(prefix) != std::string::npos;
}

bool OpenAssetIOAsset::checkPermissions(const std::string& assetId, const StringMap& context)
{
    // TODO(DH): Implement checkPermissions()
    (void)assetId;
    (void)context;
    return true;
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
bool OpenAssetIOAsset::runAssetPluginCommand(const std::string& assetId,
                                             const std::string& command,
                                             const StringMap& commandArgs)
{
    // TODO(DH): Implement runAssetPluginCommand()
    (void)assetId;
    (void)command;
    (void)commandArgs;
    return true;
}

void OpenAssetIOAsset::resolveAsset(const std::string& assetId, std::string& resolvedAsset)
{
    using openassetio::access::ResolveAccess;
    using openassetio_mediacreation::traits::content::LocatableContentTrait;

    // We don't know anything else about the asset other than its ID at this
    // point so attempt to resolve given only the LocatableContentTrait.
    const auto entityReference = manager_->createEntityReference(assetId);
    const auto traitData = manager_->resolve(
        entityReference, {LocatableContentTrait::kId}, ResolveAccess::kRead, context_);
    const auto url = LocatableContentTrait(traitData).getLocation();

    if (!url)
    {
        throw std::runtime_error{assetId + " has no location"};
    }
    resolvedAsset = fileUrlPathConverter_.pathFromUrl(*url);
}

void OpenAssetIOAsset::resolveAllAssets(const std::string& str, std::string& ret)
{
    // TODO(DH): Implement resolveAllAssets()
    resolveAsset(str, ret);
}

void OpenAssetIOAsset::resolvePath(const std::string& str, const int frame, std::string& ret)
{
    resolveAsset(str, ret);

    if (FnKat::DefaultFileSequencePlugin::isFileSequence(ret))
    {
        ret = FnKat::DefaultFileSequencePlugin::resolveFileSequence(ret, frame);
    }
}

void OpenAssetIOAsset::resolveAssetVersion(const std::string& assetId,
                                           std::string& ret,
                                           const std::string& versionStr)
{
    using openassetio::EntityReference;
    using openassetio::access::ResolveAccess;
    using openassetio_mediacreation::traits::lifecycle::VersionTrait;

    const EntityReference entityReference = [&]
    {
        if (versionStr.empty())
        {
            // No alternate version, so we want to query the version
            // tag associated with the given entity.
            return manager_->createEntityReference(assetId);
        }

        // Alternate version given, so we need to query the version tag
        // associated with the entity corresponding to the given
        // (meta-)version. E.g. "myasset://pony" with version of
        // "latest" has an entity reference of "myasset://pony?v=latest"
        // which we will `resolve` below to "v2" (assuming v2 is the
        // latest version).
        const auto maybeEntityReference = entityRefForAssetIdAndVersion(assetId, versionStr);

        if (!maybeEntityReference)
        {
            throw std::runtime_error{"No version found for asset " + assetId + " and version " +
                                     versionStr};
        }
        return *maybeEntityReference;
    }();

    // We don't have any other information about the asset other than its EntityReference so
    // request the VersionTrait.
    const auto traitData =
        manager_->resolve(entityReference, {VersionTrait::kId}, ResolveAccess::kRead, context_);

    // Usage by the Importomatic node implies "stableTag" is what we
    // want here - its parameters panel has a column for "Version" and a
    // column for "Resolved Version" where "Resolved Version" comes from
    // this function (and "Version" comes from getAssetFields).
    ret = VersionTrait{traitData}.getStableTag("");
}

void OpenAssetIOAsset::getAssetDisplayName(const std::string& assetId, std::string& ret)
{
    using openassetio::access::ResolveAccess;
    using openassetio_mediacreation::traits::identity::DisplayNameTrait;

    const auto entityReference = manager_->createEntityReference(assetId);
    const auto traitData =
        manager_->resolve(entityReference, {DisplayNameTrait::kId}, ResolveAccess::kRead, context_);

    ret = DisplayNameTrait{traitData}.getName("");
}

void OpenAssetIOAsset::getAssetVersions(const std::string& assetId, StringVector& ret)
{
    using openassetio::access::RelationsAccess;
    using openassetio::access::ResolveAccess;
    using openassetio_mediacreation::specifications::lifecycle::
        EntityVersionsRelationshipSpecification;
    using openassetio_mediacreation::traits::lifecycle::VersionTrait;

    // Get all related references, such that each reference points to a
    // different version of the same asset.
    const auto entityRefPager = manager_->getWithRelationship(
        manager_->createEntityReference(assetId),
        EntityVersionsRelationshipSpecification::create().traitsData(),
        constants::kPageSize,
        RelationsAccess::kRead,
        context_,
        {});

    openassetio::EntityReferences entityRefs;

    // Collect all pages of related references into a single list.
    openassetio::EntityReferences entityRefPage;
    while (!(entityRefPage = entityRefPager->get()).empty())
    {
        copy(cbegin(entityRefPage), cend(entityRefPage), back_inserter(entityRefs));
        entityRefPager->next();
    }

    // Batch `resolve` to get version metadata associated with each
    // entity reference.
    const auto traitsDatas =
        manager_->resolve(entityRefs, {VersionTrait::kId}, ResolveAccess::kRead, context_);

    // Extract and return the version "specified tag", i.e. version tag
    // potentially including meta-versions such as "latest".
    transform(cbegin(traitsDatas),
              cend(traitsDatas),
              back_inserter(ret),
              [](const auto& traitsData) { return VersionTrait{traitsData}.getSpecifiedTag(""); });
}

void OpenAssetIOAsset::getUniqueScenegraphLocationFromAssetId(const std::string& assetId,
                                                              const bool includeVersion,
                                                              std::string& ret)
{
    using openassetio::access::ResolveAccess;
    using openassetio::trait::TraitSet;
    using openassetio_mediacreation::traits::lifecycle::VersionTrait;
    using openassetio_mediacreation::traits::threeDimensional::SourcePathTrait;

    const auto traits = includeVersion ? TraitSet{VersionTrait::kId, SourcePathTrait::kId}
                                       : TraitSet{SourcePathTrait::kId};

    const auto traitsData = manager_->resolve(
        manager_->createEntityReference(assetId), traits, ResolveAccess::kRead, context_);

    ret = SourcePathTrait{traitsData}.getPath("/");

    if (includeVersion)
    {
        if (const auto versionTag = VersionTrait{traitsData}.getStableTag())
        {
            ret += "/";
            ret += *versionTag;
        }
    }
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
void OpenAssetIOAsset::getRelatedAssetId(const std::string& assetId,
                                         const std::string& relation,
                                         std::string& ret)
{
    // TODO(DH): Implement getRelatedAssetId()
    (void)assetId;
    (void)relation;
    ret = "";
}

void OpenAssetIOAsset::getAssetFields(const std::string& assetId,
                                      const bool includeDefaults,
                                      StringMap& returnFields)
{
    (void)includeDefaults;  // TODO(DF): How should we use this?

    using openassetio::access::ResolveAccess;
    using openassetio::trait::TraitSet;
    using openassetio_mediacreation::traits::identity::DisplayNameTrait;
    using openassetio_mediacreation::traits::lifecycle::VersionTrait;

    const auto entityReference = manager_->createEntityReference(assetId);

    const auto traitsData = manager_->resolve(entityReference,
                                              {DisplayNameTrait::kId, VersionTrait::kId},
                                              ResolveAccess::kRead,
                                              context_);

    // Katana's AssetAPI only standardises Name & Version fields.
    returnFields[constants::kAssetId] = assetId;
    returnFields[kFnAssetFieldName] = DisplayNameTrait{traitsData}.getName("");
    returnFields[kFnAssetFieldVersion] = VersionTrait{traitsData}.getSpecifiedTag("");
}

void OpenAssetIOAsset::buildAssetId(const StringMap& fields, std::string& ret)
{
    using openassetio::EntityReference;
    using openassetio::EntityReferences;
    using openassetio::access::RelationsAccess;
    using openassetio::access::ResolveAccess;
    using openassetio_mediacreation::specifications::lifecycle::
        EntityVersionsRelationshipSpecification;
    using openassetio_mediacreation::traits::lifecycle::VersionTrait;

    const std::string assetId = [&]
    {
        // getAssetFields populates __assetId.
        if (const auto assetIdIt = fields.find(constants::kAssetId); assetIdIt != fields.end())
        {
            return assetIdIt->second;
        }
        throw std::runtime_error("Could not determine Asset ID from field list.");
    }();

    // `buildAssetId` is used by Katana as a mechanism to switch between
    // versions of the same asset. So we must query for entity
    // references that are related to the input reference but that
    // point to the given version.
    const auto versionedAssetId = [&]() -> std::optional<std::string>
    {
        const auto versionIt = fields.find(kFnAssetFieldVersion);
        if (versionIt == fields.end())
        {
            return std::nullopt;
        }
        const std::string& desiredVersionTag = versionIt->second;

        const std::optional<EntityReference> versionedRef =
            entityRefForAssetIdAndVersion(assetId, desiredVersionTag);

        if (!versionedRef)
        {
            return std::nullopt;
        }
        return versionedRef->toString();
    }();

    ret = versionedAssetId.value_or(assetId);
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
void OpenAssetIOAsset::getAssetAttributes(const std::string& assetId,
                                          [[maybe_unused]] const std::string& scope,
                                          StringMap& returnAttrs)
{
    // TODO(DF): E.g. see CastingSheet.py - a scope of "version" is
    //  expected to (also) return a field of "type". The default File
    //  AssetAPI plugin gives the file extension as the "type".

    // TODO(DF): use `scope` to filter the attributes returned(?).

    using openassetio::access::EntityTraitsAccess;
    using openassetio_mediacreation::traits::identity::DisplayNameTrait;
    using openassetio_mediacreation::traits::lifecycle::VersionTrait;

    const auto entityReference = manager_->createEntityReference(assetId);

    // Find out what the asset management system knows about this asset.
    auto traitSet = manager_->entityTraits(entityReference, EntityTraitsAccess::kRead, context_);

    // Augment with DisplayName and Version if it isn't specified already for Katana's
    // specified fields
    traitSet.insert(DisplayNameTrait::kId);
    traitSet.insert(VersionTrait::kId);

    using openassetio::access::ResolveAccess;

    const auto traitsData =
        manager_->resolve(entityReference, traitSet, ResolveAccess::kRead, context_);

    // Katana's AssetAPI only standardises Name & Version fields.
    // TODO(DH): Specify set of other well known fields?
    returnAttrs[constants::kAssetId] = assetId;
    returnAttrs[kFnAssetFieldName] = DisplayNameTrait{traitsData}.getName("");
    returnAttrs[kFnAssetFieldVersion] = VersionTrait{traitsData}.getSpecifiedTag("");

    // TODO(DH): Determine alternative way to surface traits to Katana?
    for (const auto& traitId : traitsData->traitSet())
    {
        for (const auto& traitPropertyKey : traitsData->traitPropertyKeys(traitId))
        {
            openassetio::trait::property::Value value;
            traitsData->getTraitProperty(&value, traitId, traitPropertyKey);
            std::string attrKey = traitId;
            attrKey += kAssetFieldKeySep;
            attrKey += traitPropertyKey;
            // For safe use to index GroupAttributes.
            replace(begin(attrKey), end(attrKey), '.', kAssetFieldKeySep);

            std::visit(
                [&](const auto& containedValue)
                {
                    std::ostringstream sstr;
                    sstr << std::boolalpha << containedValue;
                    returnAttrs[std::move(attrKey)] = sstr.str();
                },
                value);
        }
    }
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
void OpenAssetIOAsset::setAssetAttributes(const std::string& assetId,
                                          const std::string& scope,
                                          const StringMap& attrs)
{
    // TODO(DH): Implement setAssetAttributes()
    (void)assetId;
    (void)scope;
    (void)attrs;
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
void OpenAssetIOAsset::getAssetIdForScope(const std::string& assetId,
                                          const std::string& scope,
                                          std::string& ret)
{
    // TODO(DH): Implement getAssetIdForScope()
    (void)scope;
    ret = assetId;
}

void OpenAssetIOAsset::createAssetAndPath(FnKat::AssetTransaction* txn,
                                          const std::string& assetType,
                                          // NOLINTNEXTLINE(*-easily-swappable-parameters)
                                          const StringMap& assetFields,
                                          const StringMap& args,
                                          const bool createDirectory,
                                          std::string& assetId)
{
    // `assetFields` comes from `getAssetFields`, with no mutations.
    //
    // `args` often starts off as a dict populated by the delegated
    // asset browser panel's (optional) `getExtraOptions`. Katana itself
    // doesn't make use of this though, so by default `args` starts off
    // empty.
    //
    // "versionUp" and "publish" are commonly seen in `args`. From
    // `CreateSceneAsset`:
    // > @param versionUp: Flag that controls whether to create a new
    // > version.
    // > @param publish: Flag that controls whether to publish the
    // > resulting scene as the current version.
    //
    // Some nodes/panels have special `args`:
    // * LiveGroup: "comment" (can't see where this can be set).
    // * ImageWrite / Render: "ext" (file extension), "res"
    //   (resolution), "colorspace", "view" (left/right), "versionUp",
    //   "frame", "explicitOutputVersion" (`--render_explicit_version`
    //   command-line only)
    // * LookFileMaterialsOut: "outputFormat" ("as archive"/"as
    //   directory"), "versionUp", "publish" (can't find where those
    //   last two are set)
    // * LookFileBake: "outputFormat" (as above), "fileExtension" (.klf
    //   or blank).
    // * Catalog panel: "exportedSequence" (file sequence string pattern
    //   given to postCreateAsset), "context" (kFnAssetContextCatalog).

    (void)createDirectory;  // TODO(DF): kCreateRelated?

    if (txn != nullptr)
    {
        throw std::runtime_error("AssetAPI transactions not yet supported.");
    }
    const auto assetIdIt = assetFields.find(constants::kAssetId);
    if (assetIdIt == assetFields.end())
    {
        throw std::runtime_error("Existing assetId not specified in publish");
    }

    using openassetio_mediacreation::traits::managementPolicy::ManagedTrait;

    const PublishStrategy& strategy = publishStrategies_.strategyForAssetType(assetType);

    const auto entityPolicy = manager_->managementPolicy(
        strategy.assetTraitSet(), openassetio::access::PolicyAccess::kWrite, context_);

    if (!ManagedTrait::isImbuedTo(entityPolicy))
    {
        // TODO(DH): Attempt fallback to persist basic entity?
        FnLogWarn("OpenAssetIO Manager '" + manager_->displayName() +
                  "' does not support trait specifiction.");
        throw std::runtime_error("Specification not supported.");
    }

    // Indicate to the Manager we wish to publish something via preflight
    const auto existingAssetId = manager_->createEntityReference(assetIdIt->second);

    assetId = manager_
                  ->preflight(existingAssetId,
                              strategy.prePublishTraitData(args),
                              openassetio::access::PublishingAccess::kWrite,
                              context_)
                  .toString();
}

void OpenAssetIOAsset::postCreateAsset(FnKat::AssetTransaction* txn,
                                       const std::string& assetType,
                                       // NOLINTNEXTLINE(*-easily-swappable-parameters)
                                       const StringMap& assetFields,
                                       const StringMap& args,
                                       std::string& assetId)
{
    if (txn != nullptr)
    {
        throw std::runtime_error("AssetAPI transactions not yet supported.");
    }
    // getAssetFields re-populates this with our working entity reference.
    const auto assetIdIt = assetFields.find(constants::kAssetId);
    if (assetIdIt == assetFields.cend())
    {
        throw std::runtime_error("Working EntityReference not specified in post-publish");
    }

    const PublishStrategy& strategy = publishStrategies_.strategyForAssetType(assetType);

    const auto workingEntityReference = manager_->createEntityReferenceIfValid(assetIdIt->second);
    if (!workingEntityReference)
    {
        throw std::runtime_error(
            "Error creating EntityReference during pre-publish from Asset ID: " +
            assetIdIt->second);
    }

    assetId = manager_
                  ->register_(workingEntityReference.value(),
                              strategy.postPublishTraitData(args),
                              openassetio::access::PublishingAccess::kWrite,
                              context_)
                  .toString();
}

// --- Register plugin ------------------------

DEFINE_ASSET_PLUGIN(OpenAssetIOAsset)

void registerPlugins()
{
    REGISTER_PLUGIN(OpenAssetIOAsset,
                    KATANA_OPENASSETIO_PLUGIN_NAME,
                    KATANA_OPENASSETIO_PLUGIN_VERSION_MAJOR,
                    KATANA_OPENASSETIO_PLUGIN_VERSION_MINOR);
}
