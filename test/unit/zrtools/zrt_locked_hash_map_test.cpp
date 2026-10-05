//
// Created by dell on 2025/12/30.
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/zrt_locked_hash_map.h"
#include <thread>
#include <vector>
#include <chrono>

using namespace zrt;

// 基本功能测试
TEST(LockedHashMapTest, BasicOperations) {
    LockedHashMap<int, std::string> map{};

    // 测试插入
    EXPECT_TRUE(map.insert(1, "one"));
    EXPECT_FALSE(map.insert(1, "ONE"));  // 重复插入应该失败

    // 测试查找
    auto result = map.find(1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "one");

    // 查找不存在的key
    EXPECT_FALSE(map.find(999).has_value());

    // 测试大小
    EXPECT_EQ(map.size(), 1);
    EXPECT_FALSE(map.empty());
}

// 测试 insert_or_assign
TEST(LockedHashMapTest, InsertOrAssign) {
    LockedHashMap<int, int> map{};

    // 首次插入
    EXPECT_TRUE(map.insert_or_assign(1, 100));
    EXPECT_EQ(map.find(1).value(), 100);

    // 更新已存在的key
    EXPECT_FALSE(map.insert_or_assign(1, 200));
    EXPECT_EQ(map.find(1).value(), 200);
}

// 测试 emplace
TEST(LockedHashMapTest, Emplace) {
    LockedHashMap<std::string, std::string> map{};

    EXPECT_TRUE(map.emplace("key1", "value1"));
    EXPECT_FALSE(map.emplace("key1", "value2"));  // 已存在

    auto result = map.find("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "value1");
}

// 测试 contains
TEST(LockedHashMapTest, Contains) {
    LockedHashMap<int, std::string> map{};

    map.insert(1, "one");
    map.insert(2, "two");

    EXPECT_TRUE(map.contains(1));
    EXPECT_TRUE(map.contains(2));
    EXPECT_FALSE(map.contains(3));
}

// 测试 get_or_default
TEST(LockedHashMapTest, GetOrDefault) {
    LockedHashMap<int, std::string> map{};

    map.insert(1, "one");

    EXPECT_EQ(map.get_or_default(1, "default"), "one");
    EXPECT_EQ(map.get_or_default(999, "default"), "default");
}

// 测试 operator[]
TEST(LockedHashMapTest, OperatorBracket) {
    LockedHashMap<int, int> map{};

    map.insert(1, 100);
    EXPECT_EQ(map[1], 100);

    // operator[] 会插入不存在的key (默认构造值)
    int value = map[999];
    EXPECT_EQ(value, 0);  // int 默认初始化为 0
    EXPECT_TRUE(map.contains(999));
}

// 测试 erase
TEST(LockedHashMapTest, Erase) {
    LockedHashMap<int, std::string> map{};

    map.insert(1, "one");
    map.insert(2, "two");

    EXPECT_EQ(map.size(), 2);

    EXPECT_TRUE(map.erase(1));
    EXPECT_EQ(map.size(), 1);
    EXPECT_FALSE(map.contains(1));

    EXPECT_FALSE(map.erase(999));  // 删除不存在的key
}

// 测试 clear
TEST(LockedHashMapTest, Clear) {
    LockedHashMap<int, std::string> map{};

    map.insert(1, "one");
    map.insert(2, "two");
    map.insert(3, "three");

    EXPECT_EQ(map.size(), 3);

    map.clear();

    EXPECT_EQ(map.size(), 0);
    EXPECT_TRUE(map.empty());
    EXPECT_FALSE(map.contains(1));
}

// 测试 for_each
TEST(LockedHashMapTest, ForEach) {
    LockedHashMap<int, int> map{};

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);

    int sum = 0;
    map.for_each([&sum](const int& key, const int& value) {
        sum += value;
    });

    EXPECT_EQ(sum, 60);
}

// 测试 for_each_mutable
TEST(LockedHashMapTest, ForEachMutable) {
    LockedHashMap<int, int> map{};

    map.insert(1, 10);
    map.insert(2, 20);
    map.insert(3, 30);

    // 所有值乘以2
    map.for_each_mutable([](const int& key, int& value) {
        value *= 2;
    });

    EXPECT_EQ(map.find(1).value(), 20);
    EXPECT_EQ(map.find(2).value(), 40);
    EXPECT_EQ(map.find(3).value(), 60);
}

// 测试 update_if_exists
TEST(LockedHashMapTest, UpdateIfExists) {
    LockedHashMap<int, int> map{};

    map.insert(1, 100);

    // 更新存在的key
    bool updated = map.update_if_exists(1, [](const int& old_value) {
        return old_value + 50;
    });

    EXPECT_TRUE(updated);
    EXPECT_EQ(map.find(1).value(), 150);

    // 尝试更新不存在的key
    updated = map.update_if_exists(999, [](const int& old_value) {
        return old_value + 50;
    });

    EXPECT_FALSE(updated);
}

