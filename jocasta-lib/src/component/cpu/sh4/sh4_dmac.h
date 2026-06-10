#pragma once

#include "helpers/int.h"

namespace SH4 {
struct core;
struct DMAC;

struct CHANNEL {
    DMAC *dmac{};
    u32 num{};
    u32 SAR{}; // Source address
    u32 DAR{}; // Dest Address
    union {  // CHCR3
        struct {
            u32 DE : 1;
            u32 TE : 1;
            u32 IE : 1;
            u32 : 1;
            u32 TS : 3;
            u32 : 1;
            u32 RS : 4;
            u32 SM : 2;
            u32 DM : 2;
        };
        u32 u{};
    } CHCR; // Control
    u32 DMATCR{}; // Transfer count

    bool can_transfer();
    void end_transfer();
    void update_IRQs();
};

struct DMAC {
    explicit DMAC(core *parent);
    core *cpu;
    void reset();
    void write(u32 addr, u8 sz, u64 val, bool *success);
    u64 read(u32 addr, u8 sz, bool *success);

    CHANNEL channels[4];
    union {  // DMAOR
        struct {
            u32 DME : 1; // DMA Master Enable
            u32 NMIF : 1;
            u32 AE : 1;
            u32 : 12;
            u32 DDT : 1;
        };
        u32 u{};
    } DMAOR;  // 0xFFA00040
};
}
