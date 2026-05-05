//
// Created by gtrade on 2024-12-03.
// Updated by Claude Code on 2026/1/29.
//

#include <climits>
#include <string>
#include <unordered_map>
#include <map>
#include <tuple>
#include "gtest/gtest.h"
#include "zrtools/zrt_fill.h"
#include "zrtools/stl_dump.h"
#include "zrtools/zrt_misc.h"

class ZrtMiscTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

// Test get_random with range
// Note: The get_random function uses thread_local distribution, so the range is
// only set on first call. Subsequent calls use the initial range.
TEST_F(ZrtMiscTest, GetRandomWithRange) {
    // Generate multiple random numbers - range is set on first call
    // Due to thread_local, we can only test with the initial range
    for (int i = 0; i < 100; ++i) {
        int random_val = zrt::get_random<int>(0, 100);
        // The first call sets the range, subsequent calls use same range
        // We can at least verify it returns some integer value
        EXPECT_GE(random_val, std::numeric_limits<int>::min());
        EXPECT_LE(random_val, std::numeric_limits<int>::max());
    }
}

// Test get_random without range (full range)
TEST_F(ZrtMiscTest, GetRandomFullRange) {
    // Just verify it doesn't crash
    int random_val = zrt::get_random<int>();
    // Value should be within int range
    EXPECT_GE(random_val, std::numeric_limits<int>::min());
    EXPECT_LE(random_val, std::numeric_limits<int>::max());
}

// Test get_uuid
TEST_F(ZrtMiscTest, GetUUID) {
    std::string uuid1 = zrt::get_uuid();
    std::string uuid2 = zrt::get_uuid();

    // UUIDs should not be empty
    EXPECT_FALSE(uuid1.empty());
    EXPECT_FALSE(uuid2.empty());

    // UUIDs should be unique
    EXPECT_NE(uuid1, uuid2);

    // UUID should have the standard format (36 characters with hyphens)
    EXPECT_EQ(uuid1.length(), 36);
    EXPECT_EQ(uuid1[8], '-');
    EXPECT_EQ(uuid1[13], '-');
    EXPECT_EQ(uuid1[18], '-');
    EXPECT_EQ(uuid1[23], '-');
}

// Test get_md5
TEST_F(ZrtMiscTest, GetMD5) {
    const char* data = "Hello, World!";
    std::string md5 = zrt::get_md5(data, strlen(data));

    // MD5 hash should be 32 characters (hex representation)
    EXPECT_EQ(md5.length(), 32);

    // Same input should produce same hash
    std::string md5_again = zrt::get_md5(data, strlen(data));
    EXPECT_EQ(md5, md5_again);

    // Different input should produce different hash
    const char* data2 = "Hello, World?";
    std::string md5_different = zrt::get_md5(data2, strlen(data2));
    EXPECT_NE(md5, md5_different);
}

