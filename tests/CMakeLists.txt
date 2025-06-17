# KatanaOpenAssetIO
# Copyright (c) 2025 The Foundry Visionmongers Ltd
# SPDX-License-Identifier: Apache-2.0

# C++ test dependencies ------------------------------------------------

find_package(Catch2 REQUIRED)
find_package(Python REQUIRED COMPONENTS Interpreter Development.Embed)
find_package(pybind11 REQUIRED)

# Test target executable -----------------------------------------------

add_executable(KatanaOpenAssetIOTest main.cpp OpenAssetIOPluginTest.cpp)

target_link_libraries(
    KatanaOpenAssetIOTest
    PRIVATE
    foundry.katana.FnAsset
    foundry.katana.FnPluginManager
    Catch2::Catch2

    pybind11::embed
    ${CMAKE_DL_LIBS}
)
add_dependencies(KatanaOpenAssetIOTest KatanaOpenAssetIOPlugin)

# Navigate from Katana CMake config to libFnGeolib3.so.
cmake_path(GET Katana_DIR PARENT_PATH _geolib3_lib_path)
cmake_path(GET _geolib3_lib_path PARENT_PATH _geolib3_lib_path)
cmake_path(APPEND _geolib3_lib_path bin)
cmake_path(APPEND _geolib3_lib_path libFnGeolib3.so)

target_link_libraries(
    KatanaOpenAssetIOTest
    PRIVATE
    ${_geolib3_lib_path}
    foundry.katana.FnAssetPlugin
)

target_compile_definitions(
    KatanaOpenAssetIOTest
    PRIVATE
    # For appending to Geolib plugin search path.
    PLUGIN_DIR="$<TARGET_FILE_DIR:KatanaOpenAssetIOPlugin>"
    # For dynamically loading BAL JSON libraries for each test.
    BAL_DB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

# Test environment -----------------------------------------------------

# We're missing system packages for the Python interpreter, so use the
# source interpreter's prefix.
execute_process(
    COMMAND ${Python_EXECUTABLE} -c "import sys; sys.stdout.write(sys.base_prefix)"
    OUTPUT_VARIABLE Python_PREFIX
)
list(APPEND _envvars PYTHONHOME=set:${Python_PREFIX})
# OpenAssetIO_Python_SITELIB is conveniently defined via OpenAssetIO's
# CMake configuration file (OpenAssetIOConfig.cmake).
list(APPEND _envvars PYTHONPATH=path_list_append:${OpenAssetIO_Python_SITELIB})
# Add MediaCreation and BAL, installed by test fixture below.
list(APPEND _envvars PYTHONPATH=path_list_append:${CMAKE_CURRENT_BINARY_DIR}/site-packages)
# Set OpenAssetIO to use the test's config file.
list(
    APPEND
    _envvars
    OPENASSETIO_DEFAULT_CONFIG=set:${CMAKE_CURRENT_SOURCE_DIR}/resources/openassetio_config.toml
)


# Python test dependencies ---------------------------------------------

add_test(
    NAME
    KatanaOpenAssetIOTest.dependencies
    COMMAND
    ${Python_EXECUTABLE} -m pip install -r ${CMAKE_CURRENT_SOURCE_DIR}/resources/requirements.txt
    -t ${CMAKE_CURRENT_BINARY_DIR}/site-packages --no-deps
)
set_tests_properties(
    KatanaOpenAssetIOTest.dependencies
    PROPERTIES
    FIXTURES_SETUP KatanaOpenAssetIOTest.dependencies
)


# Test target ----------------------------------------------------------

add_test(NAME KatanaOpenAssetIOTest COMMAND KatanaOpenAssetIOTest)
set_tests_properties(
    KatanaOpenAssetIOTest
    PROPERTIES
    ENVIRONMENT_MODIFICATION "${_envvars}"
    FIXTURES_REQUIRED KatanaOpenAssetIOTest.dependencies
)