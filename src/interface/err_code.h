//
// Created by dell on 2025/3/17.
//

#pragma once

struct ErrorCode {
    enum {
        kNoError = 0,

        kJsonParseError,
        kStratEngineWithdrawEntrustError,
        kInvalidEntno,
        kEntnoNotFound,
        OkxOrderFailed,

        kUnknown,
        kCount
    };
};
