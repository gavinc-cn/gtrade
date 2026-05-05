#pragma once
#include <fstream>
#include <functional>
#include <sstream>
#include "spdlog/spdlog.h"
#include "filesystem_compatible.h"

namespace zrt {

    class FileWriter {
    public:
        FileWriter(const std::string& file_path, bool append=false):
        m_file_path(file_path),
        m_append(append)
        {
        }
        ~FileWriter() {
            if (m_file.is_open()) {
                m_file.close();
            }
        }

        bool Exist() {
            return fs::exists(m_file_path);
        }

        bool EnsureDir() {
            fs::path parent_path = fs::path(m_file_path).parent_path();
            if (!fs::exists(parent_path)) {
                return fs::create_directories(parent_path);
            }
            return true;
        }

        bool Open() {
            if (m_file.is_open()) {
                return true;
            }
            if (m_file_path.empty()) {
                SPDLOG_ERROR("file path is empty: {}", m_file_path);
                return false;
            }
            if (!EnsureDir()) {
                SPDLOG_ERROR("create folder failed: {}", m_file_path);
                return false;
            }
            auto flag = m_append ? std::ios::app : std::ios::trunc;
            m_file.open(m_file_path, std::ios::out | flag);
            if (!m_file.is_open()) {
                SPDLOG_ERROR("open file failed, path: {}", m_file_path);
                return false;
            }
            return true;
        }

        template<typename... Args>
        bool SetHeader(const std::string& content, Args... args) {
            if (!Exist()) {
                return WriteFmt(content, args...);
            }
            return true;
        }

        bool WriteLine(const std::string& content) {
            if (!Open()) {
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

        const std::string& GetFilePath() {
            return m_file_path;
        }

    private:
        std::ofstream m_file;
        std::string m_file_path;
        bool m_append {};
    };

    class FileReader {
    public:
        explicit FileReader(const std::string& file_path) :
            m_file_path(file_path)
        {
        }

        ~FileReader() {
            if (m_file.is_open()) {
                m_file.close();
            }
        }

        bool Exists() const {
            return fs::exists(m_file_path);
        }

        bool Open() {
            if (m_file.is_open()) {
                return true;
            }
            if (m_file_path.empty() || !Exists()) {
                SPDLOG_ERROR("file({}) not exist", m_file_path);
                return false;
            }
            m_file.open(m_file_path, std::ios::in);
            if (!m_file.is_open()) {
                SPDLOG_ERROR("open file failed, path: {}", m_file_path);
                return false;
            }
            return true;
        }

        std::string Read() {
            if (!Open()) {
                SPDLOG_ERROR("file is not open, path: {}", m_file_path);
                return "";
            }

            std::stringstream buffer {};
            buffer << m_file.rdbuf();
            return buffer.str();
        }

        bool ForEachLine(const std::function<bool(const std::string&)>& func) {
            if (!Open()) {
                SPDLOG_ERROR("file is not open, path: {}", m_file_path);
                return false;
            }

            std::string line {};
            while (std::getline(m_file, line)) {
                if (!func(line)) {
                    return false;  // Stop iteration if function returns false
                }
            }
            return true;
        }

        // Reset file stream to beginning
        void Rewind() {
            if (m_file.is_open()) {
                m_file.clear();  // Clear any error flags
                m_file.seekg(0);  // Go to beginning of file
            }
        }

    private:
        std::ifstream m_file {};
        std::string m_file_path {};
    };
}