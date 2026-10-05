#include <gtest/gtest.h>
#include "zrtools/zrt_bmic_ordered.h"
#include "zrtools/zrt_fill.h"
#include "zrtools/zrt_bmic_hashed.h"
#include "../test_interface.h"
//
//
//using namespace zrt;
//using namespace boost::multi_index;
//
//
TEST(BMIC, AddElement) {
    zrt::BMIC<ZRT_HASH_BMIC(sample, id),sample> container;
    sample val(1, "John", 30);

    container.Set(val);

    // Check if the element was added
    sample ret = container.TryCopy<Tag_id>(1);
    EXPECT_EQ(ret.name, "John");
}

TEST(BMIC, UpdateElement) {
    zrt::BMIC<ZRT_HASH_BMIC(sample, id),sample> container;
    sample val(1, "John", 30);
    container.Set(val);

    sample new_val(1, "Johnny", 31);
    container.Update<Tag_id>(1, new_val);

    // Check if the element was updated
    sample ret {};
    if (container.TryCopy<Tag_id>(1, ret)) {
        EXPECT_EQ(ret.id, 1);
        EXPECT_EQ(ret.name, "Johnny");
        EXPECT_EQ(ret.age, 31);
    }

    auto opt_ret = container.Copy<Tag_id>(1);
    if (opt_ret) {
        EXPECT_EQ(opt_ret->id, 1);
        EXPECT_EQ(opt_ret->name, "Johnny");
        EXPECT_EQ(opt_ret->age, 31);
    }
}

TEST(BMIC, AddElement2) {
    zrt::BMIC<ZRT_HASHED_BMIC(1, sample, id),sample> container;
    sample val(1, "John", 30);

    container.Set(val);

    // Check if the element was added
    sample ret = container.TryCopy<TagPrimeKey>(1);
    EXPECT_EQ(ret.name, "John");
}

TEST(BMIC, UpdateElement2) {
    zrt::BMIC<ZRT_HASHED_BMIC(1, sample, id),sample> container;
    sample val(1, "John", 30);
    container.Set(val);

    sample new_val(1, "Johnny", 31);
    container.Update<TagPrimeKey>(1, new_val);

    // Check if the element was updated
    sample ret {};
    if (container.TryCopy<TagPrimeKey>(1, ret)) {
        EXPECT_EQ(ret.id, 1);
        EXPECT_EQ(ret.name, "Johnny");
        EXPECT_EQ(ret.age, 31);
    }

    auto opt_ret = container.Copy<TagPrimeKey>(1);
    if (opt_ret) {
        EXPECT_EQ(opt_ret->id, 1);
        EXPECT_EQ(opt_ret->name, "Johnny");
        EXPECT_EQ(opt_ret->age, 31);
    }
}

struct TestSt {
    int hashed_index1;
    int hashed_index2;
    int ordered_index1;
    int ordered_index2;
};

std::ostream& operator<<(std::ostream& os, const TestSt& val) {
    os << "{" << val.hashed_index1 << ", " << val.hashed_index2 << ", " << val.ordered_index1 << ", " << val.ordered_index2 << "}";
    return os;
}

