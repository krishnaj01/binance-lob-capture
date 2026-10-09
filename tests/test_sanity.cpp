#include <gtest/gtest.h>
#include <simdjson.h>

// Placeholder: proves the test harness and simdjson link correctly.
// Replaced by real suites as components land.
TEST(Sanity, SimdjsonParsesObject) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(std::string_view(R"({"u":42})"));
    auto doc = parser.iterate(json);
    EXPECT_EQ(uint64_t(doc["u"]), 42u);
}
