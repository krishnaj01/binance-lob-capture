# Third-party dependencies.
#  - System: Boost (Beast/Asio are header-only; program_options is compiled), OpenSSL.
#  - Fetched (pinned tags): simdjson, GoogleTest. Marked SYSTEM so their headers do not
#    trigger our strict warning set.

include(FetchContent)

find_package(Threads REQUIRED)
find_package(OpenSSL 3 REQUIRED)
find_package(Boost 1.74 REQUIRED COMPONENTS program_options)

FetchContent_Declare(simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.10.1
    GIT_SHALLOW    TRUE
    SYSTEM)
set(SIMDJSON_DEVELOPER_MODE OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(simdjson)

if(BLC_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
        SYSTEM)
    set(INSTALL_GTEST OFF CACHE INTERNAL "")
    set(BUILD_GMOCK OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(googletest)
endif()
