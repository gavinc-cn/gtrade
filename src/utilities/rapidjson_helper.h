//
// Created by dell on 2025/11/8.
//

#pragma once
#include <ostream>
#include "rapidjson/rapidjson.h"

// std::ostream& operator<<(std::ostream& os, const rapidjson::Value& st) {
    // rapidjson::StringBuffer buffer;
    // rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    // st.Accept(writer);
    // return buffer.GetString();
// }

namespace zrt {
    inline std::string to_str(const rapidjson::Value& st) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        st.Accept(writer);
        return buffer.GetString();
    }
}