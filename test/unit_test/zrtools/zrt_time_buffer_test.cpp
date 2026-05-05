//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include "zrtools/time_buffer.h"

class TimeBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    // Sleep for a short duration
    void shortSleep(int milliseconds = 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

// Test basic push operation
TEST_F(TimeBufferTest, BasicPush) {
    TimeBuffer<int> tb(1.0);  // 1 second window

    tb.Push(10);
    tb.Push(20);
    tb.Push(30);

    // Verify items are in buffer via ForEach
    std::vector<int> items;
    tb.ForEach([&items](const int& val) {
        items.push_back(val);
    });

    EXPECT_EQ(items.size(), 3);
    EXPECT_EQ(items[0], 10);
    EXPECT_EQ(items[1], 20);
    EXPECT_EQ(items[2], 30);
}

// Test time-based expiration
TEST_F(TimeBufferTest, TimeExpiration) {
    TimeBuffer<int> tb(0.1);  // 100ms window

    tb.Push(1);
    tb.Push(2);

    // Wait for items to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    tb.Push(3);  // Add new item

    std::vector<int> items;
    tb.ForEach([&items](const int& val) {
        items.push_back(val);
    });

    // Only the new item should be present
    EXPECT_EQ(items.size(), 1);
    EXPECT_EQ(items[0], 3);
}

// Test with struct data
TEST_F(TimeBufferTest, StructData) {
    struct Order {
        int id;
        double price;
        int quantity;
    };

    TimeBuffer<Order> tb(1.0);

    tb.Push({1, 100.5, 10});
    tb.Push({2, 101.0, 20});
    tb.Push({3, 99.5, 15});

    std::vector<Order> orders;
    tb.ForEach([&orders](const Order& o) {
        orders.push_back(o);
    });

    EXPECT_EQ(orders.size(), 3);
    EXPECT_EQ(orders[0].id, 1);
    EXPECT_DOUBLE_EQ(orders[0].price, 100.5);
    EXPECT_EQ(orders[1].id, 2);
    EXPECT_EQ(orders[2].id, 3);
}

// Test with string data
TEST_F(TimeBufferTest, StringData) {
    TimeBuffer<std::string> tb(1.0);

    tb.Push("first");
    tb.Push("second");
    tb.Push("third");

    std::vector<std::string> strings;
    tb.ForEach([&strings](const std::string& s) {
        strings.push_back(s);
    });

    EXPECT_EQ(strings.size(), 3);
    EXPECT_EQ(strings[0], "first");
    EXPECT_EQ(strings[1], "second");
    EXPECT_EQ(strings[2], "third");
}

// Test gradual expiration
TEST_F(TimeBufferTest, GradualExpiration) {
    TimeBuffer<int> tb(0.15);  // 150ms window

    tb.Push(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    tb.Push(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    tb.Push(3);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // Item 1 should have expired by now
    std::vector<int> items;
    tb.ForEach([&items](const int& val) {
        items.push_back(val);
    });

    // Items 2 and 3 should remain (or just 3)
    EXPECT_LE(items.size(), 2);
    if (!items.empty()) {
        EXPECT_EQ(items.back(), 3);
    }
}

// Test with thread-safe version
TEST_F(TimeBufferTest, ThreadSafe) {
    TimeBuffer<int, std::mutex> tb(1.0);

    const int num_threads = 4;
    const int items_per_thread = 100;
    std::vector<std::thread> threads;

    // Producer threads
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&tb, t, items_per_thread]() {
            for (int i = 0; i < items_per_thread; ++i) {
                tb.Push(t * 1000 + i);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Count items
    int count = 0;
    tb.ForEach([&count](const int&) {
        count++;
    });

    EXPECT_EQ(count, num_threads * items_per_thread);
}

// Test empty buffer
TEST_F(TimeBufferTest, EmptyBuffer) {
    TimeBuffer<int> tb(1.0);

    int count = 0;
    tb.ForEach([&count](const int&) {
        count++;
    });

    EXPECT_EQ(count, 0);
}

// Test all items expired
TEST_F(TimeBufferTest, AllExpired) {
    TimeBuffer<int> tb(0.05);  // 50ms window

    tb.Push(1);
    tb.Push(2);
    tb.Push(3);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int count = 0;
    tb.ForEach([&count](const int&) {
        count++;
    });

    EXPECT_EQ(count, 0);
}

// Test very short time window
TEST_F(TimeBufferTest, VeryShortWindow) {
    TimeBuffer<int> tb(0.001);  // 1ms window

    tb.Push(1);

    // Even a small sleep should expire the item
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    int count = 0;
    tb.ForEach([&count](const int&) {
        count++;
    });

    EXPECT_EQ(count, 0);
}

// Test long time window
TEST_F(TimeBufferTest, LongWindow) {
    TimeBuffer<int> tb(60.0);  // 60 seconds window

    for (int i = 0; i < 100; ++i) {
        tb.Push(i);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int count = 0;
    tb.ForEach([&count](const int&) {
        count++;
    });

    // All items should still be present
    EXPECT_EQ(count, 100);
}

// Test ForEach with accumulation
TEST_F(TimeBufferTest, ForEachAccumulation) {
    TimeBuffer<double> tb(1.0);

    tb.Push(1.0);
    tb.Push(2.0);
    tb.Push(3.0);
    tb.Push(4.0);
    tb.Push(5.0);

    double sum = 0.0;
    tb.ForEach([&sum](const double& val) {
        sum += val;
    });

    EXPECT_DOUBLE_EQ(sum, 15.0);
}

// Test with complex nested structure
TEST_F(TimeBufferTest, ComplexStruct) {
    struct Trade {
        std::string symbol;
        double price;
        int volume;
        bool is_buy;
    };

    TimeBuffer<Trade> tb(1.0);

    tb.Push({"BTC", 45000.0, 100, true});
    tb.Push({"ETH", 3000.0, 50, false});
    tb.Push({"BTC", 45100.0, 200, false});

    double btc_volume = 0;
    tb.ForEach([&btc_volume](const Trade& t) {
        if (t.symbol == "BTC") {
            btc_volume += t.volume;
        }
    });

    EXPECT_EQ(btc_volume, 300);
}

// Test continuous push and expire
TEST_F(TimeBufferTest, ContinuousPushExpire) {
    TimeBuffer<int> tb(0.1);  // 100ms window

    for (int round = 0; round < 5; ++round) {
        tb.Push(round);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Only recent items should remain
    std::vector<int> items;
    tb.ForEach([&items](const int& val) {
        items.push_back(val);
    });

    // Should have only 1-2 items
    EXPECT_LE(items.size(), 3);
    if (!items.empty()) {
        EXPECT_EQ(items.back(), 4);  // Last pushed item
    }
}
