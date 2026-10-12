cmake_minimum_required(VERSION 3.20)

project(PrismCore LANGUAGES CXX)

option(PRISMCORE_BUILD_TESTS "Build PrismCore tests" OFF)

add_subdirectory(src/core)

if(PRISMCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(extern/Catch2 EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()