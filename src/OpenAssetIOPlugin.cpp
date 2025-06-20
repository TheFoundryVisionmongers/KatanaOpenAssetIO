// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#include "OpenAssetIOAsset.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <ios>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <FnAsset/FnDefaultFileSequencePlugin.h>
#include <FnAsset/plugin/FnAsset.h>
#include <FnAsset/suite/FnAssetSuite.h>
#include <FnLogging/FnLogging.h>
#include <FnLogging/suite/FnLoggingSuite.h>
#include <FnPluginSystem/FnPlugin.h>
#include <pystring/pystring.h>

#include <openassetio/EntityReference.hpp>
#include <openassetio/access.hpp>
#include <openassetio/constants.hpp>
#include <openassetio/errors/exceptions.hpp>
#include <openassetio/hostApi/EntityReferencePager.hpp>
#include <openassetio/hostApi/ManagerFactory.hpp>
#include <openassetio/log/LoggerInterface.hpp>
#include <openassetio/pluginSystem/CppPluginSystemManagerImplementationFactory.hpp>
#include <openassetio/pluginSystem/HybridPluginSystemManagerImplementationFactory.hpp>
#include <openassetio/python/hostApi.hpp>
#include <openassetio/trait/TraitsData.hpp>
#include <openassetio/trait/property.hpp>
#include <openassetio/typedefs.hpp>

#include <openassetio_mediacreation/specifications/lifecycle/EntityVersionsRelationshipSpecification.hpp>
#include <openassetio_mediacreation/traits/content/LocatableContentTrait.hpp>
#include <openassetio_mediacreation/traits/identity/DisplayNameTrait.hpp>
#include <openassetio_mediacreation/traits/lifecycle/VersionTrait.hpp>
#include <openassetio_mediacreation/traits/managementPolicy/ManagedTrait.hpp>
#include <openassetio_mediacreation/traits/relationship/SingularTrait.hpp>
#include <openassetio_mediacreation/traits/threeDimensional/SourcePathTrait.hpp>
#include <openassetio_mediacreation/traits/usage/RelationshipTrait.hpp>

#include "KatanaHostInterface.hpp"
#include "PublishStrategies.hpp"
#include "config.hpp"
#include "constants.hpp"
#include "logging.hpp"

namespace
{
struct KatanaLoggerInterface final : openassetio::log::LoggerInterface
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

    [[nodiscard]] bool isSeverityLogged(const Severity severity) const override
    {
        switch (severity)
        {
        case Severity::kDebugApi:
        case Severity::kDebug:
            return _fnLog.isSeverityEnabled(kFnLoggingSeverityDebug);
        case Severity::kInfo:
        case Severity::kProgress:
            return _fnLog.isSeverityEnabled(kFnLoggingSeverityInfo);
        case Severity::kWarning:
            return _fnLog.isSeverityEnabled(kFnLoggingSeverityWarning);
        case Severity::kError:
            return _fnLog.isSeverityEnabled(kFnLoggingSeverityError);
        case Severity::kCritical:
            return _fnLog.isSeverityEnabled(kFnLoggingSeverityCritical);
        }

        // Should never happen (check compiler warnings for unhandled `switch` case).
        return false;
    }
};

constexpr char kAssetFieldKeySep = ',';
constexpr auto kDisablePythonEnvVar = "KATANAOPENASSETIO_DISABLE_PYTHON";

