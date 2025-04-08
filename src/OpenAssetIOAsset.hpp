// KatanaOpenAssetIO
// Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>

#include <FnAsset/plugin/FnAsset.h>

#include <openassetio/EntityReference.hpp>
#include <openassetio/hostApi/Manager.hpp>
#include <openassetio/hostApi/ManagerFactory.hpp>
#include <openassetio/utils/path.hpp>

#include "PublishStrategies.hpp"

class OpenAssetIOAsset final : public FnKat::Asset
{
public:
    OpenAssetIOAsset();

    ~OpenAssetIOAsset() override;

    /**
    Factory method to create a new OpenAssetIOAsset instance.
    */
    static Asset* create() { return new OpenAssetIOAsset(); }  // NOLINT(*-owning-memory)

    static void flush() {}

    /** @brief Reset will be called when Katana flushes its caches, giving the plugin a chance to
     * reset.
     */
    void reset() override;

    /** @brief Return whether the input string represents a valid asset id.
     *
     * Should parse the input string to determine whether it is a valid asset id.  Should not
     * attempt to determine whether the asset actually exists in the asset database.
     *
     * @param name The string to test.
     *
     * @return True if the string is valid asset id syntax.
     */
    bool isAssetId(const std::string& name) override;

    /** @brief Return whether the input string contains a valid asset id anywhere within it.
     *
     * Should parse the input string to determine whether it contains a valid asset id.  Should not
     * attempt to determine whether any asset ids found in the string actually exist in the asset
     * database.
     *
     * @param name The string to test.
     *
     * @return True if the string contains a valid asset id.
     */
    bool containsAssetId(const std::string& name) override;

    /** @brief Returns whether permissions for the given asset id are valid in the given context.
     *
     * @param  assetId Asset id to check permissions for.
     * @param  context Additional strings used to specify more details about permissions to be
     * checked. Examples include current user or current show environment.
     *
     * @return True if permissions for the given asset id are valid in the given context.
     */
    bool checkPermissions(const std::string& assetId, const StringMap& context) override;

    /** @brief Runs a custom command for the given asset id.
     *
     * @param  assetId Asset id the command will be run on.
     * @param  command Name of the command to run.
     * @param  commandArgs Key-string pair representing the command arguments.
     *
     * @return True if the command execution succeeds.
     */
    bool runAssetPluginCommand(const std::string& assetId,
                               const std::string& command,
                               const StringMap& commandArgs) override;

    /** @brief Lookup asset id in asset system and return path (or other string) that it references.
     *
     * @param assetId Asset id to resolve.
     * @param resolvedAsset Set to resolved asset string.
     */
    void resolveAsset(const std::string& assetId, std::string& resolvedAsset) override;

    /** @brief Replace any asset ids found in input string with resolved asset strings.
     *
     * @param str Input string to resolve.
     *
     * @param ret Set to resolved asset string.
     */
    void resolveAllAssets(const std::string& str, std::string& ret) override;

    /** @brief Resolve env vars in input path string, then resolve asset ids and file sequences.
     *
     * File sequence will most likely be resolved using default file sequence plugin from host.
     * This is a good candidate for some base functionality in the C++ wrappers on the plugin side.
     *
     * @param str Input path string to resolve.
     * @param frame Frame number to resolve file sequences with.
     *
     * @param ret Set to resolved path string.
     */
    void resolvePath(const std::string& str, int frame, std::string& ret) override;

    /** @brief Return the version that this asset id resolves to.
     *
     * @param assetId Input asset id resolve.
     *
     * @param ret Returns the version that this asset id resolves to.
     * @param versionStr In some asset management systems the version can be
     *                   specified by keywords (example: latest, aproved, etc)
     *                   that might be associated to a specific version number.
     *                   If versionStr is specified, then the plugin will try
     *                   to return (on ret) the resolved string number for the
     *                   assetId specified by str and the given version keyword
     *                   (the versionStr value).
     */
    void resolveAssetVersion(const std::string& assetId,
                             std::string& ret,
                             const std::string& versionStr) override;

    /** @brief Return a valid scenegraph location path that uniquely represents the input asset id.
     *
     * This will be used as the default location for look file materials loaded into
     * the Katana scene, for example.  (Example: /name/version/other/fields).
     *
     * @param assetId Input asset id to represent as scenegraph location path.
     * @param includeVersion allows to specify if the scenegraph location path should contain the
     * asset version
     *
     * @param ret Set to scenegraph location path.
     */
    void getUniqueScenegraphLocationFromAssetId(const std::string& assetId,
                                                bool includeVersion,
                                                std::string& ret) override;

    /** @brief Returns a string that can be used in GUIs, for example.
     *
     * @param assetId The asset for which the display name is required
     *
     * @param ret Return value with the display name.
     */
    void getAssetDisplayName(const std::string& assetId, std::string& ret) override;

    /** @brief Returns a vector of strings listing the available versions for the given asset.
     *
     * @param assetId The asset who's versions we want
     *
     * @param ret The string vector of available versions
     */
    void getAssetVersions(const std::string& assetId, StringVector& ret) override;

