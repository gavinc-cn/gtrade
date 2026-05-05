//
// Created by dell on 2025/5/1.
//

#pragma once

#include "zrt_bmic.h"

#define ZRT_BMI_ORDERED_INDEX(index_type,cls,member) \
    ZRT_BMI_ORDERED_1_PARAM_INDEX(index_type,Tag_##member,cls,member)

#define ZRT_BMI_ORDERED_1_PARAM_INDEX(index_type,index_tag,cls,member) \
    boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>, \
    BOOST_MULTI_INDEX_MEMBER(cls, decltype(cls::member), member)>

#define ZRT_BMI_ORDERED_2_PARAM_INDEX(index_type,index_tag,cls,member1,member2) \
boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>,   \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2)>,                  \
boost::multi_index::composite_key_compare<                                      \
zrt::my_less<decltype(cls::member1)>,                                           \
zrt::my_less<decltype(cls::member2)>>>

#define ZRT_BMI_ORDERED_3_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3) \
boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>,   \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3)>,                  \
boost::multi_index::composite_key_compare<                                      \
zrt::my_less<decltype(cls::member1)>,                                           \
zrt::my_less<decltype(cls::member2)>,                                           \
zrt::my_less<decltype(cls::member3)>>>

#define ZRT_BMI_ORDERED_4_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4) \
boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>,   \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4)>,                  \
boost::multi_index::composite_key_compare<                                      \
zrt::my_less<decltype(cls::member1)>,                                           \
zrt::my_less<decltype(cls::member2)>,                                           \
zrt::my_less<decltype(cls::member3)>,                                           \
zrt::my_less<decltype(cls::member4)>>>

#define ZRT_BMI_ORDERED_5_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5) \
boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>,   \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5)>,                  \
boost::multi_index::composite_key_compare<                                      \
zrt::my_less<decltype(cls::member1)>,                                           \
zrt::my_less<decltype(cls::member2)>,                                           \
zrt::my_less<decltype(cls::member3)>,                                           \
zrt::my_less<decltype(cls::member4)>,                                           \
zrt::my_less<decltype(cls::member5)>>>

#define ZRT_BMI_ORDERED_6_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5,member6) \
boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>,   \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member6),member6)>,                  \
boost::multi_index::composite_key_compare<                                      \
zrt::my_less<decltype(cls::member1)>,                                           \
zrt::my_less<decltype(cls::member2)>,                                           \
zrt::my_less<decltype(cls::member3)>,                                           \
zrt::my_less<decltype(cls::member4)>,                                           \
zrt::my_less<decltype(cls::member5)>,                                           \
zrt::my_less<decltype(cls::member6)>>>

#define ZRT_BMI_ORDERED_7_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5,member6,member7) \
boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>,   \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member6),member6),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member7),member7)>,                  \
boost::multi_index::composite_key_compare<                                      \
zrt::my_less<decltype(cls::member1)>,                                           \
zrt::my_less<decltype(cls::member2)>,                                           \
zrt::my_less<decltype(cls::member3)>,                                           \
zrt::my_less<decltype(cls::member4)>,                                           \
zrt::my_less<decltype(cls::member5)>,                                           \
zrt::my_less<decltype(cls::member6)>,                                           \
zrt::my_less<decltype(cls::member7)>>>

#define ZRT_BMI_ORDERED_8_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5,member6,member7,member8) \
boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>,   \
boost::multi_index::composite_key<cls,                                          \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member1),member1),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member2),member2),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member3),member3),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member4),member4),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member5),member5),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member6),member6),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member7),member7),                   \
BOOST_MULTI_INDEX_MEMBER(cls,decltype(cls::member8),member8)>,                  \
boost::multi_index::composite_key_compare<                                      \
zrt::my_less<decltype(cls::member1)>,                                           \
zrt::my_less<decltype(cls::member2)>,                                           \
zrt::my_less<decltype(cls::member3)>,                                           \
zrt::my_less<decltype(cls::member4)>,                                           \
zrt::my_less<decltype(cls::member5)>,                                           \
zrt::my_less<decltype(cls::member6)>,                                           \
zrt::my_less<decltype(cls::member7)>,                                           \
zrt::my_less<decltype(cls::member8)>>>

#define ZRT_BMI_ORDERED_9_PARAM_INDEX(index_type,index_tag,cls,member1,member2,member3,member4,member5,member6,member7,member8,member9) \
boost::multi_index::ordered_##index_type<boost::multi_index::tag<struct index_tag>,   \
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
boost::multi_index::composite_key_compare<                                      \
zrt::my_less<decltype(cls::member1)>,                                           \
zrt::my_less<decltype(cls::member2)>,                                           \
zrt::my_less<decltype(cls::member3)>,                                           \
zrt::my_less<decltype(cls::member4)>,                                           \
zrt::my_less<decltype(cls::member5)>,                                           \
zrt::my_less<decltype(cls::member6)>,                                           \
zrt::my_less<decltype(cls::member7)>,                                           \
zrt::my_less<decltype(cls::member8)>,                                           \
zrt::my_less<decltype(cls::member9)>>>

/*
 * 调用示例
 * ZRT_BMI_ORDERED(2,unique,TagPrimeKey,MyClass,member1,member2)
 * ZRT_BMI_ORDERED(3,non_unique,TagPrimeKey,MyClass,member1,member2,member3)
 */
#define ZRT_BMI_ORDERED(index_cnt, ...) ZRT_BMI_ORDERED_##index_cnt##_PARAM_INDEX(__VA_ARGS__)
/*
 * 调用示例
 * ZRT_ORDERED_BMIC(2,unique,MyClass,member1,member2)
 * ZRT_ORDERED_BMIC(3,non_unique,MyClass,member1,member2,member3)
 */
#define ZRT_ORDERED_BMIC(index_cnt, cls, ...) ZRT_BMIC(cls, ZRT_BMI_ORDERED(index_cnt, unique, TagPrimeKey, cls, __VA_ARGS__))

#define ZRT_BMI_ORDERED_UNIQUE_INDEX(...) ZRT_BMI_ORDERED_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_ORDERED_NON_UNIQUE_INDEX(...) ZRT_BMI_ORDERED_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_ORDERED_UNIQUE_1_PARAM_INDEX(...) ZRT_BMI_ORDERED_1_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_ORDERED_NON_UNIQUE_1_PARAM_INDEX(...) ZRT_BMI_ORDERED_1_PARAM_INDEX(non_unique,__VA_ARGS__)

#define ZRT_BMI_ORDERED_UNIQUE_2_PARAM_INDEX(...) ZRT_BMI_ORDERED_2_PARAM_INDEX(unique,__VA_ARGS__)
#define ZRT_BMI_ORDERED_NON_UNIQUE_2_PARAM_INDEX(...) ZRT_BMI_ORDERED_2_PARAM_INDEX(non_unique,__VA_ARGS__)
