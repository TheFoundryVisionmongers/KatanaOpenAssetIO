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

// NOLINTEND(*-chained-comparison)
