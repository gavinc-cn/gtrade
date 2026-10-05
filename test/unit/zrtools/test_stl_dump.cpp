// stl_dump_test.cpp

#include "gtest/gtest.h"
#include "zrtools/stl_dump.h"  // 假设这是您的头文件路径
#include <vector>
#include <set>
#include <map>
#include <tuple>
#include <string>

// 测试单容器输出
TEST(STLDumpTest, SingleContainerOutput) {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::ostringstream oss;
    oss << vec;
    EXPECT_EQ(oss.str(), "[1, 2, 3, 4, 5]");

    std::set<int> s = {1, 2, 3, 4, 5};
    oss.str("");
    oss << s;
    EXPECT_EQ(oss.str(), "[1, 2, 3, 4, 5]");
}

// 测试双容器输出
TEST(STLDumpTest, DoubleContainerOutput) {
    std::map<std::string, int> m = {{"one", 1}, {"two", 2}, {"three", 3}};
    std::ostringstream oss;
    oss << m;
    EXPECT_EQ(oss.str(), "{one:1, three:3, two:2}");

    std::unordered_map<std::string, int> um = {{"one", 1}, {"two", 2}, {"three", 3}};
    oss.str("");
    oss << um;
    EXPECT_EQ(oss.str(), "{three:3, two:2, one:1}");
}

// 测试元组输出
TEST(STLDumpTest, TupleOutput) {
    std::tuple<int, double, std::string> t = std::make_tuple(1, 2.5, "hello");
    std::ostringstream oss;
    oss << t;
    EXPECT_EQ(oss.str(), "(1, 2.5, hello)");
}

// 测试字符串转换
TEST(STLDumpTest, ToStringConversion) {
    std::vector<int> vec = {1, 2, 3};
    EXPECT_EQ(zrt::to_str(vec), "[1, 2, 3]");

    std::map<std::string, int> m = {{"one", 1}, {"two", 2}};
    EXPECT_EQ(zrt::to_str(m), "{one:1, two:2}");
}

// 测试打印函数
TEST(STLDumpTest, PrintFunction) {
    testing::internal::CaptureStdout();
    zrt::print("Hello", "World", 123);
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "Hello World 123\n");
}

//int main(int argc, char **argv) {
//    ::testing::InitGoogleTest(&argc, argv);
//    return RUN_ALL_TESTS();
//}
