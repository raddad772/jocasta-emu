#pragma once

#include "helpers/int.h"
#include <vector>

#define MFS_MAX 20


struct BUF {
    explicit BUF(size_t s) { allocate(s); };
    BUF() = default;
    ~BUF() { del(); }

    // Don't copy that floppy!
    BUF (const BUF&) = delete;
    BUF& operator=(const BUF&) = delete;

    // ...but do move it...
    BUF(BUF&& other) noexcept
    {
        ptr = other.ptr;
        size = other.size;
        other.ptr = nullptr;
        other.size = 0;
    }

    BUF& operator=(BUF&& other) noexcept {
        if (this != &other) {
            del();
            ptr = other.ptr;
            size = other.size;
            other.ptr = nullptr;
            other.size = 0;
        }
        return *this;
    }

    void *ptr{};
    size_t size{};
    u32 dirty{}; // used by external programs

    void allocate(size_t size);
    void del();
    void copy(const BUF* src);
};

struct read_file_buf {
    BUF buf{};
    char path[255]{};
    char name[255]{};
    u64 pos{};

    int read(const char *fname, const char *fpath);
};

struct multi_file_set {
    std::vector<read_file_buf> files{};

    void add(const char *fname, const char *fpath);
    void add_from_buf(const char *fname, const char *fpath, const void *ptr, size_t size);
    void clear();
};