// 测试 keys 和 values
TEST(LockedHashMapTest, KeysAndValues) {
    LockedHashMap<int, std::string> map{};

    map.insert(1, "one");
    map.insert(2, "two");
    map.insert(3, "three");

    auto keys = map.keys();
    auto values = map.values();

    EXPECT_EQ(keys.size(), 3);
    EXPECT_EQ(values.size(), 3);

    // 检查keys包含所有预期值
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys[0], 1);
    EXPECT_EQ(keys[1], 2);
    EXPECT_EQ(keys[2], 3);

    // 检查values包含所有预期值
    std::sort(values.begin(), values.end());
    EXPECT_EQ(values[0], "one");
    EXPECT_EQ(values[1], "three");
    EXPECT_EQ(values[2], "two");
}

// 多线程并发写入测试
TEST(LockedHashMapTest, ConcurrentWrites) {
    LockedHashMap<int, int> map{};
    constexpr int num_threads = 10;
    constexpr int ops_per_thread = 1000;

    std::vector<std::thread> threads{};
    threads.reserve(num_threads);

    // 每个线程插入不同的key范围
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&map, i]() {
            int start = i * ops_per_thread;
            int end = start + ops_per_thread;
            for (int j = start; j < end; ++j) {
                map.insert(j, j * 10);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // 验证所有插入都成功
    EXPECT_EQ(map.size(), num_threads * ops_per_thread);

    // 验证几个随机值
    EXPECT_EQ(map.find(0).value(), 0);
    EXPECT_EQ(map.find(500).value(), 5000);
    EXPECT_EQ(map.find(9999).value(), 99990);
}

// 多线程并发读写测试
TEST(LockedHashMapTest, ConcurrentReadsAndWrites) {
    LockedHashMap<int, int> map{};
    constexpr int num_keys = 100;

    // 预填充数据
    for (int i = 0; i < num_keys; ++i) {
        map.insert(i, i);
    }

    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};
    std::atomic<bool> stop{false};

    // 读线程
    auto reader = [&]() {
        while (!stop.load()) {
            for (int i = 0; i < num_keys; ++i) {
                auto result = map.find(i);
                if (result.has_value()) {
                    read_count++;
                }
            }
        }
    };

    // 写线程
    auto writer = [&]() {
        while (!stop.load()) {
            for (int i = 0; i < num_keys; ++i) {
                map.insert_or_assign(i, i + 1);
                write_count++;
            }
        }
    };

    // 启动多个读写线程
    std::vector<std::thread> threads{};
    threads.reserve(6);

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(reader);
    }
    for (int i = 0; i < 2; ++i) {
        threads.emplace_back(writer);
    }

    // 运行一小段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true);

    for (auto& t : threads) {
        t.join();
    }

    // 验证map状态正常
    EXPECT_EQ(map.size(), num_keys);
    EXPECT_GT(read_count.load(), 0);
    EXPECT_GT(write_count.load(), 0);
}

// 测试空map的行为
TEST(LockedHashMapTest, EmptyMapBehavior) {
    LockedHashMap<int, std::string> map{};

    EXPECT_TRUE(map.empty());
    EXPECT_EQ(map.size(), 0);
    EXPECT_FALSE(map.contains(1));
    EXPECT_FALSE(map.find(1).has_value());
    EXPECT_EQ(map.get_or_default(1, "default"), "default");
    EXPECT_FALSE(map.erase(1));

    auto keys = map.keys();
    auto values = map.values();
    EXPECT_TRUE(keys.empty());
    EXPECT_TRUE(values.empty());
}

// 测试自定义类型
struct CustomType {
    int id;
    std::string name;

    CustomType() : id{0}, name{} {}
    CustomType(int i, const std::string& n) : id{i}, name{n} {}

    bool operator==(const CustomType& other) const {
        return id == other.id && name == other.name;
    }
};

TEST(LockedHashMapTest, CustomValueType) {
    LockedHashMap<int, CustomType> map{};

    CustomType obj1{1, "Alice"};
    CustomType obj2{2, "Bob"};

    map.insert(1, obj1);
    map.insert(2, obj2);

    auto result = map.find(1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().id, 1);
    EXPECT_EQ(result.value().name, "Alice");
}

// 性能基准测试 (简单版本)
TEST(LockedHashMapTest, PerformanceBenchmark) {
    LockedHashMap<int, int> map{};
    constexpr int num_ops = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    // 插入
    for (int i = 0; i < num_ops; ++i) {
        map.insert(i, i);
    }

    // 查找
    for (int i = 0; i < num_ops; ++i) {
        map.find(i);
    }

    // 删除
    for (int i = 0; i < num_ops; ++i) {
        map.erase(i);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // 只是确保能在合理时间内完成 (不做严格断言)
    EXPECT_LT(duration.count(), 1000);  // 应该在1秒内完成
    EXPECT_TRUE(map.empty());
}
