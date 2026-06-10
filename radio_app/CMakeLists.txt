cmake_minimum_required(VERSION 3.14)
project(RadioPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

logos_module(
    NAME radio
    REP_FILE src/radio.rep
    SOURCES
        src/radio_interface.h
        src/radio_plugin.h
        src/radio_plugin.cpp
)