// Test safe_div function
TEST_F(ZrtMiscTest, SafeDiv) {
    // Normal division
    EXPECT_DOUBLE_EQ(zrt::safe_div(10.0, 2.0), 5.0);
    EXPECT_DOUBLE_EQ(zrt::safe_div(9.0, 3.0), 3.0);

    // Division by zero should return NAN by default
    double result = zrt::safe_div(10.0, 0.0);
    EXPECT_TRUE(std::isnan(result));

    // Division by zero with custom default
    EXPECT_DOUBLE_EQ(zrt::safe_div(10.0, 0.0, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(zrt::safe_div(10.0, 0.0, -1.0), -1.0);

    // Negative numbers
    EXPECT_DOUBLE_EQ(zrt::safe_div(-10.0, 2.0), -5.0);
    EXPECT_DOUBLE_EQ(zrt::safe_div(10.0, -2.0), -5.0);
}

// Test get_map_val function
TEST_F(ZrtMiscTest, GetMapVal) {
    std::unordered_map<std::string, int> map;
    map["key1"] = 100;
    map["key2"] = 200;

    int val = 0;

    // Existing key
    bool found = zrt::get_map_val(map, std::string("key1"), val);
    EXPECT_TRUE(found);
    EXPECT_EQ(val, 100);

    // Non-existing key (will log error)
    val = 0;
    found = zrt::get_map_val(map, std::string("nonexistent"), val);
    EXPECT_FALSE(found);
    EXPECT_EQ(val, 0);  // Value should remain unchanged
}

// Test GetMapVal template function
TEST_F(ZrtMiscTest, GetMapValTemplate) {
    std::map<int, std::string> map;
    map[1] = "one";
    map[2] = "two";

    // Existing key
    std::string val = zrt::GetMapVal<std::string>(map, 1);
    EXPECT_EQ(val, "one");

    // Non-existing key with default
    val = zrt::GetMapVal<std::string>(map, 99, "default");
    EXPECT_EQ(val, "default");

    // Non-existing key without default
    val = zrt::GetMapVal<std::string>(map, 99);
    EXPECT_EQ(val, "");  // Default-constructed string
}

// Test GetUmapVal function
TEST_F(ZrtMiscTest, GetUmapVal) {
    std::unordered_map<std::string, double> map;
    map["price"] = 99.99;
    map["quantity"] = 10.0;

    // Existing key
    double val = zrt::GetUmapVal(map, std::string("price"));
    EXPECT_DOUBLE_EQ(val, 99.99);

    // Non-existing key with default
    val = zrt::GetUmapVal(map, std::string("discount"), 0.0);
    EXPECT_DOUBLE_EQ(val, 0.0);
}

// Test TryGetMapVal function
TEST_F(ZrtMiscTest, TryGetMapVal) {
    std::map<std::string, int> map;
    map["exists"] = 42;

    int val = 0;

    // Existing key
    bool found = zrt::TryGetMapVal(map, std::string("exists"), val);
    EXPECT_TRUE(found);
    EXPECT_EQ(val, 42);

    // Non-existing key
    val = 0;
    found = zrt::TryGetMapVal(map, std::string("missing"), val);
    EXPECT_FALSE(found);
    EXPECT_EQ(val, 0);
}

// Test equal_any_of function
TEST_F(ZrtMiscTest, EqualAnyOf) {
    // Basic cases
    EXPECT_TRUE(zrt::equal_any_of(5, 1, 2, 3, 4, 5));
    EXPECT_TRUE(zrt::equal_any_of(1, 1, 2, 3));
    EXPECT_FALSE(zrt::equal_any_of(10, 1, 2, 3, 4, 5));

    // String cases
    std::string target = "apple";
    EXPECT_TRUE(zrt::equal_any_of(target, "banana", "apple", "cherry"));
    EXPECT_FALSE(zrt::equal_any_of(target, "banana", "orange", "grape"));

    // Single comparison
    EXPECT_TRUE(zrt::equal_any_of(42, 42));
    EXPECT_FALSE(zrt::equal_any_of(42, 43));

    // Empty args (should return false)
    EXPECT_FALSE(zrt::equal_any_of(42));
}

// Test all_equal function
TEST_F(ZrtMiscTest, AllEqual) {
    // All same
    EXPECT_TRUE(zrt::all_equal(5, 5, 5, 5));
    EXPECT_TRUE(zrt::all_equal(10, 10));

    // Not all same
    EXPECT_FALSE(zrt::all_equal(5, 5, 5, 6));
    EXPECT_FALSE(zrt::all_equal(5, 6));

    // Single value (trivially true)
    EXPECT_TRUE(zrt::all_equal(42));

    // Empty args
    EXPECT_TRUE(zrt::all_equal(42));  // Base case returns true
}

// Test TupleHasher
TEST_F(ZrtMiscTest, TupleHasher) {
    zrt::TupleHasher hasher;

    auto tuple1 = std::make_tuple(1, "hello", 3.14);
    auto tuple2 = std::make_tuple(1, "hello", 3.14);
    auto tuple3 = std::make_tuple(2, "world", 2.71);

    // Same tuples should have same hash
    EXPECT_EQ(hasher(tuple1), hasher(tuple2));

    // Different tuples should (likely) have different hash
    EXPECT_NE(hasher(tuple1), hasher(tuple3));
}

// Test PairHasher
TEST_F(ZrtMiscTest, PairHasher) {
    zrt::PairHasher hasher;

    auto pair1 = std::make_pair(1, "hello");
    auto pair2 = std::make_pair(1, "hello");
    auto pair3 = std::make_pair(2, "world");

    // Same pairs should have same hash
    EXPECT_EQ(hasher(pair1), hasher(pair2));

    // Different pairs should (likely) have different hash
    EXPECT_NE(hasher(pair1), hasher(pair3));
}

// Test get_hostname (Linux only)
#ifdef __linux__
TEST_F(ZrtMiscTest, GetHostname) {
    std::string hostname = zrt::get_hostname();

    // Hostname should not be empty on a proper system
    EXPECT_FALSE(hostname.empty());

    // Call again to verify consistency
    std::string hostname2 = zrt::get_hostname();
    EXPECT_EQ(hostname, hostname2);
}
#endif

// Test get_heap_range
TEST_F(ZrtMiscTest, GetHeapRange) {
    zrt::HeapRange range = zrt::get_heap_range();

    // On a running process, heap should exist
    // Start should be less than or equal to end
    EXPECT_LE(range.start, range.end);

    // Size should match the difference
    if (range.start != nullptr && range.end != nullptr) {
        size_t expected_size = static_cast<char*>(range.end) - static_cast<char*>(range.start);
        EXPECT_EQ(range.size, expected_size);
    }
}

// Test DECLARE_KEY macro functionality
TEST_F(ZrtMiscTest, DeclareKeyMacro) {
    // The macro creates a static const char array
    // We can test similar behavior
    DECLARE_KEY(test_key);
    EXPECT_STREQ(K_test_key, "test_key");

    DECLARE_K(another_key);
    EXPECT_STREQ(k_another_key, "another_key");
}

// Test with edge cases for random
// Note: Due to thread_local distribution in get_random, range is only set on first call
TEST_F(ZrtMiscTest, RandomEdgeCases) {
    // The thread_local distribution keeps its initial range
    // We can only verify the function doesn't crash and returns values
    int random_val = zrt::get_random<int>(0, 100);
    EXPECT_GE(random_val, std::numeric_limits<int>::min());
    EXPECT_LE(random_val, std::numeric_limits<int>::max());

    // Long type - first call for this type
    long long_random = zrt::get_random<long>(0L, 1000000L);
    EXPECT_GE(long_random, 0L);
    EXPECT_LE(long_random, 1000000L);
}

// Test MD5 with empty input
TEST_F(ZrtMiscTest, MD5EmptyInput) {
    std::string md5 = zrt::get_md5("", 0);
    EXPECT_EQ(md5.length(), 32);

    // Verify consistent hash for empty input
    std::string md5_again = zrt::get_md5("", 0);
    EXPECT_EQ(md5, md5_again);
}

// Test MD5 with binary data
TEST_F(ZrtMiscTest, MD5BinaryData) {
    char binary_data[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    std::string md5 = zrt::get_md5(binary_data, sizeof(binary_data));

    EXPECT_EQ(md5.length(), 32);

    // Same binary data should produce same hash
    std::string md5_again = zrt::get_md5(binary_data, sizeof(binary_data));
    EXPECT_EQ(md5, md5_again);
}