using Severity = openassetio::log::LoggerInterface::Severity;
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
        logger_ = std::make_shared<KatanaLoggerInterface>();
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi("OpenAssetIOAsset::reset()");
        }

        // Create the appropriate plugin system.
        const auto managerImplFactory = [&]() -> ManagerImplementationFactoryInterfacePtr
        {
            if (const char* disablePythonEnvVar = std::getenv(kDisablePythonEnvVar);
                disablePythonEnvVar && std::string_view{disablePythonEnvVar} != "0")
            {
                // User has chosen to disable Python manager plugins. So
                // just use the C++ plugin system.
                return CppPluginSystemManagerImplementationFactory::make(logger_);
            }
            // Support  C++ or Python or hybrid C++/Python plugins.
            return HybridPluginSystemManagerImplementationFactory::make(
                {// Plugin systems:
                 // C++ plugin system
                 CppPluginSystemManagerImplementationFactory::make(logger_),
                 // Python plugin system
                 pyApi::createPythonPluginSystemManagerImplementationFactory(logger_)},
                logger_);
        }();

        manager_ = ManagerFactory::defaultManagerForInterface(
            std::make_shared<KatanaHostInterface>(), managerImplFactory, logger_);

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
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::containsAssetId(name=", name, ")"));
        }
        using openassetio::constants::kInfoKey_EntityReferencesMatchPrefix;
        const auto info = manager_->info();
        // NOLINTNEXTLINE(*-suspicious-stringview-data-usage)
        const auto prefixKey = info.find(kInfoKey_EntityReferencesMatchPrefix.data());
        if (prefixKey == info.end())
        {
            throw std::runtime_error("OpenAssetIO does not provide entity reference prefix.");
        }

        const auto prefix = std::get<std::string>(prefixKey->second);

        const bool isContained = name.find(prefix) != std::string::npos;

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::containsAssetId -> ", isContained));
        }
        return isContained;
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(
                logging::concatAsStr("OpenAssetIOAsset::containsAssetId -> ERROR: ", exc.what()));
        }
        throw;
    }
}

bool OpenAssetIOAsset::checkPermissions(const std::string& assetId, const StringMap& context)
{
    if (logger_->isSeverityLogged(Severity::kDebugApi))
    {
        logger_->debugApi(logging::concatAsStr(
            "OpenAssetIOAsset::checkPermissions(assetId=", assetId, ", context=", context, ")"));
    }
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
    if (logger_->isSeverityLogged(Severity::kDebugApi))
    {
        logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::runAssetPluginCommand(assetId=",
                                               assetId,
                                               ", command=",
                                               command,
                                               ", commandArgs=",
                                               commandArgs,
                                               ")"));
    }
    (void)assetId;

    if (command == "initialize")
    {
        // Re-`initialize` the manager with updated settings. Will only
        // work for string-valued settings (otherwise with throw). Note
        // that partial updates are supported as per the API contract.
        try
        {
            manager_->initialize({cbegin(commandArgs), cend(commandArgs)});
        }
        catch (const std::exception& exc)
        {
            if (logger_->isSeverityLogged(Severity::kDebug))
            {
                logger_->debug(logging::concatAsStr(
                    "OpenAssetIOAsset::runAssetPluginCommand -> ERROR: ", exc.what()));
            }
            return false;
        }
    }
    return true;
}

void OpenAssetIOAsset::resolveAsset(const std::string& assetId, std::string& resolvedAsset)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::resolveAsset(assetId=", assetId, ")"));
        }

        using openassetio::access::ResolveAccess;
        using openassetio_mediacreation::traits::content::LocatableContentTrait;

        auto [entityReference, managerDrivenValue] =
            assetIdToEntityRefAndManagerDrivenValue(assetId);

        if (managerDrivenValue.empty())
        {
            // We assume that Katana wants a path when it calls
            // `resolveAsset`, which is always the case except for
            // esoteric configurations.

            const auto traitData = manager_->resolve(
                entityReference, {LocatableContentTrait::kId}, ResolveAccess::kRead, context_);
            const auto url = LocatableContentTrait(traitData).getLocation();

            if (!url)
            {
                throw std::runtime_error{assetId + " has no location"};
            }
            resolvedAsset = fileUrlPathConverter_->pathFromUrl(*url);
        }
        else
        {
            // If the reference contains a manager-driven value, i.e. is
            // the result of a `createAssetAndPath()`, return that
            // instead.
            resolvedAsset = std::move(managerDrivenValue);
        }

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::resolveAsset -> ", resolvedAsset));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(
                logging::concatAsStr("OpenAssetIOAsset::resolveAsset -> ERROR: ", exc.what()));
        }
        throw;
    }
}

