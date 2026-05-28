#pragma once

#include "helpers/int.h"

namespace PS1 {
struct core;

typedef u32 (*MEM_READ_FUNC)(void *ptr, u32 addr);
typedef void (*MEM_WRITE_FUNC)(void *ptr, u32 addr, u32 val);


struct MEM_ENTRY {
    MEM_READ_FUNC read{};
    MEM_WRITE_FUNC write{};
    void *ptr{};
};

struct MEM {
    MEM_ENTRY pages[2][4][256]{}; // for general mem funcs, debug on/off + sz + bits 28-22
    MEM_ENTRY iopages[2][4][4096]{}; // for IO funcs, debug on/off + sz + bits 15-4

    u8 scratchpad[1024]{};

    //u32 scraptchad{}, MRAM{}, VRAM{}, BIOS{};

    u8 MRAM[2 * 1024 * 1024]{};
    u8 BIOS[512 * 1024]{};
    u8 BIOS_unpatched[512 * 1024]{};
    bool cache_isolated{};
};
}