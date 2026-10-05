//
// 委托持久化缓冲区单元测试
//

#include <gtest/gtest.h>
#include "persistence_buffer.h"
#include "zrtools/zrt_shm_direct.h"
#include "zrtools/zrt_time.h"
#include <cstring>

// 测试委托缓冲区基础功能
TEST(PersistenceBufferTest, EntrustBufferBasics) {
    OrderPersistenceBuffer buffer{};

    // 验证初始状态
    EXPECT_TRUE(buffer.IsValid());
    EXPECT_EQ(buffer.magic, OrderPersistenceBuffer::MAGIC_NUMBER);
    EXPECT_EQ(buffer.version, 1);
    EXPECT_EQ(buffer.GetWriteSeq(), 0);
    EXPECT_EQ(buffer.GetConfirmedSeq(), 0);
    EXPECT_EQ(buffer.GetUnconfirmedCount(), 0);

    // 添加一条委托
    Order entrust{};
    entrust.entno = 123456;
    entrust.status.m_data = '2';
    EXPECT_TRUE(buffer.Write(entrust));

    EXPECT_EQ(buffer.GetWriteSeq(), 1);
    EXPECT_EQ(buffer.GetConfirmedSeq(), 0);
    EXPECT_EQ(buffer.GetUnconfirmedCount(), 1);

    // 确认写入
    buffer.ConfirmRead(1);
    EXPECT_EQ(buffer.GetConfirmedSeq(), 1);
    EXPECT_EQ(buffer.GetUnconfirmedCount(), 0);
}

// 测试环形缓冲区
TEST(PersistenceBufferTest, EntrustRingBuffer) {
    OrderPersistenceBuffer buffer{};

    // 添加多条委托
    for (int i = 1; i <= 100; i++) {
        Order entrust{};
        entrust.entno = i;
        entrust.status.m_data = '2';
        EXPECT_TRUE(buffer.Write(entrust));
    }

    EXPECT_EQ(buffer.GetWriteSeq(), 100);
    EXPECT_EQ(buffer.GetUnconfirmedCount(), 100);

    // 恢复未确认的委托
    std::vector<Order> recovered(100);
    size_t count = buffer.ReadUnconfirmed(recovered.data(), 100);

    EXPECT_EQ(count, 100);
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(recovered[i].entno, i + 1);
    }
}

// 测试环形缓冲区覆盖
TEST(PersistenceBufferTest, EntrustRingBufferOverwrite) {
    OrderPersistenceBuffer buffer{};

    // 添加超过缓冲区大小的委托
    const size_t TOTAL = MAX_ENTRUST_BUFFER_SIZE + 100;
    size_t failed_count = 0;
    for (size_t i = 1; i <= TOTAL; i++) {
        Order entrust{};
        entrust.entno = i;
        if (!buffer.Write(entrust)) {
            failed_count++;
        }
    }

    // 由于使用ReturnFalse策略，超出容量的写入应该失败
    EXPECT_EQ(failed_count, 100);
    EXPECT_EQ(buffer.GetWriteSeq(), MAX_ENTRUST_BUFFER_SIZE);

    // 恢复时应该能够读取到所有数据
    std::vector<Order> recovered(MAX_ENTRUST_BUFFER_SIZE);
    size_t count = buffer.ReadUnconfirmed(recovered.data(), MAX_ENTRUST_BUFFER_SIZE);

    // 应该返回缓冲区容量大小的数据
    EXPECT_EQ(count, MAX_ENTRUST_BUFFER_SIZE);
}

// 测试部分确认
TEST(PersistenceBufferTest, EntrustPartialConfirm) {
    OrderPersistenceBuffer buffer{};

    // 添加100条委托
    for (int i = 1; i <= 100; i++) {
        Order entrust{};
        entrust.entno = i;
        EXPECT_TRUE(buffer.Write(entrust));
    }

    // 确认前50条
    buffer.ConfirmRead(50);
    EXPECT_EQ(buffer.GetUnconfirmedCount(), 50);

    // 恢复未确认的50条
    std::vector<Order> recovered(50);
    size_t count = buffer.ReadUnconfirmed(recovered.data(), 50);

    EXPECT_EQ(count, 50);
    // 应该从第51条开始
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(recovered[i].entno, 51 + i);
    }
}

