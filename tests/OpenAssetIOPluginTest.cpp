// KatanaOpenAssetIO
// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 The Foundry Visionmongers Ltd
#include <memory>
#include <catch2/catch_test_macros.hpp>

#include <FnAsset/plugin/FnAsset.h>
#include <FnAsset/suite/FnAssetSuite.h>
#include <FnPluginManager/FnPluginManager.h>

// Disable checks triggered by Catch2 macros.
// NOLINTBEGIN(*-chained-comparison)
namespace
{
/**
 * Get an Asset base class reference from the KatanaOpenAssetIO plugin.
 *
 * Get the plugin from the plugin cache and create an instance.
 *
 * Return a shared_ptr with a custom deleter that uses the C AssetAPI
 * function pointer suite's `destroy` function to ensure proper cleanup
 * of the instance created by the plugin system.
 */
auto assetPluginInstance()
{
    auto* pluginHandle = FnKat::PluginManager::getPlugin("KatanaOpenAssetIO", "AssetPlugin", 1);
    const auto* pluginSuite = FnKat::PluginManager::getPluginSuite(pluginHandle);
    const auto* assetSuite = static_cast<const FnAssetPluginSuite_v1*>(pluginSuite);

    FnAssetHandle instanceHandle = assetSuite->create();

    return std::shared_ptr<FnKat::Asset>{
        &instanceHandle->getAsset(),
        [destroy = assetSuite->destroy, instanceHandle]([[maybe_unused]] FnKat::Asset*)
        { destroy(instanceHandle); }};
}
}  // namespace

TEST_CASE("BAL plugin is loaded")
{
    auto plugin = assetPluginInstance();
    CHECK(plugin->isAssetId("bal:///"));
    CHECK_FALSE(plugin->isAssetId("notbal:///"));
}

SCENARIO("getAssetDisplayName()")
{
    auto plugin = assetPluginInstance();

    REQUIRE(plugin->runAssetPluginCommand(
        "", "initialize", {{"library_path", BAL_DB_DIR "/bal_db_simple_image.json"}}));

    GIVEN("a valid asset ID")
    {
        const std::string assetId = "bal:///cat";

        WHEN("display name is retrieved")
        {
            std::string displayName;
            plugin->getAssetDisplayName(assetId, displayName);

            THEN("display name is as the DisplayName trait's name property")
            {
                CHECK(displayName == "😺");
            }
        }
    }

    GIVEN("an invalid asset ID")
    {
        const std::string assetId = "notbal:///cat";

        WHEN("display name is retrieved")
        {
            std::string displayName;
            plugin->getAssetDisplayName(assetId, displayName);

            THEN("display name is the asset ID")
            {
                CHECK(displayName == "notbal:///cat");
            }
        }
    }
}

/**
 * This test simulates the calls that LookFileMaterialsOut makes when
 * writing a new material lookfile (.klf).
 *
 * The calls were determined by enabling KatanaOpenAssetIO debug
 * logging and replicating them here.
 */
