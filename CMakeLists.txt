# KatanaOpenAssetIO
# Copyright (c) 2024 The Foundry Visionmongers Ltd
# SPDX-License-Identifier: Apache-2.0

# Policies --------------------------------------------------------------------
cmake_minimum_required(VERSION 3.26.4)

# Project ---------------------------------------------------------------------
project(KatanaOpenAssetIO
        VERSION 1.0.0
        DESCRIPTION "AssetAPI adapter to load OpenAssetIO plugins in Katana"
        LANGUAGES C CXX)

option(
    KATANAOPENASSETIO_ENABLE_EXTRA_WARNINGS
    "Enable a large set of compiler warnings for project targets"
    ON
)
option(
    KATANAOPENASSETIO_ENABLE_SECURITY_HARDENING
    "Enable security hardening features for project targets"
    ON
)
option(
    KATANAOPENASSETIO_ENABLE_UI_DELEGATE
    "Enable 'Asset' browser - a simple text box alternative to the file browser"
    OFF
)
option(
    KATANAOPENASSETIO_ENABLE_TESTS
    "Enable unit tests. Requires Katana license and Python development install."
    OFF
)

# Global Settings -------------------------------------------------------------
include(cmake/platform.cmake)
include(cmake/thirdparty.cmake)

# Source ----------------------------------------------------------------------
add_subdirectory(src)

# Packaging -------------------------------------------------------------------
include(cmake/installers.cmake)

# Tests -------------------------------------------------------------------
if (KATANAOPENASSETIO_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
