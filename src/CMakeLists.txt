# KatanaOpenAssetIO
# Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
# SPDX-License-Identifier: Apache-2.0

configure_file(config.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/include/config.hpp)

add_library(KatanaOpenAssetIOPlugin MODULE
    OpenAssetIOPlugin.cpp
    utilities.cpp
    PublishStrategies.cpp
)

katanaopenassetio_platform_target_properties(KatanaOpenAssetIOPlugin)

set_target_properties(KatanaOpenAssetIOPlugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET "hidden"
)

target_include_directories(KatanaOpenAssetIOPlugin
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

target_link_libraries(KatanaOpenAssetIOPlugin
    PUBLIC
    OpenAssetIO::openassetio-core
    OpenAssetIO::openassetio-python-bridge
    OpenAssetIO-MediaCreation::openassetio-mediacreation

    PRIVATE
    foundry.katana.FnConfig
    foundry.katana.FnAsset
    foundry.katana.FnAssetPlugin
    foundry.katana.FnAttribute
    foundry.katana.FnLogging
    foundry.katana.pystring
    Python::Python
)

set_target_properties(KatanaOpenAssetIOPlugin
    PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Libs"
)

install(TARGETS KatanaOpenAssetIOPlugin LIBRARY
    COMPONENT Plugin
    DESTINATION Libs)

if (KATANAOPENASSETIO_ENABLE_UI_DELEGATE)
    install(
        FILES OpenAssetIOWidgetDelegate.py
        COMPONENT Plugin
        DESTINATION UIPlugins
    )
endif ()