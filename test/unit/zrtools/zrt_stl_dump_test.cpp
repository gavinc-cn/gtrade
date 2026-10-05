//
// zrt_stl_dump_test.cpp - STL 容器打印功能补充测试
//
// 注意：基础测试在 test/unit_test/test_stl_dump.cpp 中
// 本文件提供更全面的测试覆盖
//

#include "pch.h"
#include "gtest/gtest.h"
#include "zrtools/stl_dump.h"
#include <vector>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <tuple>
#include <list>
#include <deque>
#include <string>
#include <sstream>

// ========== 空容器测试 ==========

TEST(StlDumpTest, EmptyContainers) {
    std::ostringstream oss {};

    // 空 vector
    std::vector<int> empty_vec {};
    oss << empty_vec;
    EXPECT_EQ(oss.str(), "[]");
    oss.str("");

    // 空 set
    std::set<int> empty_set {};
    oss << empty_set;
    EXPECT_EQ(oss.str(), "[]");
    oss.str("");

    // 空 map
    std::map<int, int> empty_map {};
    oss << empty_map;
    EXPECT_EQ(oss.str(), "{}");
}

// ========== 单元素容器测试 ==========

TEST(StlDumpTest, SingleElementContainers) {
    std::ostringstream oss {};

    // 单元素 vector
    std::vector<int> single_vec {42};
    oss << single_vec;
    EXPECT_EQ(oss.str(), "[42]");
    oss.str("");

    // 单元素 set
    std::set<std::string> single_set {"hello"};
    oss << single_set;
    EXPECT_EQ(oss.str(), "[hello]");
    oss.str("");

    // 单元素 map
    std::map<std::string, int> single_map {{"key", 100}};
    oss << single_map;
    EXPECT_EQ(oss.str(), "{key:100}");
}

// ========== pair 测试 ==========

TEST(StlDumpTest, PairOutput) {
    std::ostringstream oss {};

    std::pair<int, std::string> p {42, "answer"};
    oss << p;
    EXPECT_EQ(oss.str(), "42:answer");
    oss.str("");

    std::pair<double, double> p2 {3.14, 2.71};
    oss << p2;
    EXPECT_TRUE(oss.str().find("3.14") != std::string::npos);
}

// ========== multiset 测试 ==========

TEST(StlDumpTest, MultisetOutput) {
    std::ostringstream oss {};

    std::multiset<int> ms {1, 2, 2, 3, 3, 3};
    oss << ms;
    EXPECT_EQ(oss.str(), "[1, 2, 2, 3, 3, 3]");
}

// ========== multimap 测试 ==========

TEST(StlDumpTest, MultimapOutput) {
    std::ostringstream oss {};

    std::multimap<std::string, int> mm {};
    mm.insert({"a", 1});
    mm.insert({"a", 2});
    mm.insert({"b", 3});

    oss << mm;
    const std::string result = oss.str();

    EXPECT_TRUE(result.find("a:") != std::string::npos);
    EXPECT_TRUE(result.find("b:") != std::string::npos);
}

// ========== unordered_set 测试 ==========

TEST(StlDumpTest, UnorderedSetOutput) {
    std::ostringstream oss {};

    std::unordered_set<int> us {3, 1, 2};
    oss << us;

    const std::string result = oss.str();
    // unordered_set 使用 {} 作为边界
    EXPECT_EQ(result.front(), '{');
    EXPECT_EQ(result.back(), '}');
    EXPECT_TRUE(result.find("1") != std::string::npos);
    EXPECT_TRUE(result.find("2") != std::string::npos);
    EXPECT_TRUE(result.find("3") != std::string::npos);
}

// ========== unordered_multiset 测试 ==========

TEST(StlDumpTest, UnorderedMultisetOutput) {
    std::ostringstream oss {};

    std::unordered_multiset<int> ums {1, 1, 2, 2, 3};
    oss << ums;

    const std::string result = oss.str();
    EXPECT_EQ(result.front(), '[');
    EXPECT_EQ(result.back(), ']');
}

// ========== unordered_multimap 测试 ==========

TEST(StlDumpTest, UnorderedMultimapOutput) {
    std::ostringstream oss {};

    std::unordered_multimap<std::string, int> umm {};
    umm.insert({"x", 1});
    umm.insert({"x", 2});

    oss << umm;

    const std::string result = oss.str();
    EXPECT_EQ(result.front(), '{');
    EXPECT_EQ(result.back(), '}');
}

// ========== tuple 测试 ==========

TEST(StlDumpTest, TupleWithVariousTypes) {
    std::ostringstream oss {};

    // 三元组
    std::tuple<int, double, std::string> t3 {1, 2.5, "three"};
    oss << t3;
    EXPECT_EQ(oss.str(), "(1, 2.5, three)");
    oss.str("");

    // 四元组
    std::tuple<int, int, int, int> t4 {1, 2, 3, 4};
    oss << t4;
    EXPECT_EQ(oss.str(), "(1, 2, 3, 4)");
    oss.str("");

    // 二元组
    std::tuple<std::string, bool> t2 {"test", true};
    oss << t2;
    EXPECT_TRUE(oss.str().find("test") != std::string::npos);
}

// ========== to_str 函数测试 ==========

