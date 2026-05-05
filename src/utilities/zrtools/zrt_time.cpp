//
// Created by dell on 2025/3/9.
//


#include "zrtools/zrt_time-inl.h"
#include <iostream>

namespace zrt {

    // 10位epoch转换成时间字符串
    std::string get_time_str_local(int64_t epoch10, const std::string& format) {
        auto epoch_time = std::chrono::seconds{epoch10};
        auto sys_time = std::chrono::system_clock::from_time_t(epoch_time.count());
        std::time_t tt = std::chrono::system_clock::to_time_t(sys_time);
        char iso_time[26];
        std::strftime(iso_time, sizeof(iso_time), format.c_str(), std::localtime(&tt));
        return iso_time;
    }

    std::string GetTimeStrUTC(const std::string& format, int decimal_places) {
        boost::posix_time::ptime utc_time = boost::posix_time::microsec_clock::universal_time();
        std::ostringstream oss {};
        oss.imbue(std::locale(oss.getloc(), new boost::posix_time::time_facet(format.c_str())));
        oss << utc_time;
        std::string ret = oss.str();
        size_t pos = format.find("%f");
        if (pos != std::string::npos) {
            // 要删除的小数位数 + %f后面字符的长度
            size_t num_digits_to_erase = 6 - decimal_places + format.size() - pos - 2;
            if (num_digits_to_erase > 0) {
                ret.erase(ret.length() - num_digits_to_erase, num_digits_to_erase);
            }
            // 追加格式字符串的剩余部分
            ret.append(format.substr(pos + 2));
        }
        return ret;
    }

    std::string GetTimeStrUTC10(int64_t epoch, const std::string& format) {
        boost::posix_time::ptime utc_time = boost::posix_time::from_time_t(epoch);
        std::stringstream ss {};
        ss.imbue(std::locale(ss.getloc(), new boost::posix_time::time_facet(format.c_str())));
        ss << utc_time;
        return ss.str();
    }



}
