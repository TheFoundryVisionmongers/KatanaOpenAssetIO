// KatanaOpenAssetIO
// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 The Foundry Visionmongers Ltd
#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <FnAsset/plugin/FnAsset.h>
#include <FnAsset/suite/FnAssetSuite.h>
#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnAttributeBase.h>
#include <FnAttribute/suite/FnAttributeSuite.h>
#include <FnPluginManager/FnPluginManager.h>

namespace std
{
/**
 * Make StringMap printable in Catch2 assertion macro failure output.
 */
// NOLINTNEXTLINE(*-use-internal-linkage)
std::ostream& operator<<(std::ostream& ostr, const std::pair<std::string, std::string>& value)
{
    // Double-newline to break up output, since we're using pre-compiled
    // Catch2, where CATCH_CONFIG_CONSOLE_WIDTH=80 by default.
    ostr << "\n\n  '" << value.first << "' = '" << value.second << "'";
    return ostr;
}
}  // namespace std

namespace
{
/*
 * Get an Asset base class instance, as well as the C suite and handle
 * that wrap it, from the KatanaOpenAssetIO plugin.
 *
 * Return a shared_ptr with a custom deleter that uses the C AssetAPI
 * function pointer suite's `destroy` function to ensure proper cleanup
 * of the instance created by the plugin system.
 */
auto assetPluginInstanceAndSuiteAndHandle()
{
    auto* pluginHandle = FnKat::PluginManager::getPlugin("KatanaOpenAssetIO", "AssetPlugin", 1);
    const auto* pluginSuite = FnKat::PluginManager::getPluginSuite(pluginHandle);
    const auto* assetSuite = static_cast<const FnAssetPluginSuite_v1*>(pluginSuite);

    FnAssetHandle instanceHandle = assetSuite->create();

    std::shared_ptr<FnKat::Asset> instance{
        &instanceHandle->getAsset(),
        [destroy = assetSuite->destroy, instanceHandle]([[maybe_unused]] FnKat::Asset*)
        { destroy(instanceHandle); }};

    return std::tuple{std::move(instance), assetSuite, instanceHandle};
}

/**
 * Get an Asset base class instance from the KatanaOpenAssetIO plugin.
 */
auto assetPluginInstance()
{
    auto [instance, assetSuite, instanceHandle] = assetPluginInstanceAndSuiteAndHandle();
    return instance;
}
}  // namespace

// Disable checks triggered by Catch2 macros.
// NOLINTBEGIN(*-chained-comparison,*-function-cognitive-complexity,*-container-size-empty)

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
 * Check that the getAssetAttributes function returns the expected
 * values and that the C API reflects those values.
 *
 * We must check the C API because it returns a GroupAttribute rather
 * than a StringMap.
 *
 * When reading/writing an element in a GroupAttribute, `.`s in the key
 * are a shorthand for referencing a nested element.
 *
 * The Asset API within Katana (e.g. in the Python console) transforms
 * the GroupAttribute back to a flat dictionary, losing any nested
 * elements.
 *
 * So we cannot have `.`s in our attribute names.
 *
 * An exception is the DefaultAssetPlugin API. It instead encodes the
 * entire StringMap as a single StringAttribute, so `.`s in the key are
 * kept verbatim.
 */