TEST(StlDumpTest, ToStrFunction) {
    // 基本类型
    EXPECT_EQ(zrt::to_str(42), "42");
    EXPECT_EQ(zrt::to_str("hello"), "hello");

    // vector
    std::vector<int> vec {1, 2, 3};
    EXPECT_EQ(zrt::to_str(vec), "[1, 2, 3]");

    // 空容器
    std::vector<double> empty {};
    EXPECT_EQ(zrt::to_str(empty), "[]");

    // 嵌套容器
    std::vector<std::vector<int>> nested {{1, 2}, {3, 4}};
    const std::string nested_str = zrt::to_str(nested);
    EXPECT_TRUE(nested_str.find("[1, 2]") != std::string::npos);
    EXPECT_TRUE(nested_str.find("[3, 4]") != std::string::npos);
}

// ========== to_str 精度测试 ==========

TEST(StlDumpTest, ToStrPrecision) {
    // 浮点数精度
    const std::string result = zrt::to_str(3.14159265);
    EXPECT_TRUE(result.find("3.14") != std::string::npos);
}

// ========== print 函数测试 ==========

TEST(StlDumpTest, PrintNoArgs) {
    testing::internal::CaptureStdout();
    zrt::print();
    const std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "\n");
}

TEST(StlDumpTest, PrintSingleArg) {
    testing::internal::CaptureStdout();
    zrt::print("hello");
    const std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "hello\n");
}

TEST(StlDumpTest, PrintMultipleArgs) {
    testing::internal::CaptureStdout();
    zrt::print(1, 2.5, "three", true);
    const std::string output = testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("1") != std::string::npos);
    EXPECT_TRUE(output.find("2.5") != std::string::npos);
    EXPECT_TRUE(output.find("three") != std::string::npos);
}

TEST(StlDumpTest, PrintContainers) {
    testing::internal::CaptureStdout();
    std::vector<int> vec {1, 2, 3};
    zrt::print(vec);
    const std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "[1, 2, 3]\n");
}

TEST(StlDumpTest, PrintToStream) {
    std::ostringstream oss {};

    // 使用 std::ostream 引用调用
    std::ostream& os = oss;
    zrt::print(os, "test", 123);

    EXPECT_EQ(oss.str(), "test 123\n");
}

// ========== 嵌套容器测试 ==========

TEST(StlDumpTest, NestedContainers) {
    std::ostringstream oss {};

    // vector of vectors
    std::vector<std::vector<int>> vv {{1, 2}, {3, 4, 5}};
    oss << vv;
    EXPECT_EQ(oss.str(), "[[1, 2], [3, 4, 5]]");
    oss.str("");

    // map of vectors
    std::map<std::string, std::vector<int>> mv {};
    mv["a"] = {1, 2};
    mv["b"] = {3, 4};

    oss << mv;
    const std::string result = oss.str();
    EXPECT_TRUE(result.find("a:[1, 2]") != std::string::npos);
    EXPECT_TRUE(result.find("b:[3, 4]") != std::string::npos);
}

// ========== 复杂类型测试 ==========

TEST(StlDumpTest, ComplexTypes) {
    std::ostringstream oss {};

    // map with string values
    std::map<int, std::string> ms {{1, "one"}, {2, "two"}};
    oss << ms;
    EXPECT_EQ(oss.str(), "{1:one, 2:two}");
}

// ========== 字符串类型测试 ==========

TEST(StlDumpTest, StringContainers) {
    std::ostringstream oss {};

    std::vector<std::string> vs {"hello", "world", "test"};
    oss << vs;
    EXPECT_EQ(oss.str(), "[hello, world, test]");
    oss.str("");

    std::set<std::string> ss {"apple", "banana", "cherry"};
    oss << ss;
    EXPECT_EQ(oss.str(), "[apple, banana, cherry]");
}

// ========== 数值类型测试 ==========

TEST(StlDumpTest, NumericTypes) {
    std::ostringstream oss {};

    // 整数
    std::vector<int> vi {-1, 0, 1, INT_MAX, INT_MIN};
    oss << vi;
    const std::string int_result = oss.str();
    EXPECT_TRUE(int_result.find("-1") != std::string::npos);
    EXPECT_TRUE(int_result.find("0") != std::string::npos);
    oss.str("");

    // 浮点数
    std::vector<double> vd {0.0, 1.5, -2.5, 3.14159};
    oss << vd;
    const std::string double_result = oss.str();
    EXPECT_TRUE(double_result.find("1.5") != std::string::npos);
}

// ========== 特殊字符测试 ==========

TEST(StlDumpTest, SpecialCharactersInStrings) {
    std::ostringstream oss {};

    std::vector<std::string> vs {"hello world", "tab\there", "new\nline"};
    oss << vs;

    // 输出应该包含特殊字符
    const std::string result = oss.str();
    EXPECT_TRUE(result.find("hello world") != std::string::npos);
}

// ========== 大容器测试 ==========

TEST(StlDumpTest, LargeContainer) {
    std::vector<int> large_vec {};
    large_vec.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        large_vec.push_back(i);
    }

    const std::string result = zrt::to_str(large_vec);

    EXPECT_TRUE(result.find("[0") != std::string::npos);
    EXPECT_TRUE(result.find("999]") != std::string::npos);
}
