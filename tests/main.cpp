// KatanaOpenAssetIO
// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 The Foundry Visionmongers Ltd
#include <exception>
#include <iostream>

#include <catch2/catch_session.hpp>
#include <pybind11/embed.h>

#include <FnAttribute/FnAttributeBase.h>
#include <FnPluginManager/FnPluginManager.h>
#include <FnPluginManager/suite/FnPluginManagerSuite.h>

#ifndef PLUGIN_DIR
#error PLUGIN_DIR must be the location of the KatanaOpenAssetIO library.
#endif

extern "C"
{
    // NOLINTNEXTLINE(readability-identifier-naming)
    extern int FnGeolib3Initialize(void*);

    // NOLINTNEXTLINE(readability-identifier-naming)
    extern FnPluginManagerHostSuite_v1* FnGeolib3GetPluginManager();
}

int main(int argc, char* argv[])
{
    try
    {
        // Start a Python interpreter. The tests rely on the BAL
        // mock/fake asset manager, which is pure Python.
        const pybind11::scoped_interpreter pythonInterpreter{};

        // Load and initialise the Katana Geolib library.
        if (FnGeolib3Initialize(nullptr) != 0)
        {
            throw std::runtime_error{
                "Failed to initialise Geolib3. Do you have a Katana license configured?"};
        }

        // Get the Geolib plugin manager.
        auto* pluginManagerSuite = FnGeolib3GetPluginManager();
        FnKat::PluginManager::setHost(pluginManagerSuite->getHost());

        // Find and load KatanaOpenAssetIO.
        FnKat::PluginManager::addSearchPath({PLUGIN_DIR});
        FnKat::PluginManager::findPlugins();

        // Enable other required Katana API.
        FnKat::Attribute::setHost(FnKat::PluginManager::getHost());

        // Execute discovered tests.
        return Catch::Session().run(argc, argv);
    }
    catch (const std::exception& exc)
    {
        std::cerr << "Fatal error running tests: " << exc.what();
    }
    catch (...)
    {
        std::cerr << "Fatal unknown error running tests";
    }
    return 1;
}