void OpenAssetIOAsset::resolveAllAssets(const std::string& str, std::string& ret)
{
    if (logger_->isSeverityLogged(Severity::kDebugApi))
    {
        logger_->debugApi(
            logging::concatAsStr("OpenAssetIOAsset::resolveAllAssets(str=", str, ")"));
    }
    // TODO(DH): Implement resolveAllAssets()
    resolveAsset(str, ret);
}

void OpenAssetIOAsset::resolvePath(const std::string& str, const int frame, std::string& ret)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr(
                "OpenAssetIOAsset::resolvePath(str=", str, ", frame=", frame, ")"));
        }
        resolveAsset(str, ret);

        if (FnKat::DefaultFileSequencePlugin::isFileSequence(ret))
        {
            ret = FnKat::DefaultFileSequencePlugin::resolveFileSequence(ret, frame);
        }

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::resolvePath -> ", ret));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(
                logging::concatAsStr("OpenAssetIOAsset::resolvePath -> ERROR: ", exc.what()));
        }
        throw;
    }
}

void OpenAssetIOAsset::resolveAssetVersion(const std::string& assetId,
                                           std::string& ret,
                                           const std::string& versionStr)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::resolveAssetVersion(assetId=",
                                                   assetId,
                                                   ", versionStr=",
                                                   versionStr,
                                                   ")"));
        }
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

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::resolveAssetVersion -> ", ret));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(logging::concatAsStr("OpenAssetIOAsset::resolveAssetVersion -> ERROR: ",
                                                exc.what()));
        }
        throw;
    }
}

void OpenAssetIOAsset::getAssetDisplayName(const std::string& assetId, std::string& ret)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr(
                "OpenAssetIOAsset::getAssetDisplayName(assetId=", assetId, ")"));
        }
        // Katana often does not check if assetId is a reference or a
        // file path before calling this function.
        if (const auto entityReference = manager_->createEntityReferenceIfValid(assetId))
        {
            using openassetio::access::ResolveAccess;
            using openassetio_mediacreation::traits::identity::DisplayNameTrait;

            const auto traitData = manager_->resolve(
                *entityReference, {DisplayNameTrait::kId}, ResolveAccess::kRead, context_);

            ret = DisplayNameTrait{traitData}.getName("");
        }

        if (ret.empty())
        {
            ret = assetId;
        }

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::getAssetDisplayName -> ", ret));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(logging::concatAsStr("OpenAssetIOAsset::getAssetDisplayName -> ERROR: ",
                                                exc.what()));
        }
        throw;
    }
}

void OpenAssetIOAsset::getAssetVersions(const std::string& assetId, StringVector& ret)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::getAssetVersions(assetId=", assetId, ")"));
        }
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
                  [](const auto& traitsData)
                  { return VersionTrait{traitsData}.getSpecifiedTag(""); });

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::getAssetVersions -> ", ret));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(
                logging::concatAsStr("OpenAssetIOAsset::getAssetVersions -> ERROR: ", exc.what()));
        }
        throw;
    }
}

