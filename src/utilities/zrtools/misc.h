//
// Created by dell on 2023/11/15.
//

#pragma once

#include <random>
#include <boost/algorithm/hex.hpp>
#include <boost/uuid/detail/md5.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#define ZRT_VAR2STR(A) #A

namespace zrt {
    template<typename T>
    T get_random(T low, T high) {
        static std::default_random_engine generator(std::random_device{}());
        static std::uniform_int_distribution<T> distribution(low, high);
        return distribution(generator);
    }

    template<typename T>
    T get_random() {
        return get_random(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    }

    std::string get_uuid() {
        boost::uuids::random_generator gen;
        return boost::uuids::to_string(gen());
    }

    std::string get_md5(const char * const buffer, size_t buffer_size)
    {
        std::string str_md5;
        boost::uuids::detail::md5 boost_md5;
        boost_md5.process_bytes(buffer, buffer_size);
        boost::uuids::detail::md5::digest_type digest;
        boost_md5.get_digest(digest);
        const auto char_digest = reinterpret_cast<const char*>(&digest);
        str_md5.clear();
        boost::algorithm::hex(char_digest,char_digest+sizeof(boost::uuids::detail::md5::digest_type), std::back_inserter(str_md5));
        return str_md5;
    }
}