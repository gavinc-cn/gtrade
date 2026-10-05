//
// 单线程直接访问共享内存的性能测试
//

#include <chrono>
#include <iostream>
#include <gtest/gtest.h>
#include "zrtools/zrt_shm.h"
// #include "zrtools/deprecated/zrt_shm_v2.h"
// #include "zrtools/deprecated/zrt_shm_single_writer.h"
#include "zrtools/zrt_shm_direct.h"

struct TestData {
    double fst_target_pos;
    double fst_cur_pos;
    double sec_target_pos;
    double sec_cur_pos;
    int strat_status;
};

class DirectShmTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_data_.fst_target_pos = 100.5;
        test_data_.fst_cur_pos = 50.25;
        test_data_.sec_target_pos = -100.5;
        test_data_.sec_cur_pos = -50.25;
        test_data_.strat_status = 3;
    }

    void TearDown() override {
        boost::interprocess::shared_memory_object::remove("test_old");
        boost::interprocess::shared_memory_object::remove("test_v2");
        boost::interprocess::shared_memory_object::remove("test_single");
        boost::interprocess::shared_memory_object::remove("test_direct");
    }

    TestData test_data_;
};

// 1. 基础功能测试
TEST_F(DirectShmTest, BasicUsage) {
    zrt::DirectShm<TestData> shm("test_direct");
    ASSERT_TRUE(shm.IsValid());

    // 使用指针操作符
    shm->fst_target_pos = 100.5;
    shm->fst_cur_pos = 50.25;
    EXPECT_DOUBLE_EQ(shm->fst_target_pos, 100.5);

    // 使用解引用操作符
    (*shm).sec_target_pos = -100.5;
    EXPECT_DOUBLE_EQ((*shm).sec_target_pos, -100.5);

    // 使用Get方法
    auto* data = shm.Get();
    data->strat_status = 5;
    EXPECT_EQ(shm->strat_status, 5);

    // 使用Read/Write
    shm.Write(test_data_);
    TestData read_data = shm.Read();
    EXPECT_DOUBLE_EQ(read_data.fst_target_pos, test_data_.fst_target_pos);
}

// 2. 持久化测试
TEST_F(DirectShmTest, Persistence) {
    {
        zrt::DirectShm<TestData> shm("test_direct");
        shm->fst_target_pos = 123.456;
        shm->strat_status = 7;
    }

    // 重新打开
    {
        zrt::DirectShm<TestData> shm("test_direct");
        EXPECT_DOUBLE_EQ(shm->fst_target_pos, 123.456);
        EXPECT_EQ(shm->strat_status, 7);
    }
}

// 3. 性能测试：直接指针访问
TEST_F(DirectShmTest, DirectAccess) {
    const int iterations = 100000;
    zrt::DirectShm<TestData> shm("test_direct");

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        shm->fst_cur_pos += 1.0;
        shm->sec_cur_pos -= 1.0;
        shm->strat_status = i % 10;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    std::cout << "\n【DirectShm】直接指针访问 (100,000次):\n"
              << "  Total: " << duration.count() / 1000 << " μs\n"
              << "  Per op: " << duration.count() / iterations << " ns\n"
              << "  Throughput: " << (iterations * 1e9 / duration.count()) << " ops/s\n";
}

