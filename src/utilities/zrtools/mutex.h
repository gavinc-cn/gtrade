//
// Created by dell on 2024/6/24.
//

#pragma once

namespace zrt {

    struct null_mutex {
        void lock() {}

        void unlock() {}

        bool try_lock() {
            return true;
        }
    };
}