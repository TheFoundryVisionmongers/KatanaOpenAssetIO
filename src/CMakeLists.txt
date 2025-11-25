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
    Python::Module
)

set_target_properties(KatanaOpenAssetIOPlugin
    PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Libs"
)

# Custom traits --------------------------------------------------------

# Provide discovered Python distribution as a hint where to look for
# TraitGen, in case its installed there.
cmake_path(GET Python_EXECUTABLE PARENT_PATH Python_BIN_DIR)
if (WIN32)
    set(Python_BIN_DIR ${Python_BIN_DIR}/Scripts)
endif ()

# Find the TraitGen executable.
find_program(TRAITGEN NAME openassetio-traitgen HINTS ${Python_BIN_DIR})

if (NOT TRAITGEN)
    set(
        _error_message
        "openassetio-traitgen is required for generating"
        " Katana-specific traits but was not found. Install from PyPI"
        " using: pip install openassetio-traitgen"
    )
    message(FATAL_ERROR ${_error_message})
endif ()

# Expected output path of the generated C++ headers.
set(_trait_dir ${CMAKE_CURRENT_BINARY_DIR}/traits/katana_openassetio)

# Add a command (and target) to generate C++ headers from traits.yml.
add_custom_command(
    OUTPUT ${_trait_dir}
    COMMAND ${TRAITGEN} ${CMAKE_CURRENT_SOURCE_DIR}/traits.yml -g cpp
    -o ${CMAKE_CURRENT_BINARY_DIR}/traits
    COMMAND ${TRAITGEN} ${CMAKE_CURRENT_SOURCE_DIR}/traits.yml -g python
    -o ${CMAKE_CURRENT_BINARY_DIR}/traits
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/traits.yml
    COMMENT "Generating OpenAssetIO trait headers"
)
add_custom_target(KatanaOpenAssetIOTraits DEPENDS ${_trait_dir})

# Add dependencies on headers to main plugin target.
target_include_directories(KatanaOpenAssetIOPlugin SYSTEM PRIVATE ${_trait_dir}/include)
add_dependencies(KatanaOpenAssetIOPlugin KatanaOpenAssetIOTraits)


# Install artifacts-----------------------------------------------------

install(TARGETS KatanaOpenAssetIOPlugin LIBRARY
    COMPONENT Plugin
    DESTINATION Libs)

# Distribute traits.yml as a reference for asset manager integrators.
install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/traits.yml
    COMPONENT Plugin
    DESTINATION .
)

# Distribute generated traits.
install(
    DIRECTORY ${_trait_dir}
    COMPONENT Plugin
    DESTINATION site-packages
)

if (KATANAOPENASSETIO_ENABLE_UI_DELEGATE)
    install(
        FILES OpenAssetIOWidgetDelegate.py
        COMPONENT Plugin
        DESTINATION UIPlugins
    )
endif ()

if (KATANAOPENASSETIO_ENABLE_PATCH_RENDERNODEINFO)
    install(
        FILES KatanaOpenAssetIOPatches.py
        COMPONENT Plugin
        DESTINATION Plugins
    )
endif ()