add_executable(unit_tests
    book/test_book_sync.cpp
    book/test_order_book.cpp
    common/test_clock.cpp
    common/test_decimal.cpp
    common/test_spsc_queue.cpp
    io/test_csv.cpp
    md/test_parser.cpp
    net/test_stream_url.cpp
    pipeline/test_processor.cpp
    replay/test_replay.cpp)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(unit_tests PRIVATE
    BLC_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(unit_tests PRIVATE blc_core blc_options GTest::gtest_main)

# `cmake --build build --target tests` (the command used in the assignment brief).
add_custom_target(tests DEPENDS unit_tests)

include(GoogleTest)
gtest_discover_tests(unit_tests)
