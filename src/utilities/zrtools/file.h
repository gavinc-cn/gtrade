#pragma once

#include <iostream>
#include <fstream>
#include "spdlog/spdlog.h"

namespace zrt
{
    class File {
    public:
        explicit File(const std::string& filename, std::ios_base::openmode mode = std::ios_base::in, bool ensure_dir=true);
        ~File();
        bool isOpen() const;
        bool read(std::string& content);
        std::string read();
        bool write(const std::string& content);
    private:
        std::string filename_;
        std::ios_base::openmode mode_;
        std::fstream file_;
    };

    class FileWriter {
    public:
        FileWriter() = default;
        FileWriter(const std::string& file_path): m_file_path(file_path) {
        }
        ~FileWriter() {
            if (m_file.is_open()) {
                m_file.close();
            }
        }

        bool Open(const std::string& file_path) {
            m_file.open(file_path, std::ios::out | std::ios::trunc);
            if (m_file.is_open()) {
                return true;
            }
            SPDLOG_ERROR("open file failed, path: {}", file_path);
            return false;
        }

        bool WriteLine(const std::string& content) {
            if (!m_file.is_open() && (m_file_path.empty() || !Open(m_file_path))) {
                SPDLOG_ERROR("file is not open, path: {}", m_file_path);
                return false;
            }
            m_file << content << "\n";
            return true;
        }

        template<typename... Args>
        bool WriteFmt(const std::string& content, Args... args) {
            return WriteLine(fmt::format(content, args...));
        }

        void Flush() {
            m_file.flush();
        }

    private:
        std::ofstream m_file;
        std::string m_file_path;
    };
}



