#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

if(NOT PAL_TRAIT_BUILD_HOST_TOOLS)
    return()
endif()

# BRAssetBundlerBatch - CLI Application
ly_add_target(
    NAME BRAssetBundlerBatch.Static STATIC
    NAMESPACE AZ
    FILES_CMAKE
        BRAssetBundlerBatch_files.cmake
        platform/${PAL_PLATFORM_NAME}/platform_${PAL_PLATFORM_NAME_LOWERCASE}_files.cmake
    INCLUDE_DIRECTORIES
        PRIVATE
            .
    BUILD_DEPENDENCIES
        PUBLIC
            AZ::AzToolsFramework
            ${additional_dependencies}
)

ly_add_target(
    NAME BRAssetBundlerBatch EXECUTABLE
    NAMESPACE AZ
    FILES_CMAKE
        BRAssetBundlerBatch_exe_files.cmake
    COMPILE_DEFINITIONS
        PRIVATE
            .
    INCLUDE_DIRECTORIES
        PRIVATE
            .
    BUILD_DEPENDENCIES
        PRIVATE
            AZ::BRAssetBundlerBatch.Static
)