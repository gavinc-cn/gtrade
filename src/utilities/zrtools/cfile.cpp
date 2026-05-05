#include "zrtools/cfile.h"
#include <string>
#include <stdarg.h>

namespace zrt
{
    cfile::cfile(const std::string& filename, const std::string& mode, bool ensure_dir):
            filename_(filename), mode_(mode) {
        open(filename, mode, ensure_dir);
    }

    cfile::~cfile() {
        if (isOpen()) {
            fclose(file_);
            file_ = nullptr;
        }
    }

    bool cfile::open(const std::string& filename, const std::string& mode, bool ensure_dir) {
        filename_ = filename;
        mode_ = mode;
        if (!file_) {
            if (ensure_dir) {

            }
            file_ = fopen(filename_.c_str(), mode_.c_str());
            if (!file_) {
                throw std::runtime_error("file init failed:" + filename);
            }
        }
        return true;
    }

    bool cfile::isOpen() const {
        return file_;
    }

    void cfile::sync() const {
        fflush(file_);
    }

    bool cfile::read(char* content, size_t size) {
        if (!isOpen()) {
            throw std::runtime_error("file is not open");
        }
        size_t bytesRead = fread(content, size, 1, file_);
        if (bytesRead != size) {
            throw std::runtime_error("read failed");
        }
        return true;
    }

    bool cfile::read(std::string& content) {
        if (!isOpen()) {
            throw std::runtime_error("file is not open");
        }

        std::fseek(file_, 0, SEEK_END);
        long fileSize = std::ftell(file_);
        std::fseek(file_, 0, SEEK_SET);

        content.resize(fileSize, '\0');
        size_t bytesRead = fread(&content[0], 1, fileSize, file_);
        if (bytesRead != static_cast<size_t>(fileSize)) {
            throw std::runtime_error("read failed");
        }
        return true;
    }

    std::string cfile::read() {
        std::string content;
        if (read(content)) {
            return content;
        } else {
            throw std::runtime_error("read failed");
        }
    }

    bool cfile::write(const std::string& content) {
        if (!isOpen()) {
            throw std::runtime_error("file is not open");
        }
        if (fputs(content.c_str(), file_) == EOF) {
            throw std::runtime_error("write failed");
        }
        return true;
    }

    int cfile::write(const char* format, ...) {
        if (!isOpen()) {
            throw std::runtime_error("file is not open");
        }
        va_list args;
        va_start(args, format);
        int write_bytes = vfprintf(file_, format, args);
        va_end(args);
        return write_bytes;
    }
}
