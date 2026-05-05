//
// WAL 文件持久化
//
// 将 WAL 写入文件，支持崩溃恢复
// 基于 zrt::WalFileWriter/Reader 泛型实现
//

#pragma once

#include "wal_entry.h"
#include "zrtools/zrt_wal_file.h"

namespace gtrade {

// 使用 zrt 泛型 WAL 文件头
using WalFileHeader = zrt::WalFileHeader;
static_assert(sizeof(WalFileHeader) == 64, "WalFileHeader should be 64 bytes");

/**
 * WAL 文件写入器
 *
 * 使用 zrt::WalFileWriter 泛型实现
 */
using WalFileWriter = zrt::WalFileWriter<WalEntryType>;

/**
 * WAL 文件读取器
 *
 * 使用 zrt::WalFileReader 泛型实现
 */
using WalFileReader = zrt::WalFileReader<WalEntryType>;

}  // namespace gtrade
