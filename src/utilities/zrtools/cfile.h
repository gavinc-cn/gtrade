#pragma once

#include <iostream>
#include <fstream>

namespace zrt
{
    class cfile {
    public:
        cfile() = default;
        explicit cfile(const std::string& filename, const std::string& mode = "r", bool ensure_dir=true);
        ~cfile();
        bool open(const std::string& filename, const std::string& mode = "r", bool ensure_dir=true);
        bool isOpen() const;
        void sync() const;
        bool read(char* content, size_t size);
        bool read(std::string& content);
        std::string read();
        bool write(const std::string& content);
        int write(const char* , ...);
    private:
        std::string filename_;
        std::string mode_;
        FILE* file_ = nullptr;
    };
}



