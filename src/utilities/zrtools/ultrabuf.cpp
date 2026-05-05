//
// Created by dell on 2023/1/13.
//

#include "ultrabuf.h"
#include <iostream>
#include <cstring>

namespace zrt
{

UltraBuf::UltraBuf(size_t max_block_no):
m_max_block_no(max_block_no)
{
    m_head = new Block();
    ++m_used_block_cnt;
    m_tail = m_head;
}

UltraBuf::~UltraBuf() {
    while(m_head) {
        Block* cur = m_head;
        m_head = m_head->next;
        delete cur;
        --m_used_block_cnt;
    }
    while (m_empty_head) {
        Block* cur = m_empty_head;
        m_empty_head = m_empty_head->next;
        delete cur;
        --m_empty_block_cnt;
    }
}

int64_t UltraBuf::push(const void *data, size_t size) {
    if (is_full(size)) {
        return EAGAIN;
    }
    size_t sz_to_write_total = size;
    size_t sz_to_write = sz_to_write_total;
    char* data_front = (char*)data;
    for (;;) {
        size_t space_left = BLOCK_BUFFER_SIZE - m_tail->end;
        if (is_block_full(sz_to_write)) {
            if (m_empty_head) {
                m_tail->next = m_empty_head;
                m_empty_head = m_empty_head->next;
                m_tail->next->reset();
                ++m_used_block_cnt;
                --m_empty_block_cnt;
            } else {
                m_tail->next = new Block();
                ++m_used_block_cnt;
            }

            memcpy(&m_tail->buf[m_tail->end], data_front, space_left);
            m_tail->end += space_left;
            m_tail = m_tail->next;
            sz_to_write -= space_left;
            data_front += space_left;
        } else {
            memcpy(&m_tail->buf[m_tail->end], data_front, sz_to_write);
            m_tail->end += sz_to_write;
            m_content_size += size;
            return size;
        }
    }
}

int64_t UltraBuf::read(void* data, size_t size) const {
    if (is_empty()) {
        return 0;
    }
    Block* head = m_head;
    size_t sz_to_read_total = std::min(size, m_content_size);
    size_t sz_to_read = sz_to_read_total;
    char* buf_to_write = (char*)data;
    for (;;) {
        size_t head_begin = head->begin;
        size_t head_end = head->end;
        size_t block_content_sz = head_end - head_begin;
        if (sz_to_read > block_content_sz) {
            memcpy(buf_to_write, &head->buf[head_begin], block_content_sz);
            sz_to_read -= block_content_sz;
            buf_to_write += block_content_sz;

            head = head->next;
        } else {
            memcpy(buf_to_write, &head->buf[head_begin], sz_to_read);
            return sz_to_read_total;
        }
    }
}

int64_t UltraBuf::pull(void* data, size_t size) {
    if (is_empty()) {
        return 0;
    }
    size_t sz_to_read_total = std::min(size, m_content_size);
    size_t sz_to_read = sz_to_read_total;
    char* buf_to_write = (char*)data;
    for (;;) {
        size_t block_content_sz = m_head->end - m_head->begin;
        if (sz_to_read > block_content_sz) {
            memcpy(buf_to_write, &m_head->buf[m_head->begin], block_content_sz);
            sz_to_read -= block_content_sz;
            buf_to_write += block_content_sz;
            m_head->begin += block_content_sz;

            Block* cur = m_head;
            m_head = m_head->next;
            cur->next = m_empty_head;
            m_empty_head = cur;
            ++m_empty_block_cnt;
            --m_used_block_cnt;
        } else {
            memcpy(buf_to_write, &m_head->buf[m_head->begin], sz_to_read);
            m_content_size -= sz_to_read_total;
            m_head->begin += sz_to_read;
            return sz_to_read_total;
        }
    }
}

bool UltraBuf::is_block_empty() const {
    return m_head->begin == m_head->end;
}

//bool UltraBuf::is_empty() const {
//    return m_head == m_tail and m_head->left_idx == m_tail->right_idx;
//}

bool UltraBuf::is_empty(size_t size) const {
    return m_content_size < size;
}

bool UltraBuf::is_block_full(size_t incoming_sz) const {
    return m_tail->end + incoming_sz >= BLOCK_BUFFER_SIZE;
}

bool UltraBuf::is_full(size_t incoming_sz) const {
    return m_max_block_no and m_used_block_cnt == m_max_block_no and is_block_full(incoming_sz);
}

size_t UltraBuf::get_size() const {
    return m_content_size;
}

size_t UltraBuf::get_capacity() const {
    return cnt_block_total() * BLOCK_BUFFER_SIZE;
}

size_t UltraBuf::cnt_block_used() const {
    return m_used_block_cnt;
}

size_t UltraBuf::cnt_block_total() const {
    return m_used_block_cnt + m_empty_block_cnt;
}

void UltraBuf::print() const {
    Block* block = m_head;
    while (block) {
        printf("begin=%lu,end=%lu,block=%p,next=%p\n", block->begin, block->end, block, block->next);
        block = block->next;
    }
}

void UltraBuf::shrink_to_fit() {
    while (m_empty_head) {
        Block* block = m_empty_head->next;
        delete m_empty_head;
        m_empty_head = block;
    }
    m_empty_block_cnt = 0;
}

    /**
     * 复制缓冲区内容为字符串
     * @return 缓冲区内容的副本
     */
    std::string UltraBuf::copy_content() const {
        std::string ret {};
        ret.reserve(m_content_size);

        Block* block = m_head;
        while (block && block->end > block->begin) {
            ret.append(&block->buf[block->begin], block->end - block->begin);
            block = block->next;
        }
        return ret;
    }


}