// 4. 终极性能对比
TEST_F(DirectShmTest, UltimateComparison) {
    const int iterations = 100000;

    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "终极性能对比 (100,000 次迭代)\n";
    std::cout << std::string(80, '=') << "\n\n";

    struct Result {
        std::string name;
        double ns_per_op;
        double speedup;
    };

    std::vector<Result> results;

    // 旧实现
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            test_data_.strat_status = i;
            WriteShm("test_old", "data", test_data_);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back({"旧实现 (delete+create)", ns * 1.0 / iterations, 1.0});
    }

    // // V2实现
    // {
    //     zrt::FastShmWriter<TestData> writer("test_v2");
    //     auto start = std::chrono::high_resolution_clock::now();
    //     for (int i = 0; i < iterations; ++i) {
    //         test_data_.strat_status = i;
    //         writer.Write(test_data_);
    //     }
    //     auto end = std::chrono::high_resolution_clock::now();
    //     auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    //     results.push_back({"V2实现 (atomic)", ns * 1.0 / iterations, results[0].ns_per_op / (ns * 1.0 / iterations)});
    // }

    // // 单写优化
    // {
    //     zrt::SingleWriterShm<TestData> writer("test_single");
    //     auto start = std::chrono::high_resolution_clock::now();
    //     for (int i = 0; i < iterations; ++i) {
    //         test_data_.strat_status = i;
    //         writer.Write(test_data_);
    //     }
    //     auto end = std::chrono::high_resolution_clock::now();
    //     auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    //     results.push_back({"单写优化 (seqlock)", ns * 1.0 / iterations, results[0].ns_per_op / (ns * 1.0 / iterations)});
    // }
    //
    // // 单写零拷贝
    // {
    //     zrt::SingleWriterShm<TestData> writer("test_single");
    //     auto start = std::chrono::high_resolution_clock::now();
    //     for (int i = 0; i < iterations; ++i) {
    //         auto* data = writer.GetWritePointer();
    //         data->strat_status = i;
    //         writer.Commit();
    //     }
    //     auto end = std::chrono::high_resolution_clock::now();
    //     auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    //     results.push_back({"单写零拷贝", ns * 1.0 / iterations, results[0].ns_per_op / (ns * 1.0 / iterations)});
    // }

    // 直接访问（Write方法）
    {
        zrt::DirectShm<TestData> shm("test_direct");
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            test_data_.strat_status = i;
            shm.Write(test_data_);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back({"直接访问 (Write)", ns * 1.0 / iterations, results[0].ns_per_op / (ns * 1.0 / iterations)});
    }

    // 直接访问（指针操作）
    {
        zrt::DirectShm<TestData> shm("test_direct");
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            shm->strat_status = i;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back({"直接访问 (指针)", ns * 1.0 / iterations, results[0].ns_per_op / (ns * 1.0 / iterations)});
    }

    // 直接访问（多字段修改）
    {
        zrt::DirectShm<TestData> shm("test_direct");
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            shm->fst_cur_pos += 1.0;
            shm->sec_cur_pos -= 1.0;
            shm->strat_status = i % 10;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back({"直接访问 (3字段)", ns * 1.0 / iterations, results[0].ns_per_op / (ns * 1.0 / iterations)});
    }

    // 本地内存访问（基准对比）
    {
        TestData local_data = test_data_;
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            local_data.strat_status = i;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results.push_back({"本地内存 (基准)", ns * 1.0 / iterations, results[0].ns_per_op / (ns * 1.0 / iterations)});
    }

    // 打印结果
    printf("%-30s %15s %15s %15s\n", "实现方式", "延迟(ns/op)", "相对提升", "吞吐量(Mops/s)");
    printf("%s\n", std::string(78, '-').c_str());
    for (const auto& r : results) {
        printf("%-30s %15.2f %14.1fx %15.1f\n",
               r.name.c_str(), r.ns_per_op, r.speedup, 1000.0 / r.ns_per_op);
    }
    printf("\n");
}

// 5. 真实使用场景模拟
TEST_F(DirectShmTest, RealWorldScenario) {
    const int iterations = 10000;
    zrt::DirectShm<TestData> shm("test_direct");

    std::cout << "\n真实场景模拟：策略每个tick更新位置\n";

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        // 模拟收到tick，更新位置
        double trade_amount = (i % 2 == 0) ? 10.0 : -10.0;

        shm->fst_cur_pos += trade_amount;
        shm->sec_cur_pos -= trade_amount;

        // 更新状态
        if (i % 100 == 0) {
            shm->strat_status = 1;  // Ready
        } else if (i % 10 == 0) {
            shm->strat_status = 2;  // Trading
        }

        // 检查风控
        if (std::abs(shm->fst_cur_pos - shm->sec_cur_pos) > 100) {
            shm->strat_status = 3;  // Risk Alert
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "  处理 " << iterations << " 个ticks\n"
              << "  总耗时: " << duration.count() << " μs\n"
              << "  平均延迟: " << (duration.count() * 1000.0 / iterations) << " ns/tick\n"
              << "  吞吐量: " << (iterations * 1e6 / duration.count()) << " ticks/s\n";

    // 验证数据正确性
    EXPECT_EQ(shm->fst_cur_pos, 0.0);  // 一正一负抵消
}

// 6. 内存布局验证
TEST_F(DirectShmTest, MemoryLayout) {
    zrt::DirectShm<TestData> shm("test_direct");

    std::cout << "\n内存布局信息:\n"
              << "  DirectShmHeader: " << sizeof(zrt::DirectShmHeader) << " bytes\n"
              << "  TestData: " << sizeof(TestData) << " bytes\n"
              << "  Total: " << sizeof(zrt::DirectShmHeader) + sizeof(TestData) << " bytes\n"
              << "  指针对齐: " << alignof(TestData) << " bytes\n";
}

// 7. 便捷函数测试
TEST_F(DirectShmTest, ConvenienceFunction) {
    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        auto& shm = zrt::GetDirectShm<TestData>("test_direct");
        shm->strat_status = i;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    std::cout << "\n便捷函数性能 (thread-local cache):\n"
              << "  Per op: " << duration.count() / iterations << " ns\n";

    // 验证缓存生效
    auto& shm1 = zrt::GetDirectShm<TestData>("test_direct");
    auto& shm2 = zrt::GetDirectShm<TestData>("test_direct");
    EXPECT_EQ(&shm1, &shm2);  // 同一个对象
}
