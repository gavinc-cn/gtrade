#pragma once

#include <unistd.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <cstring>

/*
- Single direction queue, push from one size and pull from another size.
- ULTRAQ_BLOCK_BUFFER_SIZE must larger than a single data structure size, otherwise the push operation may block.
*/

#ifndef ULTRAQ_BLOCK_BUFFER_SIZE
#define ULTRAQ_BLOCK_BUFFER_SIZE 1024*1024
#endif // ULTRAQ_BLOCK_BUFFER_SIZE

namespace zrt
{

struct null_mutex
{
    void lock() {}
    void unlock() {}
    bool try_lock()
    {
        return true;
    }
};

template<typename Mutex>
class UltraQ
{
public:
    explicit UltraQ(size_t max_block_no=0);
    ~UltraQ();
    int64_t push(const void* data, size_t size);
    int64_t pull(void* data);
    bool is_full(size_t incoming_sz);
    bool is_empty();
    size_t get_size();
    size_t get_capacity();
    size_t cnt_block_used();
    size_t cnt_block_total();

private:
    int64_t push_impl(const void* data, size_t size);
    bool is_block_full(size_t incoming_sz) const;
    bool is_block_empty() const;

private:
    struct Header
    {
        uint64_t body_size;
        char body[0];
    };

    struct Block
    {
        Block(): next(nullptr), left_idx(0), right_idx(0) {}
        void reset() {
            next = nullptr;
            left_idx = 0;
            right_idx = 0;
        }
        Block* next;
        int64_t left_idx;
        int64_t right_idx;
        char buf[ULTRAQ_BLOCK_BUFFER_SIZE];
    };

    Block* m_head = nullptr;
    Block* m_tail = nullptr;
    Block* m_empty_head = nullptr;
    size_t m_max_block_no = 0;
    size_t m_used_block_cnt = 0;
    size_t m_empty_block_cnt = 0;
    size_t m_content_size = 0;
    Mutex m_lock{};
};

using UltraQ_MT = UltraQ<std::mutex>;
using UltraQ_ST = UltraQ<null_mutex>;



template <typename Mutex>
UltraQ<Mutex>::UltraQ(size_t max_block_no) : m_max_block_no(max_block_no)
{
    std::lock_guard<Mutex> lg(m_lock);
    m_head = new Block();
    ++m_used_block_cnt;
    m_tail = m_head;
}

template <typename Mutex>
UltraQ<Mutex>::~UltraQ()
{
    std::lock_guard<Mutex> lg(m_lock);
    while (m_head)
    {
        Block *cur = m_head;
        m_head = m_head->next;
        delete cur;
        --m_used_block_cnt;
    }
    while (m_empty_head)
    {
        Block *cur = m_empty_head;
        m_empty_head = m_empty_head->next;
        delete cur;
        --m_empty_block_cnt;
    }
}

template <typename Mutex>
int64_t UltraQ<Mutex>::push(const void *data, size_t size)
{
    if (is_full(size))
    {
        return 0;
    }
    if (size > ULTRAQ_BLOCK_BUFFER_SIZE - sizeof(Header))
    {
        return -1;
    }
    std::lock_guard<Mutex> lg(m_lock);
    if (is_block_full(size))
    {
        if (m_empty_head)
        {
            m_tail->next = m_empty_head;
            m_empty_head = m_empty_head->next;
            m_tail->next->reset();
            ++m_used_block_cnt;
            --m_empty_block_cnt;
        }
        else
        {
            m_tail->next = new Block();
            ++m_used_block_cnt;
        }
        m_tail = m_tail->next;
        return push_impl(data, size);
    }
    else
    {
        return push_impl(data, size);
    }
}

template <typename Mutex>
int64_t UltraQ<Mutex>::push_impl(const void *data, size_t size)
{
    Header header{};
    header.body_size = size;
    memcpy(&m_tail->buf[m_tail->right_idx], &header, sizeof(header));
    m_tail->right_idx += sizeof(header);
    memcpy(&m_tail->buf[m_tail->right_idx], data, size);
    m_tail->right_idx += size;
    m_content_size += size;
    return size;
}

template <typename Mutex>
int64_t UltraQ<Mutex>::pull(void *data)
{
    if (is_empty())
    {
        return 0;
    }
    std::lock_guard<Mutex> lg(m_lock);
    while (is_block_empty())
    {
        Block *cur = m_head;
        m_head = m_head->next;
        //        m_head->prev = nullptr;
        cur->next = m_empty_head;
        m_empty_head = cur;
        ++m_empty_block_cnt;
        --m_used_block_cnt;
    }
    Header *src = (Header *)&m_head->buf[m_head->left_idx];
    uint64_t body_size = src->body_size;
    //    memcpy(data, src, sizeof(Header));
    memcpy(data, src->body, body_size);
    m_head->left_idx += sizeof(Header) + body_size;
    m_content_size -= body_size;
    return body_size;
}

template <typename Mutex>
bool UltraQ<Mutex>::is_block_empty() const
{
    return m_head->left_idx == m_head->right_idx;
}

template <typename Mutex>
bool UltraQ<Mutex>::is_empty()
{
    std::lock_guard<Mutex> lg(m_lock);
    return m_head == m_tail and m_head->left_idx == m_tail->right_idx;
}

template <typename Mutex>
bool UltraQ<Mutex>::is_block_full(size_t incoming_sz) const
{
    return m_tail->right_idx + sizeof(Header) + incoming_sz >= ULTRAQ_BLOCK_BUFFER_SIZE;
}

template <typename Mutex>
bool UltraQ<Mutex>::is_full(size_t incoming_sz)
{
    std::lock_guard<Mutex> lg(m_lock);
    if (m_max_block_no)
    {
        return m_used_block_cnt == m_max_block_no and is_block_full(incoming_sz);
    }
    else
    {
        return false;
    }
}

template <typename Mutex>
size_t UltraQ<Mutex>::get_size()
{
    std::lock_guard<Mutex> lg(m_lock);
    return m_content_size;
}

template <typename Mutex>
size_t UltraQ<Mutex>::get_capacity()
{
    return cnt_block_total() * ULTRAQ_BLOCK_BUFFER_SIZE;
}

template <typename Mutex>
size_t UltraQ<Mutex>::cnt_block_used()
{
    std::lock_guard<Mutex> lg(m_lock);
    return m_used_block_cnt;
}

template <typename Mutex>
size_t UltraQ<Mutex>::cnt_block_total()
{
    std::lock_guard<Mutex> lg(m_lock);
    return m_used_block_cnt + m_empty_block_cnt;
}

}