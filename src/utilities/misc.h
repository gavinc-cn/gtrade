#pragma once

#include <zlib.h>
#include "yaml-cpp/yaml.h"
#include "string_keys.h"

namespace utils
{
    template <typename T>
    T round_n(T input, int n)
    {
        return std::round(input * pow(10, n)) / pow(10, n);
    }

    template <typename T>
    std::string roundToStr(T input, int n)
    {
        std::string ret_s = to_string(round_n(input, n));
        auto dot = ret_s.find('.');
        ret_s = ret_s.substr(0, dot + std::max(n+1, 2));
        return ret_s;
    }
}

inline int gzDecompress(const char *src, int srcLen, const char *dst, int dstLen) {
    z_stream strm;
    strm.zalloc = NULL;
    strm.zfree = NULL;
    strm.opaque = NULL;

    strm.avail_in = srcLen;
    strm.avail_out = dstLen;
    strm.next_in = (Bytef *) src;
    strm.next_out = (Bytef *) dst;

    int err = -1, ret = -1;
    err = inflateInit2(&strm, MAX_WBITS + 16);
    if (err == Z_OK) {
        err = inflate(&strm, Z_FINISH);
        if (err == Z_STREAM_END) {
            ret = strm.total_out;
        } else {
            inflateEnd(&strm);
            return err;
        }
    } else {
        inflateEnd(&strm);
        return err;
    }
    inflateEnd(&strm);
    return err;
}

inline void GetAccount(Account& account, const std::string& path, const std::string& account_id) {
    YAML::Node account_yml = YAML::LoadFile(path);
    account.id = account_id;
    account.key = account_yml[account_id][k_key].as<std::string>();
    account.secret = account_yml[account_id][k_secret].as<std::string>();
    account.passphrase = account_yml[account_id][k_passphrase].as<std::string>();
}
