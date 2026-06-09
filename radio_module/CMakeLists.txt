cmake_minimum_required(VERSION 3.14)
project(RadioModulePlugin LANGUAGES CXX)

# Include the Logos Module CMake helper (provided by logos-module-builder)
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME radio_module
    SOURCES
        src/radio_interface.h
        src/radio_plugin.h
        src/radio_plugin.cpp
    FIND_PACKAGES
        Qt6
)

# Tier-1 offline unit tests (Qt::Test). Built standalone; not part of the plugin.
# Enable with -DRADIO_BUILD_TESTS=ON during dev.
option(RADIO_BUILD_TESTS "Build radio_module headless unit tests" OFF)
if(RADIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
