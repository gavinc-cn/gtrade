//
// Created by dell on 2025/5/22.
//

#pragma once

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <iostream>
#include <cstring>
#include "spdlog/spdlog.h"

template<typename T>
bool WriteShm(const std::string& shm_name, const std::string& data_key, const T& src_data) {
    // 先尝试删除已有的共享内存
    boost::interprocess::shared_memory_object::remove(shm_name.c_str());
    try {
        // 创建共享内存并分配空间
        boost::interprocess::managed_shared_memory segment(boost::interprocess::create_only, shm_name.c_str(), 65536); // 64KB
        // 在共享内存中构造结构体实例
        T* data = segment.construct<T>(data_key.c_str())();
        *data = src_data;
    }
    catch (const boost::interprocess::interprocess_exception& e) {
        SPDLOG_ERROR("Writer error: {}", e.what());
        return false;
    }

    return true;
}


template<typename T>
bool ReadShm(const std::string& shm_name, const std::string& data_key, T& dst_data) {
    try {
        // 打开已有的共享内存
        boost::interprocess::managed_shared_memory segment(boost::interprocess::open_only, shm_name.c_str());
        // 查找对象
        std::pair<T*,size_t> res = segment.find<T>(data_key.c_str());
        if (res.first) {
            dst_data = *res.first;
        }
        else {
            SPDLOG_ERROR("Shared data not found!");
            return false;
        }
    }
    catch (const boost::interprocess::interprocess_exception& e) {
        SPDLOG_ERROR("Reader error: {}", e.what());
        return false;
    }
    return true;
}