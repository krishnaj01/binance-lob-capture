#include "common/SpscQueue.hpp"

#include <gtest/gtest.h>

#include <string>
#include <thread>

namespace blc {
namespace {

TEST(SpscQueue, CapacityRoundsUpToPowerOfTwo) {
    EXPECT_EQ(SpscQueue<int>(0).capacity(), 2u);
    EXPECT_EQ(SpscQueue<int>(3).capacity(), 4u);
    EXPECT_EQ(SpscQueue<int>(1024).capacity(), 1024u);
}

TEST(SpscQueue, FifoAndFullEmpty) {
    SpscQueue<int> q(4);
    int v = 0;
    EXPECT_FALSE(q.try_pop(v));
    for (int i = 1; i <= 4; ++i) {
        int x = i;
        EXPECT_TRUE(q.try_push(x));
    }
    int extra = 99;
    EXPECT_FALSE(q.try_push(extra));
    EXPECT_EQ(extra, 99);  // untouched on failure
    for (int i = 1; i <= 4; ++i) {
        ASSERT_TRUE(q.try_pop(v));
        EXPECT_EQ(v, i);
    }
    EXPECT_FALSE(q.try_pop(v));
}

TEST(SpscQueue, SwapRecyclesBuffers) {
    SpscQueue<std::string> q(2);
    std::string consumer_buf;
    consumer_buf.reserve(4096);
    const char* consumer_storage = consumer_buf.data();

    std::string frame(1000, 'a');
    ASSERT_TRUE(q.try_push(frame));
    ASSERT_TRUE(q.try_pop(consumer_buf));  // consumer's big buffer is parked in the slot
    EXPECT_EQ(consumer_buf, std::string(1000, 'a'));

    // Two pushes later the producer is handed the consumer's old buffer back.
    std::string a = "x";
    ASSERT_TRUE(q.try_push(a));
    std::string b = "y";
    ASSERT_TRUE(q.try_push(b));
    EXPECT_TRUE(a.data() == consumer_storage || b.data() == consumer_storage);
}

TEST(SpscQueue, TwoThreadsPreserveOrder) {
    constexpr std::uint64_t kCount = 1'000'000;
    SpscQueue<std::uint64_t> q(1024);

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount; ++i) {
            std::uint64_t v = i;
            while (!q.try_push(v)) std::this_thread::yield();
        }
    });

    std::uint64_t expected = 0;
    std::uint64_t v = 0;
    while (expected < kCount) {
        if (q.try_pop(v)) {
            ASSERT_EQ(v, expected);
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    EXPECT_FALSE(q.try_pop(v));
}

}  // namespace
}  // namespace blc
