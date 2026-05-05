//
// Created by gtrade on 2024-12-03.
//

#include <climits>
#include <string>
#include "gtest/gtest.h"
#include "zrtools/zrt_fill.h"
#include "zrtools/stl_dump.h"


// 整型转换测试
TEST(FillFieldTest, IntConversion) {
    int dst_int = 0;
    zrt::fill_field(dst_int, "12345");
    EXPECT_EQ(dst_int, 12345);

    // 边界值测试
    zrt::fill_field(dst_int, std::to_string(INT_MAX).c_str());
    EXPECT_EQ(dst_int, INT_MAX);

    dst_int = 0;
    zrt::fill_field(dst_int, std::to_string(INT_MAX));
    EXPECT_EQ(dst_int, INT_MAX);

    // 异常处理测试
    // 负数转uint不会置0
    zrt::print(boost::lexical_cast<uint>("-123"));
    uint dst_uint = 999;
    zrt::fill_field(dst_uint, "-123");
    EXPECT_EQ(dst_uint, -123);
}

// 浮点型转换测试
TEST(FillFieldTest, DoubleConversion) {
    double dst_dbl = 0.0;

    zrt::fill_field(dst_dbl, "3.1415926535");
    EXPECT_DOUBLE_EQ(dst_dbl, 3.1415926535);

    zrt::fill_field(dst_dbl, "1.234e5");
    EXPECT_DOUBLE_EQ(dst_dbl, 123400.0);

    //bool handler_called = false;
    //auto custom_handler = [&](double& d, const char[]) {
    //    handler_called = true;
    //    d = -1.0;
    //};
    //zrt::fill_field(dst_dbl, "invalid", custom_handler);
    //// 目前不会调用错误handler, convert不会抛exception
    //EXPECT_FALSE(handler_called);
    //EXPECT_DOUBLE_EQ(dst_dbl, 0);
}

std::string CopyStr(std::string src) {
    return src;
}

// C字符串处理测试
TEST(FillFieldTest, CStringHandling) {
    char buf[6] = {0};
    zrt::fill_field(buf, "hello");
    EXPECT_STREQ(buf, "hello");

    zrt::fill_field(buf, "world12345");
    EXPECT_STREQ(buf, "world");
    EXPECT_EQ(buf[5], '\0');

    char buf2[12] {};
    zrt::fill_field(buf2, INT_MIN);
    EXPECT_EQ(buf2, std::to_string(INT_MIN));

    // src不以\0结尾
    char buf3[] = "hellohello";
    zrt::StrView sv(buf3, 5);
    EXPECT_EQ(buf3[5], 'h');
    zrt::fill_field(buf2, sv);
    EXPECT_EQ(buf2, sv);

    // src是右值
    std::string s1 = "123456";
    char cs3[10] {};
    zrt::StrView sv2(s1);
    EXPECT_EQ(sv2.data(), &s1[0]);
    printf("%p %p\n", sv2.data(), &s1[0]);
    EXPECT_EQ(sv2.size(), 6);
    zrt::fill_field(cs3, CopyStr(s1));
    EXPECT_STREQ(cs3, "123456");
}

// C字符串处理测试
TEST(FillFieldTest, CStringHandling2) {
    char buf3[] = "hellohello";
    char buf2[12] {};
    zrt::StrView sv(buf3);
    EXPECT_EQ(sv.size(), 10);
    EXPECT_EQ(buf3[5], 'h');
    zrt::fill_field(buf2, sv);
    EXPECT_EQ(buf2, sv);
}

// 字符串处理测试
TEST(FillFieldTest, StringHandling) {
    std::string buf;
    zrt::fill_field(buf, "hello");
    EXPECT_EQ(buf, "hello");

    zrt::fill_field(buf, "world12345");
    EXPECT_EQ(buf, "world12345");
    EXPECT_EQ(buf[5], '1');

    zrt::fill_field(buf, INT_MIN);
    EXPECT_EQ(buf, std::to_string(INT_MIN));

    char buf3[] = "hellohello";
    zrt::StrView sv(buf3, 5);
    EXPECT_EQ(buf3[5], 'h');
    zrt::fill_field(buf, sv);
    EXPECT_EQ(buf, sv);
}

// 错误处理机制测试
TEST(FillFieldTest, ErrorHandling) {
    int dst_default = 999;
    zrt::fill_field(dst_default, "12a34");
    EXPECT_EQ(dst_default, 0);

    //bool custom_handler_called = false;
    //auto handler = [&](int& d, const char*) {
    //    custom_handler_called = true;
    //    d = -999;
    //};
    //zrt::fill_field(dst_default, "invalid", handler);
    //// 目前不会调用错误handler, convert不会抛exception
    //EXPECT_FALSE(custom_handler_called);
    //EXPECT_EQ(dst_default, 0);
}

// 辅助功能测试
TEST(FillFieldTest, HelperFunctions) {
    int a = 5;
    zrt::equal_or_fill(a, 10);
    EXPECT_EQ(a, 10);

    std::string dst_str;
    zrt::fill_if_not_empty(dst_str, "");
    EXPECT_TRUE(dst_str.empty());
    zrt::fill_if_not_empty(dst_str, "non-empty");
    EXPECT_EQ(dst_str, "non-empty");
}

