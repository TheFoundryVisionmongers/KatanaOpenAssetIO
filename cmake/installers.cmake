# KatanaOpenAssetIO
# Copyright (c) 2024 The Foundry Visionmongers Ltd
# SPDX-License-Identifier: Apache-2.0

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR Foundry)
set(CPACK_PACKAGE_FILE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_DESCRIPTION ${PROJECT_DESCRIPTION})

set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

if (UNIX AND NOT APPLE)
    set(CPACK_GENERATOR STGZ)
elseif (WIN32)
    set(CPACK_GENERATOR NSIS)

    set(CPACK_NSIS_HELP_LINK "https://support.foundry.com")
    set(CPACK_NSIS_PACKAGE_NAME {PROJECT_NAME})
    set(CPACK_NSIS_DISPLAY_NAME {PROJECT_NAME})
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_CONTACT support@foundry.com)
    set(CPACK_NSIS_BRANDING_TEXT "Foundry")
endif()


include(CPack)