SCENARIO("getAssetAttributes()")
{
    auto [plugin, suite, handle] = assetPluginInstanceAndSuiteAndHandle();

    REQUIRE(plugin->runAssetPluginCommand(
        "", "initialize", {{"library_path", BAL_DB_DIR "/bal_db_simple_image.json"}}));

    WHEN("asset attributes retrieved from plugin C++ API")
    {
        FnKat::Asset::StringMap attrsAsStringMap;
        plugin->getAssetAttributes("bal:///cat", "", attrsAsStringMap);

        THEN("asset attributes have expected values")
        {
            const FnKat::Asset::StringMap expected = {
                {"openassetio-mediacreation:usage,Entity", ""},
                {"openassetio-mediacreation:twoDimensional,Image", ""},
                {"openassetio-mediacreation:identity,DisplayName", ""},
                {"openassetio-mediacreation:identity,DisplayName,name", "😺"},
                {"openassetio-mediacreation:identity,DisplayName,qualifiedName", "a/cat"},
                {"openassetio-mediacreation:content,LocatableContent", ""},
                {"openassetio-mediacreation:content,LocatableContent,location",
                 "file:///some/permanent/storage/cat.v1.%23%23.exr"},
                {"openassetio-mediacreation:content,LocatableContent,isTemplated", "true"},
                {"openassetio-mediacreation:lifecycle,Version", ""},
                {"openassetio-mediacreation:lifecycle,Version,specifiedTag", "latest"},
                {"openassetio-mediacreation:lifecycle,Version,stableTag", "1"}};
            CHECK(attrsAsStringMap == expected);
        }

        AND_WHEN("asset attributes are retrieved through the C API")
        {
            FnAttributeHandle errorMessage = nullptr;
            FnAttributeHandle attrsAsGroupAttrHandle =
                suite->getAssetAttributes(handle, "bal:///cat", "", &errorMessage);
            const FnAttribute::GroupAttribute attrsAsGroupAttr =
                FnAttribute::Attribute::CreateAndSteal(attrsAsGroupAttrHandle);

            THEN("plugin and C API match")
            {
                constexpr std::size_t kExpectedNumAttrs = 11;
                CHECK(attrsAsGroupAttr.getNumberOfChildren() == kExpectedNumAttrs);

                for (const auto& [key, value] : attrsAsStringMap)
                {
                    const FnAttribute::StringAttribute expectedValue{value};
                    const FnAttribute::StringAttribute actualValue =
                        attrsAsGroupAttr.getChildByName(key);
                    CHECK(actualValue == expectedValue);
                }
            }
        }
    }
}

/**
 * This test simulates the calls that LookFileBake and
 * LookFileMaterialsOut makes when writing a new material lookfile
 * (.klf).
 *
 * Technically LookFileBake has an extra arg of "fileExtension", but
 * this doesn't add any information for us, so is left unset in the
 * tests.
 *
 * Plugins can add more output formats to LookFileBake. By default,
 * an output format produces multiple files, so the asset system should
 * return a writeable directory. The default "as archive" (.klf) format
 * is a special case.
 *
 * The calls were determined by enabling KatanaOpenAssetIO debug
 * logging and replicating them here.
 */
