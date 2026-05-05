//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include "zrtools/ultraq.h"

class UltraQTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

// Test basic push and pull operations
TEST_F(UltraQTest, BasicPushPull) {
    zrt::UltraQ_ST q;

    struct TestData {
        int value;
        char name[32];
    };

    TestData input{42, "test"};
    int64_t pushed = q.push(&input, sizeof(input));
    EXPECT_EQ(pushed, sizeof(input));
    EXPECT_FALSE(q.is_empty());

    TestData output{};
    int64_t pulled = q.pull(&output);
    EXPECT_EQ(pulled, sizeof(input));
    EXPECT_EQ(output.value, 42);
    EXPECT_STREQ(output.name, "test");
    EXPECT_TRUE(q.is_empty());
}

// Test multiple push and pull
TEST_F(UltraQTest, MultiplePushPull) {
    zrt::UltraQ_ST q;

    // Push multiple items
    for (int i = 0; i < 100; ++i) {
        int64_t pushed = q.push(&i, sizeof(i));
        EXPECT_EQ(pushed, sizeof(i));
    }

    EXPECT_FALSE(q.is_empty());
    EXPECT_EQ(q.get_size(), 100 * sizeof(int));

    // Pull all items
    for (int i = 0; i < 100; ++i) {
        int value = -1;
        int64_t pulled = q.pull(&value);
        EXPECT_EQ(pulled, sizeof(int));
        EXPECT_EQ(value, i);
    }

    EXPECT_TRUE(q.is_empty());
}

// Test empty queue
TEST_F(UltraQTest, EmptyQueue) {
    zrt::UltraQ_ST q;

    EXPECT_TRUE(q.is_empty());
    EXPECT_EQ(q.get_size(), 0);

    int value = -1;
    int64_t pulled = q.pull(&value);
    EXPECT_EQ(pulled, 0);
    EXPECT_EQ(value, -1);  // Value should remain unchanged
}

// Test queue size tracking
TEST_F(UltraQTest, SizeTracking) {
    zrt::UltraQ_ST q;

    EXPECT_EQ(q.get_size(), 0);

    int data1 = 100;
    q.push(&data1, sizeof(data1));
    EXPECT_EQ(q.get_size(), sizeof(int));

    double data2 = 3.14;
    q.push(&data2, sizeof(data2));
    EXPECT_EQ(q.get_size(), sizeof(int) + sizeof(double));

    int temp;
    q.pull(&temp);
    EXPECT_EQ(q.get_size(), sizeof(double));

    double temp2;
    q.pull(&temp2);
    EXPECT_EQ(q.get_size(), 0);
}

// Test with string data
TEST_F(UltraQTest, StringData) {
    zrt::UltraQ_ST q;

    struct Message {
        char content[256];
    };

    Message msg1;
    strcpy(msg1.content, "Hello, World!");
    q.push(&msg1, sizeof(msg1));

    Message msg2;
    strcpy(msg2.content, "Second message");
    q.push(&msg2, sizeof(msg2));

    Message output1{}, output2{};
    q.pull(&output1);
    q.pull(&output2);

    EXPECT_STREQ(output1.content, "Hello, World!");
    EXPECT_STREQ(output2.content, "Second message");
}

// Test block counting
TEST_F(UltraQTest, BlockCounting) {
    zrt::UltraQ_ST q;

    EXPECT_EQ(q.cnt_block_used(), 1);  // Initial block
    EXPECT_EQ(q.cnt_block_total(), 1);

    // Fill one block
    char data[ULTRAQ_BLOCK_BUFFER_SIZE / 2];
    memset(data, 'A', sizeof(data));
    q.push(data, sizeof(data));

    EXPECT_EQ(q.cnt_block_used(), 1);
}

// Test max block limit
TEST_F(UltraQTest, MaxBlockLimit) {
    zrt::UltraQ_ST q(2);  // Limit to 2 blocks

    // Fill first block
    char data1[ULTRAQ_BLOCK_BUFFER_SIZE - 100];
    memset(data1, 'A', sizeof(data1));
    int64_t pushed1 = q.push(data1, sizeof(data1));
    EXPECT_GT(pushed1, 0);

    // Fill second block
    char data2[ULTRAQ_BLOCK_BUFFER_SIZE - 100];
    memset(data2, 'B', sizeof(data2));
    int64_t pushed2 = q.push(data2, sizeof(data2));
    EXPECT_GT(pushed2, 0);

    // Third push should fail (queue is full)
    char data3[1000];
    memset(data3, 'C', sizeof(data3));
    int64_t pushed3 = q.push(data3, sizeof(data3));
    EXPECT_EQ(pushed3, 0);
}

