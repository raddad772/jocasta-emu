// Toshiba TLCS900/H, used in NeoGeo Pocket

#pragma once
#include <cstdlib>
#include <cstdio>
#include "helpers/int.h"

namespace TLCS900H {
enum reg_names {
    XWA0 = 0,
    XBC0 = 1,
    XDE0 = 2,
    XHL0 = 3,
    XWA1 = 4,
    XBC1 = 5,
    XDE1 = 6,
    XHL1 = 7,
    XWA2 = 8,
    XBC2 = 9,
    XDE2 = 10,
    XHL2 = 11,
    XWA3 = 12,
    XBC3 = 13,
    XDE3 = 14,
    XHL3 = 15,
    XIX = 16,
    XIY = 17,
    XIZ = 18,
    XSP = 19
};

union RSPLIT {
    union {
        u8 b0, b1, b2, b3;;
    };
    union {
        u16 w_lo, w_high;
    };
    u32 dw{};
};

struct REGS {
    // 4x banks of 8x 32-bit registers
    RSPLIT R[20]; // 4 banks of 4, plus 4 more
    RSPLIT IR{}; // Instruction Register

    u16 INTNEST{};

    u32 get(u8 num, u8 sz) {
        if (num < 4) num += SR.RFP << 2;
        static constexpr u32 masksz[5] = { 0, 0xFF, 0xFFFF, 0, 0xFFFF'FFFF };
        return R[num].dw & masksz[sz];
    }

    void set(u8 num, u8 sz, u32 val) {
        static constexpr u32 masksz[5] = { 0, 0xFF, 0xFFFF, 0, 0xFFFF'FFFF };
        if (num < 4) num += SR.RFP << 2;
        u32 v = get(num, sz);
        R[num].dw = (v & ~masksz[sz]) | (val & masksz[sz]);
    }

    u32 PC{};
    union {
        struct {
            union {
                u8 F;
                u8 lo;
            };
            u8 hi;
        };
        struct {
            u16 C : 1;
            u16 N : 1;
            u16 V : 1;
            u16 : 1;
            u16 H : 1;
            u16 : 1;
            u16 Z : 1;
            u16 S : 1;
            u16 RFP : 3;
            u16 MAX : 1;
            u16 IFF : 3;
            u16 SYSM : 1;
        };
        u16 u{};
    } SR{};
    u8 F_{};
    u32 control_reg{};
};

struct DMA_CHANNEL {
    u32 source{};
    u8 mode{};
    u16 count{};
};

struct core {
    core();
    void reset();

    void run(i32 num_cycles);
    void decode_and_exec();
    u8 fetch8();
    u16 fetch16();
    u32 fetch24();
    u32 fetch32();

    u32 ADD(u32 a, u32 b, u8 sz, u32 carry_in);

    void CALL(u32 addr);
    template<u8 sz>void PUSH(u32 val) {
        regs.R[XSP].dw -= sz;
        if constexpr(sz == 1) write8(mem_ptr, regs.R[XSP].dw, val);
        if constexpr(sz == 2) write16(mem_ptr, regs.R[XSP].dw, val);
        if constexpr(sz == 4) write32(mem_ptr, regs.R[XSP].dw, val);
        NOGOHERE;
    }

    template<u8 sz>u32 POP() {
        u32 v;
        if constexpr(sz == 1) return read8(mem_ptr, regs.R[XSP].dw++);
        else if constexpr(sz == 2) v = read16(mem_ptr, regs.R[XSP].dw);
        else if constexpr(sz == 4) v = read32(mem_ptr, regs.R[XSP].dw);
        else {
            NOGOHERE;
        }
        regs.R[XSP].dw += sz;
        return v;
    }

    i32 my_cycles{};
    REGS regs;

    bool halted{};

    u8 cur_sz{};
    u8 cur_reg{};

    const u32 VECTOR_ADDR = 0x00FF'FF00; // TODO: ???

    void *mem_ptr{};
    void (*write8)(void *, u32 addr, u8 val){};
    void (*write16)(void *, u32 addr, u16 val){};
    void (*write32)(void *, u32 addr, u32 val){};
    u8 (*read8)(void *, u32 addr){};
    u16 (*read16)(void *, u32 addr){};
    u32 (*read32)(void *, u32 addr){};
    void (*idle)(void *, u32 num);
};
}