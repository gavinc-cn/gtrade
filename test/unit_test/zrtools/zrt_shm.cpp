//
// Created by dell on 2024/10/28.
//

#include <cmath>
#include <limits>
#include <iostream>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "zrtools/zrt_shm.h"

#include <zrtools/zrt_fill.h>

#include "zrtools/stl_dump.h"


TEST(ZrtShm, Test1) {
    struct SharedData {
        int value;
        char name[256];
    };

    SharedData data1 {};
    zrt::fill_field(data1.value, 10);
    zrt::fill_field(data1.name, "hello");
    zrt::print(data1.value, data1.name);
    EXPECT_EQ(WriteShm("hello", "", data1), true);

    SharedData data2 {};
    EXPECT_EQ(ReadShm("hello", "", data2), true);
    EXPECT_EQ(data2.value, 10);
    EXPECT_STREQ(data2.name, "hello");
}