TEST(BMIC, UpdateElement_ChangeIndexField) {
    zrt::BMIC<ZRT_BMIC(TestSt,
        ZRT_BMI_HASHED(2, unique, TagHash, TestSt, hashed_index1, hashed_index2),
        ZRT_BMI_ORDERED(2, non_unique, TagOrder, TestSt, ordered_index1, ordered_index2))
    ,TestSt> container {};

    TestSt val {};

    zrt::fill_field(val.hashed_index1, 1);
    zrt::fill_field(val.hashed_index2, 2);
    zrt::fill_field(val.ordered_index1, 10);
    zrt::fill_field(val.ordered_index2, 20);
    container.Set(val);
    SPDLOG_INFO("{}", container.Dump<TagHash>());
    EXPECT_EQ(container.Dump<TagHash>(), "[{1, 2, 10, 20}]");

    // 插入重复值
    zrt::fill_field(val.ordered_index1, 30);
    container.Set(val);
    SPDLOG_INFO("{}", container.Dump<TagHash>());
    EXPECT_EQ(container.Dump<TagHash>(), "[{1, 2, 10, 20}]");

    // 更新重复值
    container.Update<TagHash>(std::make_tuple(val.hashed_index1, val.hashed_index2), val);
    SPDLOG_INFO("{}", container.Dump<TagOrder>());
    EXPECT_EQ(container.Dump<TagHash>(), "[{1, 2, 30, 20}]");

    // 增加一个元素
    zrt::fill_field(val.hashed_index2, 3);
    zrt::fill_field(val.ordered_index1, 40);
    container.Update<TagHash>(std::make_tuple(val.hashed_index1, val.hashed_index2), val);
    SPDLOG_INFO("{}", container.Dump<TagOrder>());
    EXPECT_EQ(container.Dump<TagOrder>(), "[{1, 2, 30, 20},{1, 3, 40, 20}]");

    // 修改非主键索引
    zrt::fill_field(val.ordered_index1, 10);
    container.Update<TagHash>(std::make_tuple(val.hashed_index1, val.hashed_index2), val);
    SPDLOG_INFO("{}", container.Dump<TagOrder>());
    EXPECT_EQ(container.Dump<TagOrder>(), "[{1, 3, 10, 20},{1, 2, 30, 20}]");

    // 索引变了就找不到原来的元素了, 相当于新索引对应的旧值改了
    zrt::fill_field(val.hashed_index2, 2);
    container.Update<TagHash>(std::make_tuple(val.hashed_index1, val.hashed_index2), val);
    SPDLOG_INFO("{}", container.Dump<TagOrder>());
    EXPECT_EQ(container.Dump<TagOrder>(), "[{1, 3, 10, 20},{1, 2, 10, 20}]");


    // zrt::fill_field(val.ordered_index1, 30);


    // sample new_val(1, "Johnny", 31);
    // container.Update<TagPrimeKey>(1, new_val);
    //
    // // Check if the element was updated
    // sample ret {};
    // if (container.TryCopy<TagPrimeKey>(1, ret)) {
    //     EXPECT_EQ(ret.id, 1);
    //     EXPECT_EQ(ret.name, "Johnny");
    //     EXPECT_EQ(ret.age, 31);
    // }
    //
    // auto opt_ret = container.Copy<TagPrimeKey>(1);
    // if (opt_ret) {
    //     EXPECT_EQ(opt_ret->id, 1);
    //     EXPECT_EQ(opt_ret->name, "Johnny");
    //     EXPECT_EQ(opt_ret->age, 31);
    // }
}

//
//TEST(zrt_multi_index_container_helper, CopyElement) {
//    sample_container container;
//    sample val(1, "John", 30);
//    SetBmic(container, val);
//
//    sample copied_val;
//    bool result = TryCopyBmicVal<Tag_id>(container, 1, copied_val);
//
//    // Check if the element was copied
//    EXPECT_TRUE(result);
//    EXPECT_EQ(copied_val.id, 1);
//    EXPECT_EQ(copied_val.name, "John");
//    EXPECT_EQ(copied_val.age, 30);
//}
//
//TEST(zrt_multi_index_container_helper, CopyElement2) {
//    sample_container container;
//    sample val(1, "John", 30);
//    SetBmic(container, val);
//
//    sample copied_val = TryCopyBmicVal<Tag_id, sample>(container, 1);
//
//    // Check if the element was copied
//    EXPECT_EQ(copied_val.id, 1);
//    EXPECT_EQ(copied_val.name, "John");
//    EXPECT_EQ(copied_val.age, 30);
//}
//
//TEST(zrt_multi_index_container_helper, CopyElement3) {
//    sample_container container;
//    sample val(1, "John", 30);
//    SetBmic(container, val);
//
//    boost::optional<sample> copied_val = TryCopyBmicVal<Tag_id, sample>(container, 1);
//
//    // Check if the element was copied
//    ASSERT_TRUE(copied_val);
//    EXPECT_EQ(copied_val->id, 1);
//    EXPECT_EQ(copied_val->name, "John");
//    EXPECT_EQ(copied_val->age, 30);
//}
//
//TEST(zrt_multi_index_container_helper, CopyNonExistentElement) {
//    sample_container container;
//    sample val(1, "John", 30);
//    SetBmic(container, val);
//
//    boost::optional<sample> copied_val = CopyBmicVal<Tag_id, sample>(container, 2);
//
//    // Check if the element was not found
//    EXPECT_FALSE(copied_val);
//}
