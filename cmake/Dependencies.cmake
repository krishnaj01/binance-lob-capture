# Third-party dependencies.
#  - System: Boost (Beast/Asio are header-only; program_options is compiled), OpenSSL.
#  - Fetched (pinned tags): simdjson, GoogleTest. Their include directories are marked
#    SYSTEM so their headers do not trigger our strict warning set. (Done by hand rather
#    than with FetchContent's SYSTEM keyword so CMake 3.22 / Ubuntu 22.04 works.)

include(FetchContent)

function(blc_mark_system_includes)
    foreach(target IN LISTS ARGN)
        get_target_property(dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
        if(dirs)
            set_target_properties(${target} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dirs}")
        endif()
    endforeach()
endfunction()

find_package(Threads REQUIRED)
find_package(OpenSSL 3 REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS program_options)

FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.10.1
    GIT_SHALLOW    TRUE)
set(SIMDJSON_DEVELOPER_MODE OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(simdjson)
blc_mark_system_includes(simdjson)

if(BLC_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE)
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(googletest)
    blc_mark_system_includes(gtest gtest_main)
endif()
