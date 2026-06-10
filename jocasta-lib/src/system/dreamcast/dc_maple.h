//
// Created by . on 4/2/26.
//

#pragma once
#include "helpers/int.h"

namespace DREAMCAST {
    struct core;
}


namespace DREAMCAST::MAPLE {
enum devices {
    DK_NONE,
    DK_CONTROLLER
};

struct PORT {
    explicit PORT(DREAMCAST::core *parent) : bus(parent) {}
    u32 read(bool& more);
    void write(u32 data);
    core *bus;
    PORT *port{};
    u32 num{};

    devices device_kind{};
    void *device_ptr{};

    u32 (*read_device)(void*, bool&){};
    void (*write_device)(void *,u32){};
};

struct core {
    explicit core(DREAMCAST::core *parent) : bus(parent), ports{PORT(parent), PORT(parent), PORT(parent), PORT(parent)} {}
    DREAMCAST::core *bus;

    void reset();
    u64 read_io(u32 addr, u8 sz, bool *success);
    void write_io(u32 addr, u8 sz, u64 val, bool *success);
    void dma_init();

    PORT ports[4];
    bool vblank_repeat_trigger{};
    u32 SB_MDSTAR{};  // 0x005F6C04
    u32 SB_MDTSEL{};  // 0x005F6C10
    u32 SB_MDEN{};  // 0x005F6C14
    u32 SB_MDST{};  // 0x005F6C18
    union {  // SB_MSYS
        struct {
            u32 delay_time : 4;
            u32 : 4;
            u32 sending_rate : 2;
            u32 : 2;
            u32 single_hard_trigger : 1;
            u32 : 3;
            u32 time_out_counter : 16;
        };
        u32 u{};
    } SB_MSYS;  // 0x005F6C80
    union {  // SB_MDAPRO
        struct {
            u32 bottom_addr : 7;
            u32 : 1;
            u32 top_addr : 7;
        };
        u32 u{};
    } SB_MDAPRO;  // 0x005F6C8C
    u32 SB_MMSEL{};  // 0x005F6CE8
};


}