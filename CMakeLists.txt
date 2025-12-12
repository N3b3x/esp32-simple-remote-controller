# =============================================================================
# ESP32 Remote Controller - Project Root CMakeLists.txt
# =============================================================================
# Standalone remote controller application
# No dependencies on TMC driver library
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Shared Default Variables
# =============================================================================
set(DEFAULT_APP_TYPE "remote_controller" CACHE STRING "Default app type")
set(DEFAULT_BUILD_TYPE "Release" CACHE STRING "Default build type")
set(DEFAULT_SOURCE_FILE "main/main.cpp" CACHE STRING "Default source file")

# =============================================================================
# Build Parameter Validation
# =============================================================================
if(NOT DEFINED APP_TYPE)
    message(FATAL_ERROR 
        "APP_TYPE not defined. Please use build_app.sh to build this project.\n"
        "Example: ./scripts/build_app.sh ${DEFAULT_APP_TYPE} ${DEFAULT_BUILD_TYPE}\n"
        "Use './scripts/build_app.sh list' to see available app types."
    )
endif()

if(NOT DEFINED BUILD_TYPE)
    message(FATAL_ERROR 
        "BUILD_TYPE not defined. Please use build_app.sh to build this project.\n"
        "Example: ./scripts/build_app.sh ${DEFAULT_APP_TYPE} ${DEFAULT_BUILD_TYPE}\n"
        "Use './scripts/build_app.sh list' to see available app types."
    )
endif()

# =============================================================================
# Build Configuration and Status Messages
# =============================================================================
message(STATUS "Building app type: ${APP_TYPE}")
message(STATUS "Build type: ${BUILD_TYPE}")

# =============================================================================
# Global Variable Configuration
# =============================================================================
set(APP_TYPE "${APP_TYPE}" CACHE STRING "App type to build" FORCE)
set(BUILD_TYPE "${BUILD_TYPE}" CACHE STRING "Build type (Debug/Release)" FORCE)

# =============================================================================
# ESP-IDF Integration
# =============================================================================
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# =============================================================================
# Project Configuration
# =============================================================================
project(esp32_remote_controller_${APP_TYPE}_app)

