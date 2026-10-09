add_executable(unit_tests
    test_sanity.cpp
    common/test_decimal.cpp
    common/test_clock.cpp
    common/test_spsc_queue.cpp
    io/test_csv.cpp)
target_link_libraries(unit_tests PRIVATE blc_core blc_options GTest::gtest_main simdjson::simdjson)

include(GoogleTest)
gtest_discover_tests(unit_tests)
