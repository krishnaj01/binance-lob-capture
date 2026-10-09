cmake_minimum_required(VERSION 3.25)

project(binance_lob_capture
    VERSION 1.0.0
    DESCRIPTION "Binance WebSocket capture + local order book"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BLC_BUILD_TESTS "Build unit tests" ON)
option(BLC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(BLC_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Project-wide compile settings, applied to our targets only (not to fetched dependencies).
add_library(blc_options INTERFACE)
target_compile_options(blc_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
    $<$<BOOL:${BLC_WARNINGS_AS_ERRORS}>:-Werror>)
if(BLC_ENABLE_SANITIZERS)
    target_compile_options(blc_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(blc_options INTERFACE -fsanitize=address,undefined)
endif()

include(cmake/Dependencies.cmake)

add_executable(binance_capture src/main.cpp)
target_link_libraries(binance_capture PRIVATE blc_options)

if(BLC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
