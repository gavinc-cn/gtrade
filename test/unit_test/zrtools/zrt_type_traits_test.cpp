//
// Created by Claude Code on 2026/1/29.
//

#include "pch.h"
#include <gtest/gtest.h>
#include <vector>
#include <map>
#include <set>
#include <array>
#include <string>
#include "zrtools/zrt_type_traits.h"

class TypeTraitsTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
};

// Test is_pair
TEST_F(TypeTraitsTest, IsPair) {
    // std::pair should be detected
    EXPECT_TRUE((zrt::is_pair<std::pair<int, int>>::value));
    EXPECT_TRUE((zrt::is_pair<std::pair<std::string, double>>::value));
    EXPECT_TRUE((zrt::is_pair<std::pair<int, std::string>>::value));

    // Non-pair types
    EXPECT_FALSE((zrt::is_pair<int>::value));
    EXPECT_FALSE((zrt::is_pair<std::vector<int>>::value));
    EXPECT_FALSE((zrt::is_pair<std::string>::value));
    EXPECT_FALSE((zrt::is_pair<double>::value));
}

// Test has_iterator
TEST_F(TypeTraitsTest, HasIterator) {
    // Types with iterator
    EXPECT_TRUE((zrt::has_iterator<std::vector<int>>::value));
    EXPECT_TRUE((zrt::has_iterator<std::string>::value));
    EXPECT_TRUE((zrt::has_iterator<std::map<int, int>>::value));
    EXPECT_TRUE((zrt::has_iterator<std::set<int>>::value));

    // Types without iterator
    EXPECT_FALSE((zrt::has_iterator<int>::value));
    EXPECT_FALSE((zrt::has_iterator<double>::value));
}

// Test has_begin_end
TEST_F(TypeTraitsTest, HasBeginEnd) {
    // Types with begin/end
    EXPECT_TRUE((zrt::has_begin_end<std::vector<int>>::value));
    EXPECT_TRUE((zrt::has_begin_end<std::string>::value));
    EXPECT_TRUE((zrt::has_begin_end<std::array<int, 5>>::value));
    EXPECT_TRUE((zrt::has_begin_end<int[5]>::value));  // C-array

    // Types without begin/end
    EXPECT_FALSE((zrt::has_begin_end<int>::value));
    EXPECT_FALSE((zrt::has_begin_end<double>::value));
}

// Test is_iterable
TEST_F(TypeTraitsTest, IsIterable) {
    // Iterable types
    EXPECT_TRUE((zrt::is_iterable<std::vector<int>>::value));
    EXPECT_TRUE((zrt::is_iterable<std::string>::value));
    EXPECT_TRUE((zrt::is_iterable<std::map<int, int>>::value));
    EXPECT_TRUE((zrt::is_iterable<std::set<int>>::value));

    // Non-iterable types
    EXPECT_FALSE((zrt::is_iterable<int>::value));
    EXPECT_FALSE((zrt::is_iterable<double>::value));
    EXPECT_FALSE((zrt::is_iterable<std::pair<int, int>>::value));
}

// Test IsCStr
TEST_F(TypeTraitsTest, IsCStr) {
    // C-string types
    EXPECT_TRUE((zrt::IsCStr_v<char*>));
    EXPECT_TRUE((zrt::IsCStr_v<const char*>));
    EXPECT_TRUE((zrt::IsCStr_v<char[10]>));
    EXPECT_TRUE((zrt::IsCStr_v<const char[5]>));

    // Non C-string types
    EXPECT_FALSE((zrt::IsCStr_v<int*>));
    EXPECT_FALSE((zrt::IsCStr_v<std::string>));
    EXPECT_FALSE((zrt::IsCStr_v<char>));
    EXPECT_FALSE((zrt::IsCStr_v<int>));
}

// Test IsChar_v
TEST_F(TypeTraitsTest, IsChar) {
    EXPECT_TRUE((zrt::IsChar_v<char>));

    EXPECT_FALSE((zrt::IsChar_v<int>));
    EXPECT_FALSE((zrt::IsChar_v<char*>));
    EXPECT_FALSE((zrt::IsChar_v<signed char>));
    EXPECT_FALSE((zrt::IsChar_v<unsigned char>));
    EXPECT_FALSE((zrt::IsChar_v<wchar_t>));
}

// Test IsIntegralNum_v
TEST_F(TypeTraitsTest, IsIntegralNum) {
    // Integral types (excluding char)
    EXPECT_TRUE((zrt::IsIntegralNum_v<int>));
    EXPECT_TRUE((zrt::IsIntegralNum_v<short>));
    EXPECT_TRUE((zrt::IsIntegralNum_v<long>));
    EXPECT_TRUE((zrt::IsIntegralNum_v<long long>));
    EXPECT_TRUE((zrt::IsIntegralNum_v<unsigned int>));
    EXPECT_TRUE((zrt::IsIntegralNum_v<unsigned long>));

    // char is excluded
    EXPECT_FALSE((zrt::IsIntegralNum_v<char>));

    // Non-integral types
    EXPECT_FALSE((zrt::IsIntegralNum_v<float>));
    EXPECT_FALSE((zrt::IsIntegralNum_v<double>));
    EXPECT_FALSE((zrt::IsIntegralNum_v<std::string>));
}