SCENARIO("LookFileBake / LookFileMaterialsOut publishing")
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

            AND_GIVEN("LookFile publish as archive args")
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

                            THEN("entity has been registered with expected traits")
                            {
                                FnKat::Asset::StringMap actual;
                                plugin->getAssetAttributes(newAssetId, "", actual);

                                const FnKat::Asset::StringMap expected = {
                                    {"openassetio-mediacreation:usage,Entity", ""},
                                    {"openassetio-mediacreation:application,Work", ""},
                                    {"openassetio-mediacreation:lifecycle,Version", ""},
                                    {"openassetio-mediacreation:lifecycle,Version,specifiedTag",
                                     "2"},
                                    {"openassetio-mediacreation:lifecycle,Version,stableTag", "2"},
                                    {"openassetio-mediacreation:content,LocatableContent", ""},
                                    {"openassetio-mediacreation:content,LocatableContent,location",
                                     "file:///some/staging/area/cat.klf"},
                                    {"openassetio-mediacreation:content,LocatableContent,mimeType",
                                     "application/vnd.foundry.katana.lookfile"}};

                                CHECK(actual == expected);
                            }
                        }
                    }
                }
            }

            AND_GIVEN("LookFile published as another output format")
            {
                const FnKat::Asset::StringMap args{{"outputFormat", "anything else"}};

                WHEN("asset creation is started")
                {
                    std::string inFlightAssetId;
                    // TODO(DF): Need a way to validate the parameters passed to `preflight()`.
                    plugin->createAssetAndPath(
                        nullptr, "look file", assetFields, args, true, inFlightAssetId);

                    AND_WHEN("in-flight reference fields are retrieved, excluding defaults")
                    {
                        FnKat::Asset::StringMap inFlightAssetFields;
                        plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);

                        AND_WHEN("asset creation is finished")
                        {
                            std::string newAssetId;
                            plugin->postCreateAsset(
                                nullptr, "look file", inFlightAssetFields, args, newAssetId);

                            THEN("entity has been registered with directory MIME type")
                            {
                                FnKat::Asset::StringMap assetAttributes;
                                plugin->getAssetAttributes(newAssetId, "", assetAttributes);

                                const std::string mimeType = assetAttributes.at(
                                    "openassetio-mediacreation:content,LocatableContent,"
                                    "mimeType");

                                CHECK(mimeType == "inode/directory");
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

                                THEN("entity has been registered with expected traits")
                                {
                                    FnKat::Asset::StringMap actual;
                                    plugin->getAssetAttributes(newAssetId, "", actual);

                                    const FnKat::Asset::StringMap expected = {
                                        {"openassetio-mediacreation:usage,Entity", ""},
                                        {"openassetio-mediacreation:twoDimensional,Image", ""},
                                        {"openassetio-mediacreation:twoDimensional,PixelBased", ""},
                                        {"openassetio-mediacreation:twoDimensional,Deep", ""},
                                        {"openassetio-mediacreation:lifecycle,Version", ""},
                                        {"openassetio-mediacreation:lifecycle,Version,specifiedTag",
                                         "2"},
                                        {"openassetio-mediacreation:lifecycle,Version,stableTag",
                                         "2"},
                                        {"openassetio-mediacreation:identity,DisplayName", ""},
                                        {"openassetio-mediacreation:identity,DisplayName,name",
                                         "deep"},
                                        {"openassetio-mediacreation:identity,DisplayName,"
                                         "qualifiedName",
                                         "deep"},
                                        {"openassetio-mediacreation:color,OCIOColorManaged", ""},
                                        {"openassetio-mediacreation:color,OCIOColorManaged,"
                                         "colorspace",
                                         "linear"},
                                        {"openassetio-mediacreation:content,LocatableContent", ""},
                                        {"openassetio-mediacreation:content,LocatableContent,"
                                         "location",
                                         "file:///some/staging/area/cat.%23%23%23%23.exr"},
                                        {"openassetio-mediacreation:content,LocatableContent,"
                                         "mimeType",
                                         "image/x-exr"}};

                                    CHECK(actual == expected);
                                }
                            }
                        }

                        AND_GIVEN("alternative Render post-publish args")
                        {
                            const FnKat::Asset::StringMap postArgs{{"colorspace", "sRGB"},
                                                                   {"ext", "png"},
                                                                   {"outputName", "other name"}};

                            WHEN("asset creation is finished")
                            {
                                std::string newAssetId;
                                plugin->postCreateAsset(
                                    nullptr, "image", inFlightAssetFields, postArgs, newAssetId);

                                THEN("registered entity's trait properties have alternative values")
                                {
                                    FnKat::Asset::StringMap actual;
                                    plugin->getAssetAttributes(newAssetId, "", actual);

                                    FnKat::Asset::StringMap expected = {
                                        {"openassetio-mediacreation:identity,DisplayName,name",
                                         "other name"},
                                        {"openassetio-mediacreation:identity,DisplayName,"
                                         "qualifiedName",
                                         "other name"},
                                        {"openassetio-mediacreation:color,OCIOColorManaged,"
                                         "colorspace",
                                         "sRGB"},
                                        {"openassetio-mediacreation:content,LocatableContent,"
                                         "mimeType",
                                         "image/png"}};

                                    // `merge()` doesn't overwrite.
                                    expected.merge(FnKat::Asset::StringMap{actual});

                                    CHECK(actual == expected);
                                }
                            }
                        }
                    }
                }
            }

            AND_GIVEN("unsupported file extension in args")
            {
                const FnKat::Asset::StringMap postArgs{{"ext", "some_unsupported_ext"}};

                WHEN("asset is published")
                {
                    std::string newAssetId;
                    plugin->postCreateAsset(nullptr, "image", assetFields, postArgs, newAssetId);

                    THEN("MIME type is unavailable")
                    {
                        FnKat::Asset::StringMap attrs;
                        plugin->getAssetAttributes(newAssetId, "", attrs);

                        CHECK(attrs.count("openassetio-mediacreation:content,LocatableContent,"
                                          "mimeType") == 0);
                    }
                }
            }

            AND_GIVEN("supported file extension in args")
            {
                using P = std::pair<std::string, std::string>;

                const P extAndMIME =
                    GENERATE(P{"exr", "image/x-exr"},
                             P{"deepexr", "image/x-exr"},
                             P{"tif", "image/tiff"},
                             P{"png", "image/png"},
                             P{"jpg", "image/jpeg"},
                             P{"rla", "image/x-rla"},
                             P{"dtex", "image/x-dtex"},
                             P{"deepshad", "image/x-deepshad"},
                             P{"hist", "application/vnd.foundry.katana.histogram+xml"});

                const FnKat::Asset::StringMap postArgs{
                    {"ext", extAndMIME.first},
                };

                WHEN("asset is published")
                {
                    std::string newAssetId;
                    plugin->postCreateAsset(nullptr, "image", assetFields, postArgs, newAssetId);

                    THEN("MIME type is as expected")
                    {
                        FnKat::Asset::StringMap attrs;
                        plugin->getAssetAttributes(newAssetId, "", attrs);

                        CHECK(attrs.at("openassetio-mediacreation:content,LocatableContent,"
                                       "mimeType") == extAndMIME.second);
                    }
                }
            }

            AND_GIVEN("deep file extension in args")
            {
                const std::string ext = GENERATE("deepexr", "dtex", "deepshad");

                const FnKat::Asset::StringMap postArgs{
                    {"ext", ext},
                };

                WHEN("asset is published")
                {
                    std::string newAssetId;
                    plugin->postCreateAsset(nullptr, "image", assetFields, postArgs, newAssetId);

                    THEN("DeepTrait is imbued")
                    {
                        FnKat::Asset::StringMap attrs;
                        plugin->getAssetAttributes(newAssetId, "", attrs);

                        CHECK(attrs.count("openassetio-mediacreation:twoDimensional,Deep") == 1);
                    }
                }
            }

            AND_GIVEN("non-deep file extension in args")
            {
                const std::string ext = GENERATE("exr", "tif", "png", "jpg", "rla", "hist");

                const FnKat::Asset::StringMap postArgs{
                    {"ext", ext},
                };

                WHEN("asset is published")
                {
                    std::string newAssetId;
                    plugin->postCreateAsset(nullptr, "image", assetFields, postArgs, newAssetId);

                    THEN("DeepTrait is not imbued")
                    {
                        FnKat::Asset::StringMap attrs;
                        plugin->getAssetAttributes(newAssetId, "", attrs);

                        CHECK(attrs.count("openassetio-mediacreation:twoDimensional,Deep") == 0);
                    }
                }
            }
        }
    }
}

