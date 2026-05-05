#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "zrtools/io_pool_v2/io_pool.h"  // Include the header file where IOPool is defined

// Mock service class to use with IOPool
class MockService {
public:
    void Init() {}
    void ThreadStart() { running = true; }
    void ThreadStop() { running = false; }
    bool IsRunning() const { return running; }
private:
    bool running = false;
};

// Test fixture for IOPool
class IOPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.named_threads = {"thread1", "thread2"};
        config.anonymous_thread_num = 3;
        pool = std::make_unique<IOPool<MockService>>(config);
    }

    IOPoolConfig config;
    std::unique_ptr<IOPool<MockService>> pool;
};

// Test case for checking initial state
TEST_F(IOPoolTest, InitialState) {
    EXPECT_FALSE(pool->IsStarted());
}

// Test case for initializing and starting the pool
TEST_F(IOPoolTest, InitAndStart) {
    pool->Register("thread1", std::make_shared<MockService>());
    pool->Register("thread2", std::make_shared<MockService>());
    pool->Init();
    pool->Start();
    EXPECT_TRUE(pool->IsStarted());

// Check if named threads are started
    EXPECT_TRUE(pool->GetNamedThread("thread1")->IsRunning());
    EXPECT_TRUE(pool->GetNamedThread("thread2")->IsRunning());

// Check if anonymous threads are started
    for (int i = 0; i < 3; ++i) {
        // EXPECT_TRUE(pool->GetSharedThread()->IsRunning());
    }
}

// Test case for stopping the pool
TEST_F(IOPoolTest, Stop) {
    pool->Register("thread1", std::make_shared<MockService>());
    pool->Register("thread2", std::make_shared<MockService>());
    pool->Init();
    pool->Start();
    pool->Stop();
    EXPECT_FALSE(pool->IsStarted());

// Check if named threads are stopped
    EXPECT_FALSE(pool->GetNamedThread("thread1")->IsRunning());
    EXPECT_FALSE(pool->GetNamedThread("thread2")->IsRunning());

// Check if anonymous threads are stopped
    for (int i = 0; i < 3; ++i) {
        // EXPECT_FALSE(pool->GetSharedThread()->IsRunning());
    }
}

// Test case for getting named thread
TEST_F(IOPoolTest, GetNamedThread) {
    pool->Register("new_thread", std::make_shared<MockService>());
    pool->Register("another_thread", std::make_shared<MockService>());
    pool->Init();
    EXPECT_FALSE(pool->GetNamedThread("new_thread")->IsRunning());
    EXPECT_FALSE(pool->GetNamedThread("another_thread")->IsRunning());
    pool->Start();
    EXPECT_TRUE(pool->GetNamedThread("new_thread")->IsRunning());
    EXPECT_TRUE(pool->GetNamedThread("another_thread")->IsRunning());
}

//// Test case for getting shared thread
//TEST_F(IOPoolTest, GetSharedThread) {
//    pool->Init();
//    std::vector<bool> seen(3, false);
//    for (int i = 0; i < 6; ++i) {  // More than the number of anonymous threads to check round-robin
//        auto &thread = pool->GetSharedThread();
//        EXPECT_TRUE(thread->IsRunning());
//        size_t index = std::find(pool->m_anonymous_threads.begin(), pool->m_anonymous_threads.end(), &thread) - pool->m_anonymous_threads.begin();
//        seen[index] = true;
//    }
//    for (bool s: seen) {
//        EXPECT_TRUE(s);  // Ensure all threads were accessed at least once
//    }
//}

//int main(int argc, char **argv) {
//    ::testing::InitGoogleTest(&argc, argv);
//    return RUN_ALL_TESTS();
//}