    /** @brief Return asset id that is related to input asset, given a relationship type.
     *
     * A general function for getting related assets.  An current example in Katana is
     * an "argsxml" asset defining the UI for a renderer procedural asset.
     *
     * @param assetId Input asset id for which to find related asset.
     * @param relation String representing relationship type.
     *
     * @param ret Set to related assed id (or empty string if no related asset is found).
     */
    void getRelatedAssetId(const std::string& assetId,
                           const std::string& relation,
                           std::string& ret) override;

    /** @brief Convert from asset id to a set of named string fields defining the asset.
     *
     * The minimum set of fields are "name" and "version".  Other fields are preserved but ignored
     * by Katana.
     *
     * @param assetId Input asset id.
     * @param includeDefaults If true, return the full set of fields for this asset system,
     * including defaults not specified in the asset id.  If false, return only the fields specified
     * in the asset id.
     *
     * @param returnFields Fill map with key/value strings representing asset fields.
     */
    void getAssetFields(const std::string& assetId,
                        bool includeDefaults,
                        StringMap& returnFields) override;

    /** @brief Convert from asset fields to asset id
     *
     * The inverse of getAssetFields, this takes a set of asset fields and returns an asset id
     * string.
     *
     * @param fields String map containing key/value strings representing asset fields.
     *
     * @param ret Set to asset id.
     */
    void buildAssetId(const StringMap& fields, std::string& ret) override;

    /** @brief Get metadata associated with an asset or a scoped item in the asset hierarchy.
     *
     * This differs from the above "asset fields" which are the fields that uniquely define
     * the asset itself.  This metadata may include fields such as asset creator, creation time,
     * etc.
     *
     * @param assetId Asset for which to return metadata.
     * @param scope Optional string specifying scope for metadata lookup, such as "name" or
     * "version".
     *
     * @param returnAttrs Fill with key/value strings representing metadata attributes.
     */
    void getAssetAttributes(const std::string& assetId,
                            const std::string& scope,
                            StringMap& returnAttrs) override;

    /** @brief Set metadata associated with an asset or a scoped item in the asset hierarchy.
     *
     * This differs from the above "asset fields" which are the fields that uniquely define
     * the asset itself.  This metadata may include fields such as asset creator, creation time,
     * etc.
     *
     * @param assetId Asset for which to set metadata.
     * @param scope Optional string specifying scope for metadata lookup, such as "name" or
     * "version".
     * @param attrs String map containing key/value strings representing metadata attributes.
     */
    void setAssetAttributes(const std::string& assetId,
                            const std::string& scope,
                            const StringMap& attrs) override;

    /** @brief Get partial asset id scoped to the specified level in the asset hierarchy.
     *
     * Should return a more general asset reference to the given asset id.  For instance,
     * if scope is "name", return an asset id that doesn't include a specific version number.
     * This may not make sense in some asset systems, in which case, return the input asset id.
     *
     * @param assetId Input asset id.
     * @param scope String specifying scope for result asset id, such as "name" or "version".
     *
     * @param ret Set to result asset id.
     */
    void getAssetIdForScope(const std::string& assetId,
                            const std::string& scope,
                            std::string& ret) override;

    /** @brief Create asset and optional directory path.
     *
     * @param txn Handle to transaction object.  If null, asset/directory creation is done
     * immediately.
     * @param assetType Type of Katana asset to create (e.g. katana scene, image, shadow, etc)
     * @param assetFields GroupAttribute representing asset fields for asset to create (same as
     * getAssetFields output).
     * @param args Additional args used to specify more details (may differ based on assetType).
     * Examples include "versionUp", which is a boolean determining whether to version up an
     * existing asset or "colorspace" if the assetId is of the type "image" (in which if this is
     * provided it should be seen as an Attribute to be associated with the asset)
     * @param createDirectory Boolean specifying whether to create the directory associated with
     * this asset.
     *
     * @param assetId Set to the asset id created (or that will be created on transaction commit).
     */
    void createAssetAndPath(FnKat::AssetTransaction* txn,
                            const std::string& assetType,
                            const StringMap& assetFields,
                            const StringMap& args,
                            bool createDirectory,
                            std::string& assetId) override;

    /** @brief Creates an asset after the creation of the files to be published.
     *
     * @param txn Handle to transaction object.  If null, asset/directory creation is done
     * immediately.
     * @param assetType Type of Katana asset to create (e.g. katana scene, image, shadow, etc)
     * @param assetFields GroupAttribute representing asset fields for asset to create (same as
     * getAssetFields output).
     * @param args Additional args used to specify more details (may differ based on assetType).
     * Examples include "versionUp", which is a boolean determining whether to version up an
     * existing asset.
     *
     * @param assetId Set to the asset id created (or that will be created on transaction commit).
     */
    void postCreateAsset(FnKat::AssetTransaction* txn,
                         const std::string& assetType,
                         const StringMap& assetFields,
                         const StringMap& args,
                         std::string& assetId) override;

private:
    std::optional<openassetio::EntityReference> entityRefForAssetIdAndVersion(
        const std::string& assetId,
        const std::string& desiredVersionTag) const;

    PublishStrategies publishStrategies_;

    openassetio::hostApi::ManagerPtr manager_;
    openassetio::ContextPtr context_;
    openassetio::utils::FileUrlPathConverter fileUrlPathConverter_;
};