void OpenAssetIOAsset::getUniqueScenegraphLocationFromAssetId(const std::string& assetId,
                                                              const bool includeVersion,
                                                              std::string& ret)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr(
                "OpenAssetIOAsset::getUniqueScenegraphLocationFromAssetId(assetId=",
                assetId,
                ", includeVersion=",
                includeVersion,
                ")"));
        }
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

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr(
                "OpenAssetIOAsset::getUniqueScenegraphLocationFromAssetId -> ", ret));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(logging::concatAsStr(
                "OpenAssetIOAsset::getUniqueScenegraphLocationFromAssetId -> ERROR: ", exc.what()));
        }
        throw;
    }
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
void OpenAssetIOAsset::getRelatedAssetId(const std::string& assetId,
                                         const std::string& relation,
                                         std::string& ret)
{
    if (logger_->isSeverityLogged(Severity::kDebugApi))
    {
        logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::getRelatedAssetId(assetId=",
                                               assetId,
                                               ", relationStr=",
                                               relation,
                                               ")"));
    }
    // TODO(DH): Implement getRelatedAssetId()
    (void)assetId;
    (void)relation;
    ret = "";
}

void OpenAssetIOAsset::getAssetFields(const std::string& assetId,
                                      const bool includeDefaults,
                                      StringMap& returnFields)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::getAssetFields(assetId=",
                                                   assetId,
                                                   ", includeDefaults=",
                                                   includeDefaults,
                                                   ")"));
        }
        (void)includeDefaults;  // TODO(DF): How should we use this?

        using openassetio::access::ResolveAccess;
        using openassetio::trait::TraitSet;
        using openassetio_mediacreation::traits::identity::DisplayNameTrait;
        using openassetio_mediacreation::traits::lifecycle::VersionTrait;

        auto [entityReference, managerDrivenValue] =
            assetIdToEntityRefAndManagerDrivenValue(assetId);

        const auto traitsData = manager_->resolve(entityReference,
                                                  {DisplayNameTrait::kId, VersionTrait::kId},
                                                  ResolveAccess::kRead,
                                                  context_);

        // Katana's AssetAPI only standardises Name & Version fields.
        returnFields[constants::kEntityReference] = entityReference.toString();
        if (!managerDrivenValue.empty())
        {
            returnFields[constants::kManagerDrivenValue] = std::move(managerDrivenValue);
        }
        returnFields[kFnAssetFieldName] = DisplayNameTrait{traitsData}.getName("");
        returnFields[kFnAssetFieldVersion] = VersionTrait{traitsData}.getSpecifiedTag("");

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::getAssetFields -> ", returnFields));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(
                logging::concatAsStr("OpenAssetIOAsset::getAssetFields -> ERROR: ", exc.what()));
        }
        throw;
    }
}

