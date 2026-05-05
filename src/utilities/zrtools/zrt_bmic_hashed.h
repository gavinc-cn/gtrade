//
// Created by dell on 2025/5/1.
//

#pragma once

#include "zrt_bmic.h"

// 单字段索引, 自动生成Tag
#define ZRT_BMI_HASHED_INDEX(index_type,cls,member) \
ZRT_BMI_HASHED_1_PARAM_INDEX(index_type,Tag_##member,cls,member)

// 单字段索引, 自定义Tag
#define ZRT_BMI_HASHED_1_PARAM_INDEX(index_type,index_tag,cls,member) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>, \
BOOST_MULTI_INDEX_MEMBER(cls, decltype(cls::member), member)>

#define ZRT_BMI_HASHED_2_PARAM_INDEX(index_type,index_tag,cls,member1,member2) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>,    \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2)>,                  \
boost::multi_index::composite_key_hash<                                         \
zrt::my_hash<decltype(cls::member1)>,                                           \
zrt::my_hash<decltype(cls::member2)>>,                                          \
boost::multi_index::composite_key_equal_to<                                     \
zrt::my_equal<decltype(cls::member1)>,                                          \
zrt::my_equal<decltype(cls::member2)>>>

#define ZRT_BMI_HASHED_3_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>,    \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3)>,                  \
boost::multi_index::composite_key_hash<                                         \
zrt::my_hash<decltype(cls::member1)>,                                           \
zrt::my_hash<decltype(cls::member2)>,                                           \
zrt::my_hash<decltype(cls::member3)>>,                                          \
boost::multi_index::composite_key_equal_to<                                     \
zrt::my_equal<decltype(cls::member1)>,                                          \
zrt::my_equal<decltype(cls::member2)>,                                          \
zrt::my_equal<decltype(cls::member3)>>>

#define ZRT_BMI_HASHED_4_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>,    \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4)>,                  \
boost::multi_index::composite_key_hash<                                         \
zrt::my_hash<decltype(cls::member1)>,                                           \
zrt::my_hash<decltype(cls::member2)>,                                           \
zrt::my_hash<decltype(cls::member3)>,                                           \
zrt::my_hash<decltype(cls::member4)>>,                                          \
boost::multi_index::composite_key_equal_to<                                     \
zrt::my_equal<decltype(cls::member1)>,                                          \
zrt::my_equal<decltype(cls::member2)>,                                          \
zrt::my_equal<decltype(cls::member3)>,                                          \
zrt::my_equal<decltype(cls::member4)>>>

#define ZRT_BMI_HASHED_5_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>,    \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5)>,                  \
boost::multi_index::composite_key_hash<                                         \
zrt::my_hash<decltype(cls::member1)>,                                           \
zrt::my_hash<decltype(cls::member2)>,                                           \
zrt::my_hash<decltype(cls::member3)>,                                           \
zrt::my_hash<decltype(cls::member4)>,                                           \
zrt::my_hash<decltype(cls::member5)>>,                                          \
boost::multi_index::composite_key_equal_to<                                     \
zrt::my_equal<decltype(cls::member1)>,                                          \
zrt::my_equal<decltype(cls::member2)>,                                          \
zrt::my_equal<decltype(cls::member3)>,                                          \
zrt::my_equal<decltype(cls::member4)>,                                          \
zrt::my_equal<decltype(cls::member5)>>>

#define ZRT_BMI_HASHED_6_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5,member6) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>,    \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member6),member6)>,                  \
boost::multi_index::composite_key_hash<                                         \
zrt::my_hash<decltype(cls::member1)>,                                           \
zrt::my_hash<decltype(cls::member2)>,                                           \
zrt::my_hash<decltype(cls::member3)>,                                           \
zrt::my_hash<decltype(cls::member4)>,                                           \
zrt::my_hash<decltype(cls::member5)>,                                           \
zrt::my_hash<decltype(cls::member6)>>,                                          \
boost::multi_index::composite_key_equal_to<                                     \
zrt::my_equal<decltype(cls::member1)>,                                          \
zrt::my_equal<decltype(cls::member2)>,                                          \
zrt::my_equal<decltype(cls::member3)>,                                          \
zrt::my_equal<decltype(cls::member4)>,                                          \
zrt::my_equal<decltype(cls::member5)>,                                          \
zrt::my_equal<decltype(cls::member6)>>>

#define ZRT_BMI_HASHED_7_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5,member6,member7) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>,    \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member6),member6),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member7),member7)>,                  \
boost::multi_index::composite_key_hash<                                         \
zrt::my_hash<decltype(cls::member1)>,                                           \
zrt::my_hash<decltype(cls::member2)>,                                           \
zrt::my_hash<decltype(cls::member3)>,                                           \
zrt::my_hash<decltype(cls::member4)>,                                           \
zrt::my_hash<decltype(cls::member5)>,                                           \
zrt::my_hash<decltype(cls::member6)>,                                           \
zrt::my_hash<decltype(cls::member7)>>,                                          \
boost::multi_index::composite_key_equal_to<                                     \
zrt::my_equal<decltype(cls::member1)>,                                          \
zrt::my_equal<decltype(cls::member2)>,                                          \
zrt::my_equal<decltype(cls::member3)>,                                          \
zrt::my_equal<decltype(cls::member4)>,                                          \
zrt::my_equal<decltype(cls::member5)>,                                          \
zrt::my_equal<decltype(cls::member6)>,                                          \
zrt::my_equal<decltype(cls::member7)>>>

#define ZRT_BMI_HASHED_8_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5,member6,member7,member8) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>,    \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member6),member6),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member7),member7),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member8),member8)>,                  \
boost::multi_index::composite_key_hash<                                         \
zrt::my_hash<decltype(cls::member1)>,                                           \
zrt::my_hash<decltype(cls::member2)>,                                           \
zrt::my_hash<decltype(cls::member3)>,                                           \
zrt::my_hash<decltype(cls::member4)>,                                           \
zrt::my_hash<decltype(cls::member5)>,                                           \
zrt::my_hash<decltype(cls::member6)>,                                           \
zrt::my_hash<decltype(cls::member7)>,                                           \
zrt::my_hash<decltype(cls::member8)>>,                                          \
boost::multi_index::composite_key_equal_to<                                     \
zrt::my_equal<decltype(cls::member1)>,                                          \
zrt::my_equal<decltype(cls::member2)>,                                          \
zrt::my_equal<decltype(cls::member3)>,                                          \
zrt::my_equal<decltype(cls::member4)>,                                          \
zrt::my_equal<decltype(cls::member5)>,                                          \
zrt::my_equal<decltype(cls::member6)>,                                          \
zrt::my_equal<decltype(cls::member7)>,                                          \
zrt::my_equal<decltype(cls::member8)>>>

#define ZRT_BMI_HASHED_9_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5,member6,member7,member8,member9) \
boost::multi_index::hashed_##index_type<boost::multi_index::tag<struct index_tag>,    \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member6),member6),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member7),member7),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member8),member8),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member9),member9)>,                  \
boost::multi_index::composite_key_hash<                                         \
zrt::my_hash<decltype(cls::member1)>,                                           \
zrt::my_hash<decltype(cls::member2)>,                                           \
zrt::my_hash<decltype(cls::member3)>,                                           \
zrt::my_hash<decltype(cls::member4)>,                                           \
zrt::my_hash<decltype(cls::member5)>,                                           \
zrt::my_hash<decltype(cls::member6)>,                                           \
zrt::my_hash<decltype(cls::member7)>,                                           \
zrt::my_hash<decltype(cls::member8)>,                                           \
zrt::my_hash<decltype(cls::member9)>>,                                          \
boost::multi_index::composite_key_equal_to<                                     \
zrt::my_equal<decltype(cls::member1)>,                                          \
zrt::my_equal<decltype(cls::member2)>,                                          \
zrt::my_equal<decltype(cls::member3)>,                                          \
zrt::my_equal<decltype(cls::member4)>,                                          \
zrt::my_equal<decltype(cls::member5)>,                                          \
zrt::my_equal<decltype(cls::member6)>,                                          \
zrt::my_equal<decltype(cls::member7)>,                                          \
zrt::my_equal<decltype(cls::member8)>,                                          \
zrt::my_equal<decltype(cls::member9)>>>

/*
 * 调用示例
 * ZRT_BMI_HASHED(2,unique,TagPrimeKey,MyClass,member1,member2)
 * ZRT_BMI_HASHED(3,non_unique,TagPrimeKey,MyClass,member1,member2,member3)
 */
#define ZRT_BMI_HASHED(index_cnt, ...) ZRT_BMI_HASHED_##index_cnt##_PARAM_INDEX(__VA_ARGS__)
/*
 * 调用示例
 * ZRT_HASHED_BMIC(2,unique,MyClass,member1,member2)
 * ZRT_HASHED_BMIC(3,non_unique,MyClass,member1,member2,member3)
 */
#define ZRT_HASHED_BMIC(index_cnt, cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED(index_cnt, unique, TagPrimeKey, cls, __VA_ARGS__))


#define ZRT_DECLARE_HASH_UNIQUE_2_PARAM_INDEX(cls,member1,member2) \
boost::multi_index::hashed_unique<boost::multi_index::tag<struct Tag_##member1##_##member2>,   \
boost::multi_index::composite_key<cls,                                        \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                 \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2)>>

