add_executable(unit_tests
    test_sanity.cpp)
target_link_libraries(unit_tests PRIVATE blc_options GTest::gtest_main simdjson::simdjson)

include(GoogleTest)
gtest_discover_tests(unit_tests)