void OpenAssetIOAsset::buildAssetId(const StringMap& fields, std::string& ret)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::buildAssetId(fields=", fields, ")"));
        }

        using openassetio::EntityReference;
        using openassetio::EntityReferences;
        using openassetio::access::RelationsAccess;
        using openassetio::access::ResolveAccess;
        using openassetio_mediacreation::specifications::lifecycle::
            EntityVersionsRelationshipSpecification;
        using openassetio_mediacreation::traits::lifecycle::VersionTrait;

        const std::string assetId = [&]
        {
            // getAssetFields populates __entityReference.
            if (const auto entityReferenceIt = fields.find(constants::kEntityReference);
                entityReferenceIt != fields.end())
            {
                // getAssetFields may populate __managerDrivenValue.
                if (const auto managerDrivenValueIt = fields.find(constants::kManagerDrivenValue);
                    managerDrivenValueIt != fields.end())
                {
                    return pystring::join(
                        constants::kAssetIdManagerDrivenValueSep,
                        {entityReferenceIt->second, managerDrivenValueIt->second});
                }
                return entityReferenceIt->second;
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

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::buildAssetId -> ", ret));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(
                logging::concatAsStr("OpenAssetIOAsset::buildAssetId -> ERROR: ", exc.what()));
        }
        throw;
    }
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
void OpenAssetIOAsset::getAssetAttributes(const std::string& assetId,
                                          [[maybe_unused]] const std::string& scope,
                                          StringMap& returnAttrs)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr(
                "OpenAssetIOAsset::getAssetAttributes(assetId=", assetId, ", scope=", scope, ")"));
        }

        // TODO(DF): E.g. see CastingSheet.py - a scope of "version" is
        //  expected to (also) return a field of "type". The default File
        //  AssetAPI plugin gives the file extension as the "type".

        // TODO(DF): use `scope` to filter the attributes returned(?).

        using openassetio::access::EntityTraitsAccess;
        using openassetio_mediacreation::traits::identity::DisplayNameTrait;
        using openassetio_mediacreation::traits::lifecycle::VersionTrait;

        const auto entityReference = manager_->createEntityReference(assetId);

        // Find out what the asset management system knows about this asset.
        auto traitSet =
            manager_->entityTraits(entityReference, EntityTraitsAccess::kRead, context_);

        using openassetio::access::ResolveAccess;

        const auto traitsData =
            manager_->resolve(entityReference, traitSet, ResolveAccess::kRead, context_);

        // TODO(DH): Determine alternative way to surface traits to Katana?

        // Convert the traits to a StringMap. Retain traits with no
        // properties, so that the trait set can be determined
        // externally, even if the trait has no resolvable properties.
        for (const auto& traitId : traitSet)
        {
            // Note that Katana will use the StringMap keys as keys for
            // building a GroupAttribute, which means `.` has special
            // meaning (nesting). Katana will then parse the
            // GroupAttribute back to a flat StringMap, losing any
            // nested elements. So we must ensure no `.`s in the key.
            // Here we (somewhat arbitrarily) use `,` instead as the key
            // separator.
            std::string attrKey = traitId;
            replace(begin(attrKey), end(attrKey), '.', kAssetFieldKeySep);
            returnAttrs[std::move(attrKey)] = "";

            // Add any available (i.e. resolvable) properties for this
            // trait.
            for (const auto& traitPropertyKey : traitsData->traitPropertyKeys(traitId))
            {
                openassetio::trait::property::Value value;
                traitsData->getTraitProperty(&value, traitId, traitPropertyKey);
                attrKey = traitId;
                attrKey += kAssetFieldKeySep;
                attrKey += traitPropertyKey;
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

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::getAssetAttributes -> ", returnAttrs));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(logging::concatAsStr("OpenAssetIOAsset::getAssetAttributes -> ERROR: ",
                                                exc.what()));
        }
        throw;
    }
}

