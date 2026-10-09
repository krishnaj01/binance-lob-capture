#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "io/CsvReader.hpp"
#include "io/CsvWriter.hpp"

namespace blc {
namespace {

namespace fs = std::filesystem;

class CsvTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = (fs::path(::testing::TempDir()) / (std::string("csv_") + info->name() + ".csv"))
                    .string();
    }
    void TearDown() override { fs::remove(path_); }

    std::string slurp() const {
        std::ifstream in(path_, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    void write_raw(const std::string& content) const {
        std::ofstream(path_, std::ios::binary) << content;
    }
    std::vector<std::vector<std::string>> read_all() const {
        CsvReader reader(path_);
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string_view> fields;
        while (reader.next(fields)) rows.emplace_back(fields.begin(), fields.end());
        return rows;
    }

    std::string path_;
};

TEST_F(CsvTest, WriterFormatsAndEscapes) {
    {
        CsvWriter w(path_);
        w.field("plain");
        w.field(std::int64_t{-42});
        w.field(std::uint64_t{18'446'744'073'709'551'615ULL});
        w.field('B');
        w.field("a,b");
        w.field(R"({"p":"1.5"})");
        w.field("line\nbreak");
        w.field("");
        w.end_row();
        EXPECT_EQ(w.rows_written(), 1u);
    }
    EXPECT_EQ(slurp(),
              "plain,-42,18446744073709551615,B,\"a,b\",\"{\"\"p\"\":\"\"1.5\"\"}\","
              "\"line\nbreak\",\n");
}

TEST_F(CsvTest, RoundTrip) {
    const std::vector<std::vector<std::string>> rows = {
        {"recv_tsec", "payload_json"},
        {"1736899200", R"({"e":"depthUpdate","b":[["67012.34","0.5"]],"a":[]})"},
        {"", "quote \" inside", "comma, inside", "multi\nline\r\nfield"},
        {"single"},
    };
    {
        CsvWriter w(path_, /*flush_threshold=*/16);  // force many intermediate flushes
        for (const auto& row : rows) {
            for (const auto& f : row) w.field(f);
            w.end_row();
        }
    }
    EXPECT_EQ(read_all(), rows);
}

TEST_F(CsvTest, ReaderHandlesCrlfMissingFinalNewlineAndBlankLines) {
    write_raw("a,b\r\n\r\n\"x\"\"y\",\n\nlast,row");
    const std::vector<std::vector<std::string>> expected = {
        {"a", "b"}, {"x\"y", ""}, {"last", "row"}};
    EXPECT_EQ(read_all(), expected);
}

TEST_F(CsvTest, ReaderReportsRecordLine) {
    write_raw("h\n\"multi\nline\"\nnext\n");
    CsvReader reader(path_);
    std::vector<std::string_view> f;
    ASSERT_TRUE(reader.next(f));
    EXPECT_EQ(reader.record_line(), 1u);
    ASSERT_TRUE(reader.next(f));
    EXPECT_EQ(reader.record_line(), 2u);
    ASSERT_TRUE(reader.next(f));
    EXPECT_EQ(reader.record_line(), 4u);
    EXPECT_FALSE(reader.next(f));
}

TEST_F(CsvTest, ReaderRejectsMalformedInput) {
    for (const char* bad : {"\"unterminated\n", "ab\"c\n", "\"x\"y\n", "a\rb\n"}) {
        write_raw(bad);
        CsvReader reader(path_);
        std::vector<std::string_view> f;
        EXPECT_THROW(reader.next(f), CsvParseError) << "input: " << bad;
    }
}

TEST_F(CsvTest, OpenFailuresThrow) {
    EXPECT_THROW(CsvReader("/nonexistent/dir/file.csv"), std::runtime_error);
    EXPECT_THROW(CsvWriter("/nonexistent/dir/file.csv"), std::runtime_error);
}

}  // namespace
}  // namespace blc