// Test IsFloatingPoint_v
TEST_F(TypeTraitsTest, IsFloatingPoint) {
    EXPECT_TRUE((zrt::IsFloatingPoint_v<float>));
    EXPECT_TRUE((zrt::IsFloatingPoint_v<double>));
    EXPECT_TRUE((zrt::IsFloatingPoint_v<long double>));

    EXPECT_FALSE((zrt::IsFloatingPoint_v<int>));
    EXPECT_FALSE((zrt::IsFloatingPoint_v<char>));
    EXPECT_FALSE((zrt::IsFloatingPoint_v<std::string>));
}

// Test IsPointer_v
TEST_F(TypeTraitsTest, IsPointer) {
    EXPECT_TRUE((zrt::IsPointer_v<int*>));
    EXPECT_TRUE((zrt::IsPointer_v<char*>));
    EXPECT_TRUE((zrt::IsPointer_v<const int*>));
    EXPECT_TRUE((zrt::IsPointer_v<void*>));

    EXPECT_FALSE((zrt::IsPointer_v<int>));
    EXPECT_FALSE((zrt::IsPointer_v<int&>));
    EXPECT_FALSE((zrt::IsPointer_v<std::string>));
}

// Test IsEnum_v
TEST_F(TypeTraitsTest, IsEnum) {
    enum OldStyleEnum { A, B, C };
    enum class ScopedEnum { X, Y, Z };

    EXPECT_TRUE((zrt::IsEnum_v<OldStyleEnum>));
    EXPECT_TRUE((zrt::IsEnum_v<ScopedEnum>));

    EXPECT_FALSE((zrt::IsEnum_v<int>));
    EXPECT_FALSE((zrt::IsEnum_v<std::string>));
    EXPECT_FALSE((zrt::IsEnum_v<double>));
}

// Test IsBothIntegralNum_v
TEST_F(TypeTraitsTest, IsBothIntegralNum) {
    EXPECT_TRUE((zrt::IsBothIntegralNum_v<int, int>));
    EXPECT_TRUE((zrt::IsBothIntegralNum_v<short, long>));
    EXPECT_TRUE((zrt::IsBothIntegralNum_v<unsigned int, int>));

    EXPECT_FALSE((zrt::IsBothIntegralNum_v<int, char>));  // char excluded
    EXPECT_FALSE((zrt::IsBothIntegralNum_v<char, int>));
    EXPECT_FALSE((zrt::IsBothIntegralNum_v<int, double>));
    EXPECT_FALSE((zrt::IsBothIntegralNum_v<float, int>));
    EXPECT_FALSE((zrt::IsBothIntegralNum_v<char, char>));
}

// Test with const and reference types
TEST_F(TypeTraitsTest, ConstAndRef) {
    // const types
    EXPECT_TRUE((zrt::IsIntegralNum_v<const int>));
    EXPECT_TRUE((zrt::IsFloatingPoint_v<const double>));

    // Pointers to const
    EXPECT_TRUE((zrt::IsPointer_v<const int*>));
    EXPECT_TRUE((zrt::IsCStr_v<const char*>));
}

// Test with user-defined types
TEST_F(TypeTraitsTest, UserDefinedTypes) {
    struct MyStruct {
        int x;
        double y;
    };

    class MyClass {
    public:
        int value;
    };

    // User-defined types should not be integral, floating point, etc.
    EXPECT_FALSE((zrt::IsIntegralNum_v<MyStruct>));
    EXPECT_FALSE((zrt::IsFloatingPoint_v<MyStruct>));
    EXPECT_FALSE((zrt::IsPointer_v<MyStruct>));
    EXPECT_FALSE((zrt::IsEnum_v<MyClass>));
    EXPECT_FALSE((zrt::IsCStr_v<MyClass>));
}

// Test with standard library types
TEST_F(TypeTraitsTest, StdLibTypes) {
    // std::string is not integral or floating point
    EXPECT_FALSE((zrt::IsIntegralNum_v<std::string>));
    EXPECT_FALSE((zrt::IsFloatingPoint_v<std::string>));

    // But it has iterators
    EXPECT_TRUE((zrt::has_iterator<std::string>::value));
    EXPECT_TRUE((zrt::is_iterable<std::string>::value));

    // std::vector
    EXPECT_FALSE((zrt::IsIntegralNum_v<std::vector<int>>));
    EXPECT_TRUE((zrt::is_iterable<std::vector<int>>::value));
}