// NOLINTNEXTLINE(*-easily-swappable-parameters)
void OpenAssetIOAsset::setAssetAttributes(const std::string& assetId,
                                          const std::string& scope,
                                          const StringMap& attrs)
{
    if (logger_->isSeverityLogged(Severity::kDebugApi))
    {
        logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::setAssetAttributes(assetId=",
                                               assetId,
                                               ", scope=",
                                               scope,
                                               ", attrs=",
                                               attrs,
                                               ")"));
    }
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
    if (logger_->isSeverityLogged(Severity::kDebugApi))
    {
        logger_->debugApi(logging::concatAsStr(
            "OpenAssetIOAsset::getAssetIdForScope(assetId=", assetId, ", scope=", scope, ")"));
    }

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
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(logging::concatAsStr("OpenAssetIOAsset::createAssetAndPath(",
                                                   "txn=",
                                                   // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
                                                   reinterpret_cast<std::uintptr_t>(txn),
                                                   ", assetType=",
                                                   assetType,
                                                   ", assetFields=",
                                                   assetFields,
                                                   ", args=",
                                                   args,
                                                   ", createDirectory=",
                                                   createDirectory,
                                                   ")"));
        }
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

        const auto assetIdIt = assetFields.find(constants::kEntityReference);
        if (assetIdIt == assetFields.end())
        {
            throw std::runtime_error("Existing assetId not specified in publish");
        }

        using BatchElementErrorPolicyTag =
            openassetio::hostApi::Manager::BatchElementErrorPolicyTag;
        using openassetio::access::RelationsAccess;
        using openassetio::access::ResolveAccess;
        using openassetio::hostApi::EntityReferencePagerPtr;
        using openassetio::trait::TraitsData;
        using openassetio::trait::TraitsDataPtr;
        using openassetio_mediacreation::traits::content::LocatableContentTrait;
        using openassetio_mediacreation::traits::lifecycle::VersionTrait;
        using openassetio_mediacreation::traits::managementPolicy::ManagedTrait;
        using openassetio_mediacreation::traits::relationship::SingularTrait;
        using openassetio_mediacreation::traits::usage::RelationshipTrait;

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
        const auto entityReference = manager_->createEntityReference(assetIdIt->second);

        const openassetio::EntityReference workingRef = [&]
        {
            openassetio::EntityReference parentWorkingRef =
                manager_->preflight(entityReference,
                                    strategy.prePublishTraitData(assetFields, args),
                                    openassetio::access::PublishingAccess::kWrite,
                                    context_);

            // If the "versionUp" arg isn't set or is not "False", then
            // just use the `preflight()` reference.
            auto keyAndValue = args.find("versionUp");
            if (keyAndValue == args.end() || keyAndValue->second != "False")
            {
                return parentWorkingRef;
            }

            // Attempt to communicate an equivalent of Katana's
            // "versionUp=False" arg, which is provided for several
            // different asset types, in particular Katana scene files.
            //
            // We use a relationship query with kWrite access mode and
            // relationship traits specifying the explicit version that
            // we want to target.
            //
            // We're assuming that the asset manager will understand
            // this as "I really want to write to this specific version
            // rather than create a new version".
            //
            // The manager may then allow overwriting, or create a new
            // "revision", of the same version.
            //
            // If the manager doesn't support this workflow, then we
            // continue to use the entity returned from the above
            // `preflight()` call as the working reference.

            const TraitsDataPtr versionTraitsData = manager_->resolve(
                entityReference, {VersionTrait::kId}, ResolveAccess::kRead, context_);

            const auto maybeStableTag = VersionTrait{versionTraitsData}.getStableTag();
            // If we can't get the explicit version that we want to
            // write to, then abort and return the `preflight()`
            // reference.
            if (!maybeStableTag.has_value())
            {
                return parentWorkingRef;
            }

            // { Relationship, Singular, Version} trait set, with
            // `stableTag` filter predicate.
            const TraitsDataPtr specificVersionRelationship = TraitsData::make();
            RelationshipTrait::imbueTo(specificVersionRelationship);
            SingularTrait::imbueTo(specificVersionRelationship);
            VersionTrait{specificVersionRelationship}.setStableTag(*maybeStableTag);

            // See if we can get a writeable reference to the
            // explicit version. Use kVariant tag so we can ignore
            // any errors.
            const auto maybeEntityRefPager =
                manager_->getWithRelationship(parentWorkingRef,
                                              specificVersionRelationship,
                                              1,
                                              RelationsAccess::kWrite,
                                              context_,
                                              {},
                                              BatchElementErrorPolicyTag::kVariant);

            const auto* entityRefPager = std::get_if<EntityReferencePagerPtr>(&maybeEntityRefPager);
            // If the relationship query isn't supported, then ignore
            // the error and abort, returning the `preflight()`
            // reference.
            if (!entityRefPager)
            {
                return parentWorkingRef;
            }

            const openassetio::EntityReferences writeableRefs = (*entityRefPager)->get();
            // If no results, or an unexpected number of results, then
            // abort and return the `preflight()` reference.
            if (writeableRefs.size() != 1)
            {
                return parentWorkingRef;
            }

            return writeableRefs.front();
        }();

        assetId = workingRef.toString();

        // In almost all cases, Katana will immediately pass `assetId`
        // to `resolveAsset()` and expect a file path to be returned.
        //
        // Since the imminent `resolveAsset()` call will not communicate
        // that it wants a writeable path, and the subsequent
        // `postCreateAsset()` call will not be told which path was
        // used, we preempt this workflow by resolving for
        // `kManagerDriven` here and encode it in the reference itself,
        // so it's available for use in these subsequent steps.
        //
        // It is a little ambiguous in the docs whether `resolve()`
        // should error for an unsupported `kManagerDriven` query, or if
        // it should just leave the offending trait unset in the result.
        // So use the kVariant tag just in case, so we can ignore any
        // errors.
        const auto maybeTraitsData = manager_->resolve(workingRef,
                                                       {LocatableContentTrait::kId},
                                                       ResolveAccess::kManagerDriven,
                                                       context_,
                                                       BatchElementErrorPolicyTag::kVariant);

        if (const auto* traitsData = std::get_if<TraitsDataPtr>(&maybeTraitsData))
        {
            if (const auto url = LocatableContentTrait(*traitsData).getLocation())
            {
                const std::string managerDrivenValue = fileUrlPathConverter_->pathFromUrl(*url);
                assetId = pystring::join(constants::kAssetIdManagerDrivenValueSep,
                                         {assetId, managerDrivenValue});
            }
        }

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::createAssetAndPath -> ", assetId));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(logging::concatAsStr("OpenAssetIOAsset::createAssetAndPath -> ERROR: ",
                                                exc.what()));
        }
        throw;
    }
}

