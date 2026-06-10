#pragma once

#include "helpers/int.h"

namespace DREAMCAST {
struct core;
typedef u64 (*MEM_READ_FUNC)(void *ptr, u32 addr, bool *s);
typedef void (*MEM_WRITE_FUNC)(void *ptr, u32 addr, u64 val, bool *s);

struct MEM_ENTRY {
    MEM_READ_FUNC read{};
    MEM_WRITE_FUNC write{};
    void *ptr{};
};

struct MEM {
    MEM_ENTRY pages[2][5][512]{};
    MEM_ENTRY iopages[2][5][256]{};
};

}