//
// Created by dell on 2024/11/7.
//

#pragma once

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/optional.hpp>
#include "zrtools/zrt_compare.h"
#include "zrtools/stl_dump.h"

// index
#define ZRT_BMI_SEQ_INDEX boost::multi_index::sequenced<>

// container
#define ZRT_DECLARE_BMIC(name, cls, ...) using name = boost::multi_index::multi_index_container<cls,boost::multi_index::indexed_by<__VA_ARGS__>>

#define ZRT_BMIC(cls, ...) boost::multi_index::multi_index_container<cls,boost::multi_index::indexed_by<__VA_ARGS__>>

namespace zrt {
    template<typename Container, typename Val>
    void SetBmic(Container& container, Val& val) {
        container.emplace(val);
    }

    template<typename Index, typename Container, typename Key, typename Val>
    void UpdateBmic(Container& container, const Key& key, Val& val) {
        auto& index = container.template get<Index>();
        auto iter = index.find(key);
        if (iter != index.end()) {
            index.replace(iter, val);
        } else {
            index.emplace(val);
        }
    }

    template<typename Index, typename Container, typename Key, typename Val>
    bool TryCopyBmicVal(const Container& container, const Key& key, Val& val) {
        auto& index = container.template get<Index>();
        auto iter = index.find(key);
        if (iter != index.end()) {
            val = *iter;
            return true;
        } else {
            return false;
        }
    }

    template<int N, typename Container, typename Key, typename Val>
    bool TryCopyBmicVal(const Container& container, const Key& key, Val& val) {
        auto& index = container.template get<N>();
        auto iter = index.find(key);
        if (iter != index.end()) {
            val = *iter;
            return true;
        } else {
            return false;
        }
    }

    template<typename Index, typename Val, typename Container, typename Key>
    Val TryCopyBmicVal(const Container& container, const Key& key) {
        Val val {};
        TryCopyBmicVal<Index>(container, key, val);
        return val;
    }

    template<typename Index, typename Val, typename Container, typename Key>
    boost::optional<Val> CopyBmicVal(const Container& container, const Key& key) {
        Val val {};
        if (TryCopyBmicVal<Index>(container, key, val)) {
            return boost::optional<Val>(val);
        } else {
            return {};
        }
    }

    template<typename Index, typename Container, typename Func>
    void ForEachBmicConstVal(const Container& container, const Func& func) {
        const auto& index = container.template get<Index>();
        for (const auto& i: index) {
            func(i);
        }
    }

    template<typename Index, typename Container, typename Func>
    void ForEachBmicVal(Container& container, const Func& func) {
        auto& index = container.template get<Index>();
        for (auto& i: index) {
            func(i);
        }
    }

    template<typename Container, typename Val>
    class BMIC {
    public:
        void Set(const Val& val) {
            m_container.emplace(val);
        }

        template<typename Index, typename Key>
        bool Remove(const Key& key) {
            auto& index = m_container.template get<Index>();
            auto iter = index.find(key);
            if (iter != index.end()) {
                index.erase(iter);
                return true;
            } else {
                return false;
            }
        }

        template<typename Index, typename Key>
        void RemoveRange(const Key& key) {
            auto& index = m_container.template get<Index>();
            auto range = index.equal_range(key);
            m_container.erase(range.first, range.second);
        }

        template<typename Index, typename Key>
        void RemoveRange(const Key& key_less, const Key& key_greater) {
            auto& index = m_container.template get<Index>();
            auto range = index.range(key_less, key_greater);
            m_container.erase(range.first, range.second);
        }

        template<typename Index, typename Key>
        bool Update(const Key& key, const Val& val) {
            auto& index = m_container.template get<Index>();
            auto iter = index.find(key);
            if (iter != index.end()) {
                index.replace(iter, val);
                return true;
            } else {
                index.emplace(val);
                return false;
            }
        }

        // 通过回调批量修改已有元素, 需要是ordered索引
        template<typename Index, typename Key>
        int UpdateRange(const Key& key, const std::function<Val(Val)>& func) {
            auto& index = m_container.template get<Index>();
            auto range = index.equal_range(key);
            int ret_count = 0;
            for (auto iter = range.first; iter != range.second; ++iter) {
                Val val = func(*iter);
                index.replace(iter, val);
                ++ret_count;
            }
            return ret_count;
        }

        bool IsEmpty() {return m_container.empty();}

        template<typename Index, typename Key>
        bool Has(const Key& key) {
            auto& index = m_container.template get<Index>();
            return index.find(key) != index.end();
        }

        template<typename Index, typename Key>
        bool TryCopy(const Key& key, Val& val) const {
            auto& index = m_container.template get<Index>();
            auto iter = index.find(key);
            if (iter != index.end()) {
                val = *iter;
                return true;
            } else {
                return false;
            }
        }

        template<int N, typename Key>
        bool TryCopy(const Key& key, Val& val) const {
            auto& index = m_container.template get<N>();
            auto iter = index.find(key);
            if (iter != index.end()) {
                val = *iter;
                return true;
            } else {
                return false;
            }
        }

        template<typename Index, typename Key>
        Val TryCopy(const Key& key) const {
            Val val {};
            TryCopy<Index>(key, val);
            return val;
        }

        template<typename Index, typename Key>
        boost::optional<Val> Copy(const Key& key) const {
            Val val {};
            if (TryCopy<Index>(key, val)) {
                return boost::optional<Val>(val);
            } else {
                return {};
            }
        }

        template<typename Index>
        auto CBegin() const {
            return m_container.template get<Index>().cbegin();
        }

        template<typename Index>
        auto Begin() const {
            return m_container.template get<Index>().begin();
        }

        // 遍历整个容器
        template<typename Index>
        int ForEach(const std::function<void(const Val&)>& func) const {
            const auto& index = m_container.template get<Index>();
            int ret_count = 0;
            for (const auto& i: index) {
                func(i);
                ++ret_count;
            }
            return ret_count;
        }

        // 查找并遍历 key_less <= x <= key_greater 的元素
        template<typename Index, typename Key>
        int ForEach(const Key& key_less, const Key& key_greater, const std::function<void(const Val&)>& func) const {
            const auto& index = m_container.template get<Index>();
            auto range = index.range(key_less, key_greater);
            int ret_count = 0;
            for (auto iter = range.first; iter != range.second; ++iter) {
                func(*iter);
                ++ret_count;
            }
            return ret_count;
        }

        // 查找并遍历与key相等的元素
        template<typename Index, typename Key>
        int ForEach(const Key& key, const std::function<void(const Val&)>& func) const {
            const auto& index = m_container.template get<Index>();
            auto range = index.equal_range(key);
            int ret_count = 0;
            for (auto iter = range.first; iter != range.second; ++iter) {
                func(*iter);
                ++ret_count;
            }
            return ret_count;
        }

        size_t GetSize() {
            return m_container.size();
        }

        template<typename Index>
        std::string Dump() {
            std::string ret = "[";
            ForEach<Index>([&ret](const auto& x) {
                ret.append(zrt::to_str(x)).append(",");
            });
            if (ret.size() > 1) ret.pop_back();
            ret.append("]");
            return ret;
        }

    private:
        Container m_container {};
    };
}

