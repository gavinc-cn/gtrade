//
// Created by dell on 2023/1/13.
//

#pragma once

#include <unistd.h>
#include <iostream>
#include <memory>
#include <boost/noncopyable.hpp>

#ifndef BLOCK_BUFFER_SIZE
#define BLOCK_BUFFER_SIZE 1024*1024
#endif

namespace zrt
{

class UltraBuf: public boost::noncopyable
{
public:
    UltraBuf(size_t max_block_no = 0);
    ~UltraBuf();
    int64_t push(const void* data, size_t size);
    int64_t pull(void* data, size_t size);
    int64_t read(void* data, size_t size) const;
    bool is_full(size_t incoming_sz) const;
    bool is_empty(size_t size = 1) const;
    size_t get_size() const;
    size_t get_capacity() const;
    size_t cnt_block_used() const;
    size_t cnt_block_total() const;
    void print() const;
    void shrink_to_fit();
    std::string copy_content() const;

private:
    bool is_block_full(size_t incoming_sz) const;
    bool is_block_empty() const;

private:
    struct Block
    {
        Block(): next(nullptr), begin(0), end(0) {}
        void reset() {
            next = nullptr;
            begin = 0;
            end = 0;
        }
        Block* next;
        size_t begin;
        size_t end;
        char buf[BLOCK_BUFFER_SIZE];
    };
    Block* m_head = nullptr;
    Block* m_tail = nullptr;
    Block* m_empty_head = nullptr;
    size_t m_max_block_no = 0;
    size_t m_used_block_cnt = 0;
    size_t m_empty_block_cnt = 0;
    size_t m_content_size = 0;
};

}