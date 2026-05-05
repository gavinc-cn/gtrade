#include "zrtools/file.h"
#include <string>
#include <stdarg.h>

namespace zrt
{
    File::File(const std::string& filename, std::ios_base::openmode mode, bool ensure_dir):
            filename_(filename), mode_(mode) {
        if (ensure_dir) {

        }
        file_.open(filename_, mode_);
        if (!file_.is_open()) {
            throw std::runtime_error("file init failed:" + filename);
        }
    }

    File::~File() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    bool File::isOpen() const {
        return file_.is_open();
    }

    bool File::read(std::string& content) {
        if (!isOpen()) {
            throw std::runtime_error("file is not open");
        }
        content.clear();
        std::string line;
        while (std::getline(file_, line)) {
            content.append(line).append("\n");
        }
        return true;
    }

    std::string File::read() {
        std::string content;
        if (read(content)) {
            return content;
        } else {
            throw std::runtime_error("read failed");
        }
    }

    bool File::write(const std::string& content) {
        if (!isOpen()) {
            throw std::runtime_error("file is not open");
        }
        file_ << content;
        return true;
    }
}
