//
// Created by dell on 2025/3/8.
//

#pragma once

#include "httplib.h"

inline void JoinUrl(std::string& endpoint, const Params& params) {
    if (params.empty()) {
        return;
    }
    bool first_param = true;
    for (const auto& p : params) {
        if (first_param) {
            endpoint.append("?").append(p.first).append("=").append(p.second);
            first_param = false;
        } else {
            endpoint.append("&").append(p.first).append("=").append(p.second);
        }
    }
}


static std::ostream& operator<<(std::ostream& os, const httplib::Headers& input)
{
    ZRT_PROCESS_DOUBLE_CONTAINER
}

static std::string GetHttpErrName(const httplib::Error err_code) {
    const static std::unordered_map<httplib::Error, std::string> err_map {
        {httplib::Error::Success, "Success"},
        {httplib::Error::Unknown, "Unknown"},
        {httplib::Error::Connection, "Connection"},
        {httplib::Error::BindIPAddress, "BindIPAddress"},
        {httplib::Error::Read, "Read"},
        {httplib::Error::Write, "Write"},
        {httplib::Error::ExceedRedirectCount, "ExceedRedirectCount"},
        {httplib::Error::Canceled, "Canceled"},
        {httplib::Error::SSLConnection, "SSLConnection"},
        {httplib::Error::SSLLoadingCerts, "SSLLoadingCerts"},
        {httplib::Error::SSLServerVerification, "SSLServerVerification"},
        {httplib::Error::SSLServerHostnameVerification, "SSLServerHostnameVerification"},
        {httplib::Error::UnsupportedMultipartBoundaryChars, "UnsupportedMultipartBoundaryChars"},
        {httplib::Error::Compression, "Compression"},
        {httplib::Error::ConnectionTimeout, "ConnectionTimeout"},
        {httplib::Error::ProxyConnection, "ProxyConnection"}
    };
    const auto iter = err_map.find(err_code);
    if (iter != err_map.end()) {
        if (err_code == httplib::Error::Success) {
            return iter->second;
        }
        return fmt::format("{}Error", iter->second);
    }
    return zrt::convert2str(static_cast<int>(err_code));
}