// // 测试成交缓冲区
// TEST(PersistenceBufferTest, DoneBufferBasics) {
//     TradePersistenceBuffer buffer{};
//
//     EXPECT_TRUE(buffer.IsValid());
//     EXPECT_EQ(buffer.magic, TradePersistenceBuffer::MAGIC_NUMBER);
//     EXPECT_EQ(buffer.version, 1);
//
//     // 添加成交
//     Trade done{};
//     done.tdno = 789;
//     done.ordno = 123;
//     EXPECT_TRUE(buffer.Write(done));
//
//     EXPECT_EQ(buffer.GetWriteSeq(), 1);
//     EXPECT_EQ(buffer.GetUnconfirmedCount(), 1);
//
//     // 恢复
//     std::vector<Trade> recovered(1);
//     size_t count = buffer.ReadUnconfirmed(recovered.data(), 1);
//
//     EXPECT_EQ(count, 1);
//     EXPECT_EQ(recovered[0].tdno, 789);
//     EXPECT_EQ(recovered[0].ordno, 123);
// }

// 测试共享内存集成
TEST(PersistenceBufferTest, DirectShmIntegration) {
    // 清理可能存在的共享内存
    boost::interprocess::shared_memory_object::remove("/test_entrust_persistence");

    // 创建共享内存
    zrt::DirectShm<OrderPersistenceBuffer> shm("/test_entrust_persistence");

    ASSERT_TRUE(shm.IsValid());
    EXPECT_TRUE(shm->IsValid());

    // 添加委托
    Order entrust{};
    entrust.entno = 999;
    entrust.status.m_data = '2';
    EXPECT_TRUE(shm->Write(entrust));

    EXPECT_EQ(shm->GetWriteSeq(), 1);

    // 模拟进程重启：创建新的共享内存对象
    zrt::DirectShm<OrderPersistenceBuffer> shm2("/test_entrust_persistence");

    ASSERT_TRUE(shm2.IsValid());
    EXPECT_TRUE(shm2->IsValid());

    // 应该能读取到之前的数据
    EXPECT_EQ(shm2->GetWriteSeq(), 1);

    std::vector<Order> recovered(1);
    size_t count = shm2->ReadUnconfirmed(recovered.data(), 1);

    EXPECT_EQ(count, 1);
    EXPECT_EQ(recovered[0].entno, 999);

    // 清理
    boost::interprocess::shared_memory_object::remove("/test_entrust_persistence");
}

// 性能测试
TEST(PersistenceBufferTest, Performance) {
    zrt::DirectShm<OrderPersistenceBuffer> shm("/test_perf_entrust");

    const int ITERATIONS = 10000;
    auto start = zrt::get_epoch19();

    for (int i = 0; i < ITERATIONS; i++) {
        Order entrust{};
        entrust.entno = i;
        entrust.status.m_data = '2';
        EXPECT_TRUE(shm->Write(entrust));
    }

    auto end = zrt::get_epoch19();
    auto duration_ns = end - start;
    auto avg_ns = duration_ns / ITERATIONS;

    std::cout << "Performance: " << ITERATIONS << " writes in " << duration_ns << " ns" << std::endl;
    std::cout << "Average: " << avg_ns << " ns/write" << std::endl;
    std::cout << "Throughput: " << (1000000000.0 / avg_ns) << " writes/sec" << std::endl;

    // 验证性能应该在纳秒级
    EXPECT_LT(avg_ns, 1000) << "Write should take less than 1 microsecond";

    // 清理
    boost::interprocess::shared_memory_object::remove("/test_perf_entrust");
}