SCENARIO("LookFileMaterialsOut publishing")
{
    auto plugin = assetPluginInstance();
    REQUIRE(plugin->runAssetPluginCommand(
        "",
        "initialize",
        {{"library_path", BAL_DB_DIR "/bal_db_LookFileMaterialsOut_publishing.json"}}));

    GIVEN("an assetId")
    {
        const std::string assetId = "bal:///cat?v=1";

        WHEN("asset fields are retrieved, including defaults")
        {
            FnKat::Asset::StringMap assetFields;
            plugin->getAssetFields(assetId, true, assetFields);

            THEN("fields contain reference, name and version")
            {
                CHECK(assetFields.size() == 3);
                CHECK(assetFields.at("__entityReference") == assetId);
                CHECK(assetFields.at("name") == "Cat");
                CHECK(assetFields.at("version") == "1");
            }

            AND_GIVEN("LookFileMaterialsOut publish args")
            {
                const FnKat::Asset::StringMap args{{"outputFormat", "as archive"}};

                WHEN("asset creation is started")
                {
                    std::string inFlightAssetId;
                    plugin->createAssetAndPath(
                        nullptr, "look file", assetFields, args, true, inFlightAssetId);

                    THEN(
                        "in-flight asset ID is the expected preflight reference with "
                        "manager-driven value appended")
                    {
                        CHECK(inFlightAssetId == "bal:///cat#value=/some/staging/area/cat.klf");
                    }

                    AND_WHEN("in-flight reference path is resolved")
                    {
                        std::string managerDrivenPath;
                        plugin->resolveAsset(inFlightAssetId, managerDrivenPath);

                        THEN("path is to a staging area")
                        {
                            CHECK(managerDrivenPath == "/some/staging/area/cat.klf");
                        }
                    }

                    AND_WHEN("in-flight reference fields are retrieved, excluding defaults")
                    {
                        FnKat::Asset::StringMap inFlightAssetFields;
                        plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);

                        THEN("fields contain in-flight reference, version and manager-driven path")
                        {
                            CHECK(inFlightAssetFields.size() == 4);
                            CHECK(inFlightAssetFields.at("__entityReference") == "bal:///cat");
                            CHECK(inFlightAssetFields.at("__managerDrivenValue") ==
                                  "/some/staging/area/cat.klf");
                            CHECK(inFlightAssetFields.at("name") == "Cat");
                            CHECK(inFlightAssetFields.at("version") == "latest");
                        }

                        AND_WHEN("asset creation is finished")
                        {
                            std::string newAssetId;
                            plugin->postCreateAsset(
                                nullptr, "look file", inFlightAssetFields, args, newAssetId);

                            THEN("new asset ID is the newly registered reference")
                            {
                                CHECK(newAssetId == "bal:///cat?v=2");
                            }

                            THEN("entity has been registered with the in-flight path")
                            {
                                FnKat::Asset::StringMap assetAttributes;
                                plugin->getAssetAttributes(newAssetId, "", assetAttributes);

                                const std::string location = assetAttributes.at(
                                    "openassetio-mediacreation:content_LocatableContent_"
                                    "location");

                                CHECK(location == "file:///some/staging/area/cat.klf");
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * This test simulates the calls made when performing a 'Disk Render'
 * via the Render node, assuming the user selects 'Pre-Render Publish
 * Asset', then 'Disk Render', then 'Post-Render Publish Asset'.
 *
 * This assumes that the KatanaOpenAssetIO patches are applied (i.e.
 * that the plugin script `KatanaOpenAssetIOPatches.py` is installed).
 *
 * The calls were determined by enabling KatanaOpenAssetIO debug
 * logging and performing the actions manually.
 *
 * We do not distinguish between 'Pre-'/'Post-Render Publish Asset' and
 * 'Pre-'/'Post-Render Publish Asset (Version Up)' menu options.
 * - If the user is explicitly publishing then we expect that they want
 *   to notify the asset manager of a potential new version.
 * - If the user wants to overwrite an in-flight render, they can do
 *   this as many times as they like before hitting 'Post-Render Publish
 *   Asset'.
 * - If the user wants to overwrite a previously published render, they
 *   can simply avoid clicking 'Pre-Render Publish Asset'.
 */
SCENARIO("Render node publishing")
{
    auto plugin = assetPluginInstance();
    REQUIRE(plugin->runAssetPluginCommand(
        "", "initialize", {{"library_path", BAL_DB_DIR "/bal_db_Render_publishing.json"}}));

    GIVEN("an assetId")
    {
        const std::string assetId = "bal:///cat?v=1";

        WHEN("asset fields are retrieved, including defaults")
        {
            FnKat::Asset::StringMap assetFields;
            plugin->getAssetFields(assetId, true, assetFields);

            AND_GIVEN("Render pre-publish args")
            {
                const FnKat::Asset::StringMap preArgs{
                    {"colorspace", "linear"},
                    {"ext", "deepexr"},
                    {"filePathTemplate", "/some/permanent/storage/cat.v1.exr"},
                    {"locationSettings.renderLocation", "bal:///cat?v=1"},
                    {"outputName", "deep"},
                    {"res", "square_512"},
                    {"view", ""}};

                WHEN("asset creation is started")
                {
                    std::string inFlightAssetId;
                    plugin->createAssetAndPath(
                        nullptr, "image", assetFields, preArgs, true, inFlightAssetId);

                    THEN(
                        "in-flight asset ID is the expected preflight reference with "
                        "manager-driven value appended")
                    {
                        CHECK(inFlightAssetId ==
                              "bal:///cat#value=/some/staging/area/cat.####.exr");
                    }

                    AND_WHEN("in-flight reference fields are retrieved, including defaults")
                    {
                        FnKat::Asset::StringMap inFlightAssetFields;
                        plugin->getAssetFields(inFlightAssetId, true, inFlightAssetFields);

                        AND_GIVEN("Render post-publish args")
                        {
                            const FnKat::Asset::StringMap postArgs{
                                {"colorspace", "linear"},
                                {"ext", "deepexr"},
                                {"filePathTemplate", "/some/staging/area/cat.####.exr"},
                                {"locationSettings", ""},
                                {"outputName", "deep"},
                                {"res", "square_512"},
                                {"view", ""}};

                            WHEN("asset creation is finished")
                            {
                                std::string newAssetId;
                                plugin->postCreateAsset(
                                    nullptr, "image", inFlightAssetFields, postArgs, newAssetId);

                                THEN("new asset ID is the newly registered reference")
                                {
                                    CHECK(newAssetId == "bal:///cat?v=2");
                                }

                                THEN("entity has been registered with the in-flight path")
                                {
                                    FnKat::Asset::StringMap assetAttributes;
                                    plugin->getAssetAttributes(newAssetId, "", assetAttributes);

                                    const std::string location = assetAttributes.at(
                                        "openassetio-mediacreation:content_LocatableContent_"
                                        "location");

                                    CHECK(location ==
                                          "file:///some/staging/area/cat.%23%23%23%23.exr");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
// NOLINTEND(*-chained-comparison)