/**
 * This test simulates the "File->Save" and "File->Version Up and Save"
 * menu options.
 *
 * Katana involves the asset manager in both of these cases, and
 * distinguishes between these via a "versionUp" flag. A good analogy is
 * creating a git revision vs. a git tag.
 *
 * This is simulated in KatanaOpenAssetIO by using a `kWrite`
 * relationship query for the explicit version, if supported.
 */
SCENARIO("Katana scene publishing")
{
    auto plugin = assetPluginInstance();
    REQUIRE(plugin->runAssetPluginCommand(
        "", "initialize", {{"library_path", BAL_DB_DIR "/bal_db_Katana_scene_publishing.json"}}));

    GIVEN("an assetId")
    {
        const std::string assetId = "bal:///cat/v1";

        WHEN("asset fields are retrieved, excluding defaults")
        {
            FnKat::Asset::StringMap assetFields;
            plugin->getAssetFields(assetId, false, assetFields);

            AND_GIVEN("File->Save args")
            {
                const FnKat::Asset::StringMap args{{"publish", "False"}, {"versionUp", "False"}};

                WHEN("asset creation is started")
                {
                    std::string inFlightAssetId;
                    plugin->createAssetAndPath(
                        nullptr, "katana scene", assetFields, args, true, inFlightAssetId);

                    AND_WHEN("in-flight reference path is resolved")
                    {
                        std::string managerDrivenPath;
                        plugin->resolveAsset(inFlightAssetId, managerDrivenPath);

                        THEN("path is to a staging area for a revision")
                        {
                            CHECK(managerDrivenPath == "/some/staging/area/cat.v1.rev2.katana");
                        }
                    }

                    AND_WHEN("in-flight reference fields are retrieved, excluding defaults")
                    {
                        FnKat::Asset::StringMap inFlightAssetFields;
                        plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);

                        AND_WHEN("asset creation is finished")
                        {
                            std::string newAssetId;
                            plugin->postCreateAsset(
                                nullptr, "katana scene", inFlightAssetFields, args, newAssetId);

                            THEN("entity has been registered with expected traits")
                            {
                                FnKat::Asset::StringMap actual;
                                plugin->getAssetAttributes(newAssetId, "", actual);

                                const FnKat::Asset::StringMap expected = {
                                    {"openassetio-mediacreation:usage,Entity", ""},
                                    {"openassetio-mediacreation:application,Work", ""},
                                    {"openassetio-mediacreation:lifecycle,Version", ""},
                                    {"openassetio-mediacreation:lifecycle,Version,specifiedTag",
                                     "2"},
                                    {"openassetio-mediacreation:lifecycle,Version,stableTag", "2"},
                                    {"openassetio-mediacreation:content,LocatableContent", ""},
                                    // Second revision of same version.
                                    {"openassetio-mediacreation:content,LocatableContent,location",
                                     "file:///some/staging/area/cat.v1.rev2.katana"},
                                    {"openassetio-mediacreation:content,LocatableContent,mimeType",
                                     "application/vnd.foundry.katana.project"}};

                                CHECK(actual == expected);
                            }
                        }
                    }
                }
            }

            AND_GIVEN("File->Version Up and Save args")
            {
                const FnKat::Asset::StringMap args{{"publish", "True"}, {"versionUp", "True"}};

                WHEN("asset creation is started")
                {
                    std::string inFlightAssetId;
                    plugin->createAssetAndPath(
                        nullptr, "katana scene", assetFields, args, true, inFlightAssetId);

                    AND_WHEN("in-flight reference path is resolved")
                    {
                        std::string managerDrivenPath;
                        plugin->resolveAsset(inFlightAssetId, managerDrivenPath);

                        THEN("path is to a staging area")
                        {
                            CHECK(managerDrivenPath == "/some/staging/area/cat.v2.rev1.katana");
                        }
                    }

                    AND_WHEN("in-flight reference fields are retrieved, excluding defaults")
                    {
                        FnKat::Asset::StringMap inFlightAssetFields;
                        plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);

                        AND_WHEN("asset creation is finished")
                        {
                            std::string newAssetId;
                            plugin->postCreateAsset(
                                nullptr, "katana scene", inFlightAssetFields, args, newAssetId);

                            THEN("entity has been registered with expected traits")
                            {
                                FnKat::Asset::StringMap actual;
                                plugin->getAssetAttributes(newAssetId, "", actual);

                                const FnKat::Asset::StringMap expected = {
                                    {"openassetio-mediacreation:usage,Entity", ""},
                                    {"openassetio-mediacreation:application,Work", ""},
                                    {"openassetio-mediacreation:lifecycle,Version", ""},
                                    {"openassetio-mediacreation:lifecycle,Version,specifiedTag",
                                     "2"},
                                    {"openassetio-mediacreation:lifecycle,Version,stableTag", "2"},
                                    {"openassetio-mediacreation:content,LocatableContent", ""},
                                    // First revision of new version.
                                    {"openassetio-mediacreation:content,LocatableContent,location",
                                     "file:///some/staging/area/cat.v2.rev1.katana"},
                                    {"openassetio-mediacreation:content,LocatableContent,mimeType",
                                     "application/vnd.foundry.katana.project"}};

                                CHECK(actual == expected);
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * This test simulates LookFileManager "Export Manager Settings..." menu
 * action.
 *
 * This is a simple case with no additional metadata, other than an
 * (invented) MIME type.
 */
SCENARIO("LookFileManager settings publishing")
{
    auto plugin = assetPluginInstance();
    REQUIRE(plugin->runAssetPluginCommand(
        "",
        "initialize",
        {{"library_path", BAL_DB_DIR "/bal_db_LookFileManager_settings_publishing.json"}}));

    GIVEN("target asset")
    {
        const std::string assetId = "bal:///cat?v=1";
        FnKat::Asset::StringMap assetFields;
        plugin->getAssetFields(assetId, false, assetFields);

        AND_GIVEN("Export Manager Settings args (i.e. empty)")
        {
            const FnKat::Asset::StringMap args{};

            WHEN("asset is published")
            {
                std::string inFlightAssetId;
                plugin->createAssetAndPath(nullptr,
                                           "look file manager settings",
                                           assetFields,
                                           args,
                                           true,
                                           inFlightAssetId);
                FnKat::Asset::StringMap inFlightAssetFields;
                plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);
                std::string newAssetId;
                plugin->postCreateAsset(
                    nullptr, "look file manager settings", inFlightAssetFields, args, newAssetId);

                THEN("entity has been registered with expected traits")
                {
                    FnKat::Asset::StringMap actual;
                    plugin->getAssetAttributes(newAssetId, "", actual);

                    const FnKat::Asset::StringMap expected = {
                        {"openassetio-mediacreation:usage,Entity", ""},
                        {"openassetio-mediacreation:application,Work", ""},
                        {"openassetio-mediacreation:application,Config", ""},
                        {"openassetio-mediacreation:lifecycle,Version", ""},
                        {"openassetio-mediacreation:lifecycle,Version,specifiedTag", "2"},
                        {"openassetio-mediacreation:lifecycle,Version,stableTag", "2"},
                        {"openassetio-mediacreation:content,LocatableContent", ""},
                        {"openassetio-mediacreation:content,LocatableContent,location",
                         "file:///some/staging/area/cat.lfmexport"},
                        {"openassetio-mediacreation:content,LocatableContent,mimeType",
                         "application/vnd.foundry.katana.lookfilemanager-settings+xml"}};

                    CHECK(actual == expected);
                }
            }
        }
    }
}

/**
 * This test simulates LiveGroup "Publish..." menu action.
 *
 * This is a simple case with no additional metadata, other than an
 * (invented) MIME type.
 */
SCENARIO("LiveGroup publishing")
{
    auto plugin = assetPluginInstance();
    REQUIRE(plugin->runAssetPluginCommand(
        "", "initialize", {{"library_path", BAL_DB_DIR "/bal_db_LiveGroup_publishing.json"}}));

    GIVEN("target asset")
    {
        const std::string assetId = "bal:///cat?v=1";
        FnKat::Asset::StringMap assetFields;
        plugin->getAssetFields(assetId, false, assetFields);

        AND_GIVEN("LiveGroup publish args (i.e. empty)")
        {
            const FnKat::Asset::StringMap args{};

            WHEN("asset is published")
            {
                std::string inFlightAssetId;
                plugin->createAssetAndPath(
                    nullptr, "live group", assetFields, args, true, inFlightAssetId);
                FnKat::Asset::StringMap inFlightAssetFields;
                plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);
                std::string newAssetId;
                plugin->postCreateAsset(
                    nullptr, "live group", inFlightAssetFields, args, newAssetId);

                THEN("entity has been registered with expected traits")
                {
                    FnKat::Asset::StringMap actual;
                    plugin->getAssetAttributes(newAssetId, "", actual);

                    const FnKat::Asset::StringMap expected = {
                        {"openassetio-mediacreation:usage,Entity", ""},
                        {"openassetio-mediacreation:application,Work", ""},
                        {"openassetio-mediacreation:lifecycle,Version", ""},
                        {"openassetio-mediacreation:lifecycle,Version,specifiedTag", "2"},
                        {"openassetio-mediacreation:lifecycle,Version,stableTag", "2"},
                        {"openassetio-mediacreation:content,LocatableContent", ""},
                        {"openassetio-mediacreation:content,LocatableContent,location",
                         "file:///some/staging/area/cat.livegroup"},
                        {"openassetio-mediacreation:content,LocatableContent,mimeType",
                         "application/vnd.foundry.katana.livegroup+xml"}};

                    CHECK(actual == expected);
                }
            }
        }
    }
}

/**
 * This test simulates a GafferThree "Export Rig" menu action.
 *
 * This is a simple case with no additional metadata, other than an
 * (invented) MIME type.
 *
 * The asset browser widget delegate can augment the args, but by
 * default they are blank.
 */
SCENARIO("GafferThree rig publishing")
{
    auto plugin = assetPluginInstance();
    REQUIRE(plugin->runAssetPluginCommand(
        "",
        "initialize",
        {{"library_path", BAL_DB_DIR "/bal_db_GafferThree_rig_publishing.json"}}));

    GIVEN("target asset")
    {
        const std::string assetId = "bal:///cat?v=1";
        FnKat::Asset::StringMap assetFields;
        plugin->getAssetFields(assetId, false, assetFields);

        AND_GIVEN("GafferThree rig publish args (i.e. empty)")
        {
            const FnKat::Asset::StringMap args{};

            WHEN("asset is published")
            {
                std::string inFlightAssetId;
                plugin->createAssetAndPath(
                    nullptr, "gafferthree rig", assetFields, args, true, inFlightAssetId);
                FnKat::Asset::StringMap inFlightAssetFields;
                plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);
                std::string newAssetId;
                plugin->postCreateAsset(
                    nullptr, "gafferthree rig", inFlightAssetFields, args, newAssetId);

                THEN("entity has been registered with expected traits")
                {
                    FnKat::Asset::StringMap actual;
                    plugin->getAssetAttributes(newAssetId, "", actual);

                    const FnKat::Asset::StringMap expected = {
                        {"openassetio-mediacreation:usage,Entity", ""},
                        {"openassetio-mediacreation:threeDimensional,Lighting", ""},
                        {"openassetio-mediacreation:threeDimensional,Spatial", ""},
                        {"openassetio-mediacreation:lifecycle,Version", ""},
                        {"openassetio-mediacreation:lifecycle,Version,specifiedTag", "2"},
                        {"openassetio-mediacreation:lifecycle,Version,stableTag", "2"},
                        {"openassetio-mediacreation:content,LocatableContent", ""},
                        {"openassetio-mediacreation:content,LocatableContent,location",
                         "file:///some/staging/area/cat.rig"},
                        {"openassetio-mediacreation:content,LocatableContent,mimeType",
                         "application/vnd.foundry.katana.rig+xml"}};

                    CHECK(actual == expected);
                }
            }
        }
    }
}

/**
 * This test simulates a "Save as Macro..." from the wrench menu on a
 * node's Parameters panel.
 */
SCENARIO("Macro publishing")
{
    auto plugin = assetPluginInstance();
    REQUIRE(plugin->runAssetPluginCommand(
        "", "initialize", {{"library_path", BAL_DB_DIR "/bal_db_macro_publishing.json"}}));

    GIVEN("target asset")
    {
        const std::string assetId = "bal:///cat?v=1";
        FnKat::Asset::StringMap assetFields;
        plugin->getAssetFields(assetId, false, assetFields);

        AND_GIVEN("GafferThree rig publish args (i.e. empty)")
        {
            const FnKat::Asset::StringMap args{};

            WHEN("asset is published")
            {
                std::string inFlightAssetId;
                plugin->createAssetAndPath(
                    nullptr, "macro", assetFields, args, true, inFlightAssetId);
                FnKat::Asset::StringMap inFlightAssetFields;
                plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);
                std::string newAssetId;
                plugin->postCreateAsset(nullptr, "macro", inFlightAssetFields, args, newAssetId);

                THEN("entity has been registered with expected traits")
                {
                    FnKat::Asset::StringMap actual;
                    plugin->getAssetAttributes(newAssetId, "", actual);

                    const FnKat::Asset::StringMap expected = {
                        {"openassetio-mediacreation:usage,Entity", ""},
                        {"openassetio-mediacreation:application,Work", ""},
                        {"openassetio-mediacreation:lifecycle,Version", ""},
                        {"openassetio-mediacreation:lifecycle,Version,specifiedTag", "2"},
                        {"openassetio-mediacreation:lifecycle,Version,stableTag", "2"},
                        {"openassetio-mediacreation:content,LocatableContent", ""},
                        {"openassetio-mediacreation:content,LocatableContent,location",
                         "file:///some/staging/area/cat.macro"},
                        {"openassetio-mediacreation:content,LocatableContent,mimeType",
                         "application/vnd.foundry.katana.macro"}};

                    CHECK(actual == expected);
                }
            }
        }
    }
}

/**
 * This test simulates a "Export FCurve..." from the right-click menu on
 * a curve parameter.
 */
SCENARIO("FCurve publishing")
{
    auto plugin = assetPluginInstance();
    REQUIRE(plugin->runAssetPluginCommand(
        "", "initialize", {{"library_path", BAL_DB_DIR "/bal_db_fcurve_publishing.json"}}));

    GIVEN("target asset")
    {
        const std::string assetId = "bal:///cat?v=1";
        FnKat::Asset::StringMap assetFields;
        plugin->getAssetFields(assetId, true, assetFields);

        AND_GIVEN("fcurve export args (i.e. empty)")
        {
            const FnKat::Asset::StringMap args{};

            WHEN("asset is published")
            {
                std::string inFlightAssetId;
                plugin->createAssetAndPath(
                    nullptr, "fcurve file", assetFields, args, true, inFlightAssetId);
                FnKat::Asset::StringMap inFlightAssetFields;
                plugin->getAssetFields(inFlightAssetId, false, inFlightAssetFields);
                std::string newAssetId;
                plugin->postCreateAsset(
                    nullptr, "fcurve file", inFlightAssetFields, args, newAssetId);

                THEN("entity has been registered with expected traits")
                {
                    FnKat::Asset::StringMap actual;
                    plugin->getAssetAttributes(newAssetId, "", actual);

                    const FnKat::Asset::StringMap expected = {
                        {"openassetio-mediacreation:usage,Entity", ""},
                        {"openassetio-mediacreation:application,Work", ""},
                        {"openassetio-mediacreation:lifecycle,Version", ""},
                        {"openassetio-mediacreation:lifecycle,Version,specifiedTag", "2"},
                        {"openassetio-mediacreation:lifecycle,Version,stableTag", "2"},
                        {"openassetio-mediacreation:content,LocatableContent", ""},
                        {"openassetio-mediacreation:content,LocatableContent,location",
                         "file:///some/staging/area/cat.fcurve"},
                        {"openassetio-mediacreation:content,LocatableContent,mimeType",
                         "application/vnd.foundry.katana.fcurve+xml"}};

                    CHECK(actual == expected);
                }
            }
        }
    }
}
// NOLINTEND(*-chained-comparison,*-function-cognitive-complexity,*-container-size-empty)
