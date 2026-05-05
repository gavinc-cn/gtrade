#include <gtest/gtest.h>
#include "zrtools/zrt_bmic_hashed.h"
#include "zrtools/zrt_bmic_ordered.h"
#include "../test_interface.h"


using namespace zrt;
using namespace boost::multi_index;


ZRT_DECLARE_BMIC(
        sample_container,
        sample,
        ZRT_BMI_HASHED_UNIQUE_INDEX(sample, id),
        ZRT_BMI_ORDERED_UNIQUE_INDEX(sample, name)
);


TEST(zrt_multi_index_container_helper, AddElement) {
    sample_container container;
    sample val(1, "John", 30);

    SetBmic(container, val);
    
    // Check if the element was added
    auto& index = container.get<Tag_id>();
    auto iter = index.find(1);
    EXPECT_NE(iter, index.end());
}

TEST(zrt_multi_index_container_helper, UpdateElement) {
    sample_container container;
    sample val(1, "John", 30);
    SetBmic(container, val);
    
    sample new_val(1, "Johnny", 31);
    UpdateBmic<Tag_id>(container, 1, new_val);
    
    // Check if the element was updated
    auto& index = container.get<Tag_id>();
    auto iter = index.find(1);
    EXPECT_NE(iter, index.end());
    EXPECT_EQ(iter->name, "Johnny");
    EXPECT_EQ(iter->age, 31);
}

TEST(zrt_multi_index_container_helper, CopyElement) {
    sample_container container;
    sample val(1, "John", 30);
    SetBmic(container, val);
    
    sample copied_val;
    bool result = TryCopyBmicVal<Tag_id>(container, 1, copied_val);
    
    // Check if the element was copied
    EXPECT_TRUE(result);
    EXPECT_EQ(copied_val.id, 1);
    EXPECT_EQ(copied_val.name, "John");
    EXPECT_EQ(copied_val.age, 30);
}

TEST(zrt_multi_index_container_helper, CopyElement2) {
    sample_container container;
    sample val(1, "John", 30);
    SetBmic(container, val);
    
    sample copied_val = TryCopyBmicVal<Tag_id, sample>(container, 1);
    
    // Check if the element was copied
    EXPECT_EQ(copied_val.id, 1);
    EXPECT_EQ(copied_val.name, "John");
    EXPECT_EQ(copied_val.age, 30);
}

TEST(zrt_multi_index_container_helper, CopyElement3) {
    sample_container container;
    sample val(1, "John", 30);
    SetBmic(container, val);
    
    boost::optional<sample> copied_val = TryCopyBmicVal<Tag_id, sample>(container, 1);
    
    // Check if the element was copied
    ASSERT_TRUE(copied_val);
    EXPECT_EQ(copied_val->id, 1);
    EXPECT_EQ(copied_val->name, "John");
    EXPECT_EQ(copied_val->age, 30);
}

TEST(zrt_multi_index_container_helper, CopyNonExistentElement) {
    sample_container container;
    sample val(1, "John", 30);
    SetBmic(container, val);
    
    boost::optional<sample> copied_val = CopyBmicVal<Tag_id, sample>(container, 2);
    
    // Check if the element was not found
    EXPECT_FALSE(copied_val);
}