#define ZRT_BMI_HASHED_UNIQUE_FUN_INDEX(cls,ret_t,mem_fun) boost::multi_index::hashed_unique<boost::multi_index::tag<struct Tag_##mem_fun>, BOOST_MULTI_INDEX_CONST_MEM_FUN(cls,ret_t, mem_fun)>

// 不用定义Tag类
#define ZRT_BMI_HASHED_UNIQUE_INDEX(...) ZRT_BMI_HASHED_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_INDEX(...) ZRT_BMI_HASHED_INDEX(non_unique,__VA_ARGS__)

// 自定义Tag类
#define ZRT_BMI_HASHED_UNIQUE_1_PARAM_INDEX(...) ZRT_BMI_HASHED_1_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_1_PARAM_INDEX(...) ZRT_BMI_HASHED_1_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_HASHED_UNIQUE_2_PARAM_INDEX(...) ZRT_BMI_HASHED_2_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_2_PARAM_INDEX(...) ZRT_BMI_HASHED_2_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_HASHED_UNIQUE_3_PARAM_INDEX(...) ZRT_BMI_HASHED_3_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_3_PARAM_INDEX(...) ZRT_BMI_HASHED_3_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_HASHED_UNIQUE_4_PARAM_INDEX(...) ZRT_BMI_HASHED_4_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_4_PARAM_INDEX(...) ZRT_BMI_HASHED_4_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_HASHED_UNIQUE_5_PARAM_INDEX(...) ZRT_BMI_HASHED_5_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_5_PARAM_INDEX(...) ZRT_BMI_HASHED_5_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_HASHED_UNIQUE_6_PARAM_INDEX(...) ZRT_BMI_HASHED_6_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_6_PARAM_INDEX(...) ZRT_BMI_HASHED_6_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_HASHED_UNIQUE_7_PARAM_INDEX(...) ZRT_BMI_HASHED_7_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_7_PARAM_INDEX(...) ZRT_BMI_HASHED_7_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_HASHED_UNIQUE_8_PARAM_INDEX(...) ZRT_BMI_HASHED_8_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_8_PARAM_INDEX(...) ZRT_BMI_HASHED_8_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_HASHED_UNIQUE_9_PARAM_INDEX(...) ZRT_BMI_HASHED_9_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_HASHED_NON_UNIQUE_9_PARAM_INDEX(...) ZRT_BMI_HASHED_9_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_HASH_BMIC(cls, member) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_INDEX(cls, member))

#define ZRT_HASH_BMIC_2(cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_2_PARAM_INDEX(TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_HASH_BMIC_3(cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_3_PARAM_INDEX(TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_HASH_BMIC_4(cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_4_PARAM_INDEX(TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_HASH_BMIC_5(cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_5_PARAM_INDEX(TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_HASH_BMIC_6(cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_6_PARAM_INDEX(TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_HASH_BMIC_7(cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_7_PARAM_INDEX(TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_HASH_BMIC_8(cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_8_PARAM_INDEX(TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_HASH_BMIC_9(cls, ...) ZRT_BMIC(cls, ZRT_BMI_HASHED_UNIQUE_9_PARAM_INDEX(TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_SEQ_HASH_BMIC(cls, member) ZRT_BMIC(cls, ZRT_BMI_SEQ_INDEX, ZRT_BMI_HASHED_UNIQUE_INDEX(cls, member))