// Test data too large for single push
TEST_F(UltraQTest, DataTooLarge) {
    zrt::UltraQ_ST q;

    // Try to push data larger than block buffer
    char large_data[ULTRAQ_BLOCK_BUFFER_SIZE + 1000];
    memset(large_data, 'X', sizeof(large_data));

    int64_t pushed = q.push(large_data, sizeof(large_data));
    EXPECT_EQ(pushed, -1);  // Should fail
}

// Test is_full function
TEST_F(UltraQTest, IsFull) {
    // Unlimited queue
    {
        zrt::UltraQ_ST q;
        EXPECT_FALSE(q.is_full(1000));
    }

    // Limited queue
    {
        zrt::UltraQ_ST q(1);  // Only 1 block

        // Initially not full
        EXPECT_FALSE(q.is_full(100));

        // Fill the block
        char data[ULTRAQ_BLOCK_BUFFER_SIZE - 200];
        memset(data, 'A', sizeof(data));
        q.push(data, sizeof(data));

        // Now should be full for large data
        EXPECT_TRUE(q.is_full(1000));
    }
}

// Test capacity
TEST_F(UltraQTest, Capacity) {
    zrt::UltraQ_ST q;

    size_t capacity = q.get_capacity();
    EXPECT_GT(capacity, 0);
    EXPECT_EQ(capacity, q.cnt_block_total() * ULTRAQ_BLOCK_BUFFER_SIZE);
}

// Test multi-threaded access with mutex
TEST_F(UltraQTest, MultiThreaded) {
    zrt::UltraQ_MT q;

    const int num_items = 1000;
    std::atomic<int> pushed_count{0};
    std::atomic<int> pulled_count{0};

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_items; ++i) {
            while (q.push(&i, sizeof(i)) <= 0) {
                std::this_thread::yield();
            }
            pushed_count++;
        }
    });

    // Consumer thread
    std::thread consumer([&]() {
        while (pulled_count < num_items) {
            int value;
            if (q.pull(&value) > 0) {
                pulled_count++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(pushed_count.load(), num_items);
    EXPECT_EQ(pulled_count.load(), num_items);
    EXPECT_TRUE(q.is_empty());
}

// Test struct alignment
TEST_F(UltraQTest, StructAlignment) {
    zrt::UltraQ_ST q;

    struct AlignedStruct {
        char a;
        double b;  // 8-byte alignment
        int c;
        char d;
    };

    AlignedStruct input{};
    input.a = 'X';
    input.b = 3.14159;
    input.c = 42;
    input.d = 'Y';

    q.push(&input, sizeof(input));

    AlignedStruct output{};
    q.pull(&output);

    EXPECT_EQ(output.a, 'X');
    EXPECT_DOUBLE_EQ(output.b, 3.14159);
    EXPECT_EQ(output.c, 42);
    EXPECT_EQ(output.d, 'Y');
}

// Test block reuse
TEST_F(UltraQTest, BlockReuse) {
    zrt::UltraQ_ST q;

    // Push and pull to create empty blocks
    int data = 42;
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 100; ++i) {
            q.push(&data, sizeof(data));
        }

        for (int i = 0; i < 100; ++i) {
            int temp;
            q.pull(&temp);
        }
    }

    // Queue should be empty but may have empty blocks
    EXPECT_TRUE(q.is_empty());
    size_t total_blocks = q.cnt_block_total();
    EXPECT_GE(total_blocks, 1);
}

// Test FIFO order
TEST_F(UltraQTest, FIFOOrder) {
    zrt::UltraQ_ST q;

    // Push items in order
    for (int i = 0; i < 50; ++i) {
        q.push(&i, sizeof(i));
    }

    // Pull and verify order
    for (int i = 0; i < 50; ++i) {
        int value = -1;
        q.pull(&value);
        EXPECT_EQ(value, i);
    }
}

// Test mixed size data
TEST_F(UltraQTest, MixedSizeData) {
    zrt::UltraQ_ST q;

    // Push different sized data
    int int_val = 123;
    double double_val = 4.56;
    char char_arr[10] = "hello";

    q.push(&int_val, sizeof(int_val));
    q.push(&double_val, sizeof(double_val));
    q.push(char_arr, sizeof(char_arr));

    // Pull in same order
    int out_int;
    double out_double;
    char out_char[10];

    EXPECT_EQ(q.pull(&out_int), sizeof(int));
    EXPECT_EQ(out_int, 123);

    EXPECT_EQ(q.pull(&out_double), sizeof(double));
    EXPECT_DOUBLE_EQ(out_double, 4.56);

    EXPECT_EQ(q.pull(out_char), sizeof(char_arr));
    EXPECT_STREQ(out_char, "hello");
}
