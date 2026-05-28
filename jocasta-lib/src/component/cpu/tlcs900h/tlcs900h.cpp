//
// Created by . on 5/9/26.
//

#include "helpers/setbits.h"

#include "tlcs900h.h"

namespace TLCS900H {

static u8 decode_s_sz(u8 s) {
    return s ? 4 : 2;
}

// MOST but not ALL instructions are 16-bit
template<u8 num_z>
static u8 decode_zzz_sz(u8 z) {
    if constexpr (num_z < 2) {
        return z ? 2 : 1;
    }

    switch (z) {
        case 0b010:
                return 1;
        case 0b011:
                return 2;
        case 0b100:
                return 4;
        default:
            NOGOHERE;
    }
}

u8 core::fetch8() {
    u8 v = read8(mem_ptr, regs.PC);
    regs.PC++;
    return v;
}

u16 core::fetch16() {
    u16 v = read16(mem_ptr, regs.PC);
    regs.PC += 2;
    return v;
}

u32 core::fetch24() {
    u32 v = fetch16();
    v |= fetch8() << 16;
    return v;
}

u32 core::fetch32() {
    u32 v = read32(mem_ptr, regs.PC);
    regs.PC += 4;
    return v;
}

void core::CALL(u32 addr) {
    PUSH<4>(regs.PC);
    regs.PC = addr;
}
static constexpr u32 masksz[5] = { 0, 0xFF, 0xFFFF, 0xFF'FFFF, 0xFFFF'FFFF };
    static constexpr u32 msb_shift[5] = { 0, 7, 15, 23, 31};
u32 core::ADD(u32 a, u32 b, u8 sz, u32 carry_in) {
    u64 mask = masksz[sz];
    u64 out = (a & mask) + (b & mask) + carry_in;
    regs.SR.S = (out >> msb_shift[sz]) & 1;
    regs.SR.H = ((a & 0x0F) + (b & 0x0F) + carry_in) > 0x0F;
    regs.SR.V = out > mask;
    regs.SR.N = 0;
    regs.SR.C = out > mask;
    out &= mask;
    regs.SR.Z = out == 0;
    return out;
}

void core::decode_and_exec() {
    if (halted) {
        idle(mem_ptr, my_cycles);
        return;
    }
    regs.IR.dw = fetch8();

    switch (regs.IR.b0) {
        case 0x19: // POP F
            regs.SR.F = POP<1>();
            return;
        case 0x15: // POP A
            regs.set(0, 1, POP<1>());
            return;
        case 0x03: // POP SR
            regs.SR.u = POP<2>();
            return;
        case 0x02: // PUSH SR
            PUSH<2>(regs.SR.u);
            return;
        case 0x18: // PUSH F
            PUSH<1>(regs.SR.F);
            return;
        case 0x28: // PUSH A
            PUSH<1>(regs.get(0, 1));
            return;
        case 0x0E: // RET
            // Read 4 bytes into PC
            regs.PC = POP<4>();
        case 0x0F: {// RETD
            regs.PC = POP<4>();
            i32 v = sign_extend<16>(fetch16());
            regs.R[XSP].dw += v;
            return; }
        case 0x11: // SCF
            regs.SR.C = 1;
            regs.SR.N = 0;
            regs.SR.H = 0;
            return;
        case 0x07: // RETI
            regs.SR.u = POP<2>();
            regs.PC = POP<4>();
            regs.INTNEST--;
            return;
        case 0x10: // RCF
            regs.SR.C = 0;
            regs.SR.N = 0;
            regs.SR.H = 0;
        case 0x13: // ZCF
            regs.SR.C = regs.SR.Z ^ 1;
            regs.SR.N = 0;
            regs.SR.H ^= 1; // Guess
            return;
        case 0x00: // NOP
            return;
        case 0x12: // CCF
            regs.SR.C ^= 1;
            regs.SR.N = 0;
            regs.SR.H ^= 1; // guess
            return;
        case 0x0D: // DECF
            regs.SR.RFP = (regs.SR.RFP - 1) & 3;
            return;
        case 0x16: {
            // EX F, F'
            u8 f = regs.SR.F;
            regs.SR.F = regs.F_;
            regs.F_ = f;
            return; }
        case 0x05:
            halted = true;
            return;
        case 0x0C: // INCF
            regs.SR.RFP = (regs.SR.RFP + 1) & 3;
            return;
        case 0x1C: {// CALL #16
            u16 addr = fetch16();
            CALL(addr);
            return; }
        case 0x1D: {// CALL #24}
            u32 addr = fetch24();
            CALL(addr);
            return; }
        case 0x1E: {
            // CALR dst
            u32 d16 = sign_extend<16>(fetch16());
            PUSH<4>(regs.PC);
            regs.PC += d16;
            return; }
        case 0x1A: { // JP #16
            regs.PC = fetch16();
            return; }
        case 0x1B: { // JP #24
            regs.PC = fetch24();
            return; }
    }

#define OB(a) (0b ## a)
#define CMASK(a,b) if ((regs.IR.b0 & a) == b)
    CMASK(OB(11111101), OB(00001000)) { // LD/W 8.16 bit
        u8 sz = decode_zzz_sz<1>(getbit<1>(regs.IR.b0));
        u8 addr = fetch8();
        if (sz == 1) {
            u8 val = read8(mem_ptr, addr);
            write8(mem_ptr, addr, val);
        }
        else {
            u16 val = read16(mem_ptr, addr);
            write16(mem_ptr, addr, val);
        }
        return;
    }

    CMASK(OB(1000'1000), OB(0000'0000)) {
        u8 sz = decode_zzz_sz<3>(getbits<4,6>(regs.IR.b0));
        u32 val;
        if (sz == 1) val = fetch8();
        else if (sz == 2) val = fetch16();
        else val = fetch32();
        regs.set(getbits<0,2>(regs.IR.b0), sz, val);
        return;
    }

    CMASK(OB(1110'1000), OB(0100'1000)) {
        u8 r = getbits<0,2>(regs.IR.b0);
        u8 sz = decode_s_sz(getbit<4>(regs.IR.b0));
        u32 v;
        if (sz == 2) v = POP<2>();
        else v = POP<4>();
        regs.set(r, sz, v);
        return;
    }

    CMASK(OB(1110'1000), OB(0010'1000)) {
        u8 r = getbits<0,2>(regs.IR.b0);
        u8 sz = decode_s_sz(getbit<4>(regs.IR.b0));
        if (sz == 2) PUSH<2>(regs.get(r, 2));
        else PUSH<4>(regs.get(r, 4));
        return;
    }
    CMASK(OB(1111'1101), OB(00001001)) {
        u8 sz = decode_zzz_sz<1>(getbit<2>(regs.IR.b0));
        if (sz == 1)
            PUSH<1>(fetch8());
        else
            PUSH<2>(fetch16());
    }

    if (regs.IR.b0 >= 0xF8) { // SWI
        PUSH<4>(regs.PC);
        PUSH<2>(regs.SR.u);
        u8 num = getbits<0, 2>(regs.IR.b0) << 2;
        regs.PC = VECTOR_ADDR + num;
        return;
    }

    // At this point, opcodes that can be resolved with only 1 operand are complete.
    // Next let's work on the 16-bit fixed & mask opcodes.
    regs.IR.b1 = fetch8();
#undef CMASK
#define CMASK(a,b) if ((regs.IR.w_lo & a) == b)
    switch (regs.IR.w_lo) {

    }

    CMASK(OB(11001000'11111000), OB(11001000'10010000)) {
        // ADC R, r
        u8 sz = decode_zzz_sz<2>(getbits<4,5>(regs.IR.w_lo));
        u8 r = getbits<0,2>(regs.IR.w_lo);
        u8 R = getbits<8, 10>(regs.IR.w_lo);
        regs.set(R, sz, ADD(regs.get(R, sz), regs.get(r, sz), sz, regs.SR.C));
        return;
    }

    CMASK(OB(1100'1000'1111'1111), OB(1100'1000'1100'1001)) {
        // ADC r, #
        u8 sz = decode_zzz_sz<2>(getbits<4,5>(regs.IR.w_lo));
        u8 r = getbits<0,2>(regs.IR.w_lo);
        u32 v;
        if (sz == 1) v = fetch8();
        else if (sz == 2) v = fetch16();
        else v = fetch32();
        regs.set(r, sz, ADD(regs.get(r, sz), v, sz, regs.SR.C));
        return;
    }

    CMASK(OB(1000'0000'1111'1000), OB(1000'0000'1001'1000)) {
        // ADC R, mem
        //u32 v = mem_operand();

    }
#undef OB
#undef CMASK
}

void core::run(i32 num_cycles) {
    my_cycles += static_cast<i32>(num_cycles);
    while (my_cycles > 0) {
        decode_and_exec();
    }
}

core::core() {
}

void core::reset() {
    regs.R[XSP].dw = 0x100;
    regs.SR.SYSM = 1;
    regs.SR.MAX = 1;
    regs.SR.RFP = 0;

    setbits<0, 15>(regs.PC, read16(mem_ptr, 0x00FF'FF00));
    setbits<16, 23>(regs.PC, read8(mem_ptr, 0x00FF'FF02));
}

}