void OpenAssetIOAsset::postCreateAsset(FnKat::AssetTransaction* txn,
                                       const std::string& assetType,
                                       // NOLINTNEXTLINE(*-easily-swappable-parameters)
                                       const StringMap& assetFields,
                                       const StringMap& args,
                                       std::string& assetId)
{
    try
    {
        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::postCreateAsset("
                                     "txn=",
                                     // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
                                     reinterpret_cast<std::uintptr_t>(txn),
                                     ", assetType=",
                                     assetType,
                                     ", assetFields=",
                                     assetFields,
                                     ", args=",
                                     args,
                                     ")"));
        }
        // getAssetFields re-populates this with our working entity reference.
        const auto assetIdIt = assetFields.find(constants::kEntityReference);
        if (assetIdIt == assetFields.cend())
        {
            throw std::runtime_error("Working EntityReference not specified in post-publish");
        }

        const PublishStrategy& strategy = publishStrategies_.strategyForAssetType(assetType);

        const auto workingEntityReference =
            manager_->createEntityReferenceIfValid(assetIdIt->second);
        if (!workingEntityReference)
        {
            throw std::runtime_error(
                "Error creating EntityReference during pre-publish from Asset ID: " +
                assetIdIt->second);
        }

        assetId = manager_
                      ->register_(workingEntityReference.value(),
                                  strategy.postPublishTraitData(assetFields, args),
                                  openassetio::access::PublishingAccess::kWrite,
                                  context_)
                      .toString();

        if (logger_->isSeverityLogged(Severity::kDebugApi))
        {
            logger_->debugApi(
                logging::concatAsStr("OpenAssetIOAsset::postCreateAsset -> ", assetId));
        }
    }
    catch (const std::exception& exc)
    {
        if (logger_->isSeverityLogged(Severity::kDebug))
        {
            logger_->debug(
                logging::concatAsStr("OpenAssetIOAsset::postCreateAsset -> ERROR: ", exc.what()));
        }
        throw;
    }
}

std::pair<openassetio::EntityReference, std::string>
OpenAssetIOAsset::assetIdToEntityRefAndManagerDrivenValue(const std::string& assetId) const
{
    auto assetIdAndManagerDrivenValue =
        pystring::rsplit(assetId, constants::kAssetIdManagerDrivenValueSep, 1);

    return {manager_->createEntityReference(std::move(assetIdAndManagerDrivenValue.front())),
            assetIdAndManagerDrivenValue.size() > 1 ? std::move(assetIdAndManagerDrivenValue.back())
                                                    : ""};
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