// 数值类型转换测试
TEST(FillFieldTest, NumericConversions) {
    double d_val = 0.0;
    zrt::fill_field(d_val, 42);
    EXPECT_DOUBLE_EQ(d_val, 42.0);
    
    zrt::fill_field(d_val, "3.14159");
    EXPECT_DOUBLE_EQ(d_val, 3.14159);
    
    unsigned int u_val = 0;
    zrt::fill_field(u_val, -5);
    // 检查负数转无符号的行为
    EXPECT_NE(u_val, 0);
    
    // 整数到浮点数的精度测试
    zrt::fill_field(d_val, INT_MAX);
    EXPECT_DOUBLE_EQ(d_val, static_cast<double>(INT_MAX));
    
    // 超大浮点数测试
    zrt::fill_field(d_val, 1e308);
    EXPECT_DOUBLE_EQ(d_val, 1e308);
}

// 边界情况测试
TEST(FillFieldTest, EdgeCases) {
    // 极限值
    long long max_val = 0;
    zrt::fill_field(max_val, LLONG_MAX);
    EXPECT_EQ(max_val, LLONG_MAX);
    
    // 空字符串
    std::string empty_str;
    std::string target_str = "should be cleared";
    zrt::fill_field(target_str, empty_str);
    EXPECT_TRUE(target_str.empty());
    
    // nullptr测试
    char buf[10] = "test";
    const char* null_ptr = nullptr;
    
    // 假设fill_field对nullptr有特殊处理
    // zrt::fill_field(buf, null_ptr);
    // EXPECT_STREQ(buf, "");
    
    // 默认构造的对象
    std::string default_str;
    int default_int = 0;
    
    zrt::fill_field(default_str, default_int);
    EXPECT_EQ(default_str, "0");
}

// 自定义类型测试
TEST(FillFieldTest, CustomTypes) {
    struct TestStruct {
        int a;
        std::string b;
        
        bool operator==(const TestStruct& other) const {
            return a == other.a && b == other.b;
        }
    };
    
    TestStruct ts1 = {1, "test"};
    TestStruct ts2 = {0, ""};
    
    // 假设fill_field支持自定义类型的复制
    zrt::fill_field(ts2, ts1);
    EXPECT_EQ(ts2.a, ts1.a);
    EXPECT_EQ(ts2.b, ts1.b);
}

// 字符数组和特殊字符测试
TEST(FillFieldTest, CharArraysAndSpecialChars) {
    char buffer[10] = {0};
    zrt::fill_field(buffer, "test");
    EXPECT_STREQ(buffer, "test");
    
    char small_buffer[3] = {0};
    zrt::fill_field(small_buffer, "test");
    // 检查截断行为
    EXPECT_STREQ(small_buffer, "te");
    
    // 特殊字符测试
    std::string dst_str;
    zrt::fill_field(dst_str, "特殊字符测试");
    EXPECT_EQ(dst_str, "特殊字符测试");
    
    zrt::fill_field(dst_str, "包含空格 和\t制表符\n以及换行");
    EXPECT_EQ(dst_str, "包含空格 和\t制表符\n以及换行");
    
    char special_buf[20] = {0};
    zrt::fill_field(special_buf, "特殊字符");
    EXPECT_STREQ(special_buf, "特殊字符");
    
    // 模拟从文件读取的二进制数据
    char binary_data[] = {0x00, 0x01, 0x02, 0x03, 0x00};
    char bin_buf[10] = {0};
    zrt::fill_field(bin_buf, binary_data);
    for (int i = 0; i < 5; ++i) {
        zrt::print((int)bin_buf[i], (int)binary_data[i]);
    }
    EXPECT_EQ(memcmp(bin_buf, binary_data, 5), -1);

    // 模拟从文件读取的二进制数据
    binary_data[0] = 0x04;
    zrt::fill_field(bin_buf, binary_data);
    for (int i = 0; i < 5; ++i) {
        zrt::print((int)bin_buf[i], (int)binary_data[i]);
    }
    EXPECT_EQ(memcmp(bin_buf, binary_data, 5), 0);
}

// 多次填充和文件操作测试
TEST(FillFieldTest, MultipleFillsAndFileOps) {
    // 文件内容填充测试
    std::string file_content = "file content test";
    std::string dst_str;
    zrt::fill_field(dst_str, file_content);
    EXPECT_EQ(dst_str, file_content);
    
    // 连续多次填充同一目标
    zrt::fill_field(dst_str, "first");
    zrt::fill_field(dst_str, "second");
    zrt::fill_field(dst_str, "third");
    EXPECT_EQ(dst_str, "third");
    
    // 不同类型多次填充
    int i_val = 10;
    zrt::fill_field(i_val, 20);
    zrt::fill_field(i_val, "30");
    zrt::fill_field(i_val, 40);
    EXPECT_EQ(i_val, 40);
}
