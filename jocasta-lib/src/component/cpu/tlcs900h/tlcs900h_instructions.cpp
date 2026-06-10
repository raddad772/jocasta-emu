#include "tlcs900h.h"

namespace TLCS900H {

static constexpr u32 masksz[5] = {0, 0xFF, 0xFFFF, 0, 0xFFFFFFFF};
static constexpr u32 maskmsb[5] = {0, 0x80, 0x8000, 0, 0x80000000};

bool core::condition(u8 code) const {
    switch (code & 15) {
        case 0: return false;
        case 1: return (regs.SR.S ^ regs.SR.V);
        case 2: return (regs.SR.Z | (regs.SR.S ^ regs.SR.V));
        case 3: return (regs.SR.C | regs.SR.Z);
        case 4: return regs.SR.V;
        case 5: return regs.SR.S;
        case 6: return regs.SR.Z;
        case 7: return regs.SR.C;
        case 8: return true;
        case 9: return !(regs.SR.S ^ regs.SR.V);
        case 10: return !(regs.SR.Z | (regs.SR.S ^ regs.SR.V));
        case 11: return !(regs.SR.C | regs.SR.Z);
        case 12: return !regs.SR.V;
        case 13: return !regs.SR.S;
        case 14: return !regs.SR.Z;
        case 15: return !regs.SR.C;
        nodefault;
    }
}

bool core::parity(u32 data, u8 sz) const {
    if (sz == 4) return false;
    data &= masksz[sz];
    if (sz == 2) data ^= data >> 8;
    data ^= data >> 4;
    data ^= data >> 2;
    data ^= data >> 1;
    return !(data & 1);
}

u32 core::ADD(u32 target, u32 source, u8 sz, u8 carry) {
    u32 mask = masksz[sz], msb = maskmsb[sz];
    target &= mask;
    source &= mask;
    u64 full = static_cast<u64>(target) + source + carry;
    u32 result = full & mask;
    u32 overflow = (target ^ result) & (source ^ result);
    regs.SR.C = +(full > mask);
    regs.SR.N = 0;
    regs.SR.V = overflow & msb ? 1 : 0;
    regs.SR.H = (sz == 4) ? 0 : +(((target & 0xF) + (source & 0xF) + carry) > 0xF);
    regs.SR.Z = result == 0;
    regs.SR.S = result & msb ? 1 : 0;
    return result;
}

u32 core::SUB(u32 target, u32 source, u8 sz, u8 carry) {
    u32 mask = masksz[sz], msb = maskmsb[sz];
    target &= mask;
    source &= mask;
    u32 result = (target - source - carry) & mask;
    u32 overflow = (target ^ result) & (source ^ target);
    regs.SR.C = +(static_cast<u64>(source) + carry > target);
    regs.SR.N = 1;
    regs.SR.V = overflow & msb ? 1 : 0;
    regs.SR.H = (sz == 4) ? 0 : +(((source & 0xF) + carry) > (target & 0xF));
    regs.SR.Z = result == 0;
    regs.SR.S = result & msb ? 1 : 0;
    return result;
}

u32 core::AND(u32 target, u32 source, u8 sz) {
    u32 mask = masksz[sz];
    u32 result = (target & source) & mask;
    regs.SR.C = 0;
    regs.SR.N = 0;
    regs.SR.V = parity(result, sz);
    regs.SR.H = 1;
    regs.SR.Z = result == 0;
    regs.SR.S = result & maskmsb[sz] ? 1 : 0;
    return result;
}

u32 core::OR(u32 target, u32 source, u8 sz) {
    u32 mask = masksz[sz];
    u32 result = (target | source) & mask;
    regs.SR.C = 0;
    regs.SR.N = 0;
    regs.SR.V = parity(result, sz);
    regs.SR.H = 0;
    regs.SR.Z = result == 0;
    regs.SR.S = result & maskmsb[sz] ? 1 : 0;
    return result;
}

u32 core::XOR(u32 target, u32 source, u8 sz) {
    u32 mask = masksz[sz];
    u32 result = (target ^ source) & mask;
    regs.SR.C = 0;
    regs.SR.N = 0;
    regs.SR.V = parity(result, sz);
    regs.SR.H = 0;
    regs.SR.Z = result == 0;
    regs.SR.S = result & maskmsb[sz] ? 1 : 0;
    return result;
}

u32 core::INC(u32 target, u32 source, u8 sz) {
    u32 mask = masksz[sz], msb = maskmsb[sz];
    target &= mask;
    source &= mask;
    u32 result = (target + source) & mask;
    regs.SR.N = 0;
    regs.SR.V = (target ^ result) & (source ^ result) & msb ? 1 : 0;
    regs.SR.H = +(((target & 0xF) + (source & 0xF)) > 0xF);
    regs.SR.Z = result == 0;
    regs.SR.S = result & msb ? 1 : 0;
    return result;
}

u32 core::DEC(u32 target, u32 source, u8 sz) {
    u32 mask = masksz[sz], msb = maskmsb[sz];
    target &= mask;
    source &= mask;
    u32 result = (target - source) & mask;
    regs.SR.N = 1;
    regs.SR.V = (target ^ source) & (target ^ result) & msb ? 1 : 0;
    regs.SR.H = +((source & 0xF) > (target & 0xF));
    regs.SR.Z = result == 0;
    regs.SR.S = result & msb ? 1 : 0;
    return result;
}

template<bool do_debug>
void core::ins_NOP() {}

template<bool do_debug>
void core::ins_LD(const operand &dst, const operand &src) {
    store<do_debug>(dst, load<do_debug>(src));
}

template<bool do_debug>
void core::ins_PUSH(const operand &src) {
    regs.R[XSP].dw -= src.size;
    bus_write<do_debug>(src.size, regs.R[XSP].dw, load<do_debug>(src));
}

template<bool do_debug>
void core::ins_POP(const operand &dst) {
    u32 v = bus_read<do_debug>(dst.size, regs.R[XSP].dw);
    store<do_debug>(dst, v);
    regs.R[XSP].dw += dst.size;
}

template<bool do_debug>
void core::ins_ADD(const operand &dst, const operand &src) {
    store<do_debug>(dst, ADD(load<do_debug>(dst), load<do_debug>(src), dst.size, 0));
}

template<bool do_debug>
void core::ins_ADC(const operand &dst, const operand &src) {
    store<do_debug>(dst, ADD(load<do_debug>(dst), load<do_debug>(src), dst.size, regs.SR.C));
}

template<bool do_debug>
void core::ins_SUB(const operand &dst, const operand &src) {
    store<do_debug>(dst, SUB(load<do_debug>(dst), load<do_debug>(src), dst.size, 0));
}

template<bool do_debug>
void core::ins_SBC(const operand &dst, const operand &src) {
    store<do_debug>(dst, SUB(load<do_debug>(dst), load<do_debug>(src), dst.size, regs.SR.C));
}

template<bool do_debug>
void core::ins_AND(const operand &dst, const operand &src) {
    store<do_debug>(dst, AND(load<do_debug>(dst), load<do_debug>(src), dst.size));
}

template<bool do_debug>
void core::ins_OR(const operand &dst, const operand &src) {
    store<do_debug>(dst, OR(load<do_debug>(dst), load<do_debug>(src), dst.size));
}

template<bool do_debug>
void core::ins_XOR(const operand &dst, const operand &src) {
    store<do_debug>(dst, XOR(load<do_debug>(dst), load<do_debug>(src), dst.size));
}

template<bool do_debug>
void core::ins_CP(const operand &dst, const operand &src) {
    SUB(load<do_debug>(dst), load<do_debug>(src), dst.size, 0);
}

template<bool do_debug>
void core::ins_INC(const operand &dst, const operand &src) {
    u32 amount = load<do_debug>(src);
    if (!amount) amount = 8;
    if (dst.kind == OPK_REG && dst.size >= 2) {
        store<do_debug>(dst, load<do_debug>(dst) + amount);
    } else {
        store<do_debug>(dst, INC(load<do_debug>(dst), amount, dst.size));
    }
}

template<bool do_debug>
void core::ins_DEC(const operand &dst, const operand &src) {
    u32 amount = load<do_debug>(src);
    if (!amount) amount = 8;
    if (dst.kind == OPK_REG && dst.size >= 2) {
        store<do_debug>(dst, load<do_debug>(dst) - amount);
    } else {
        store<do_debug>(dst, DEC(load<do_debug>(dst), amount, dst.size));
    }
}

template<bool do_debug>
void core::ins_JP(const operand &src) {
    regs.PC = load<do_debug>(src);
    invalidate();
}

template<bool do_debug>
void core::ins_JR(const operand &src) {
    regs.PC = regs.PC + static_cast<i32>(load<do_debug>(src));
    invalidate();
}

template<bool do_debug>
void core::ins_CALL(const operand &src) {
    u32 address = load<do_debug>(src);
    push<do_debug>(4, regs.PC);
    regs.PC = address;
    invalidate();
}

template<bool do_debug>
void core::ins_CALR(const operand &src) {
    i32 displacement = static_cast<i32>(load<do_debug>(src));
    push<do_debug>(4, regs.PC);
    regs.PC = regs.PC + displacement;
    invalidate();
}

template<bool do_debug>
void core::ins_RET() {
    regs.PC = pop<do_debug>(4);
    invalidate();
}

template<bool do_debug>
void core::ins_RETD(const operand &src) {
    regs.PC = pop<do_debug>(4);
    regs.R[XSP].dw += static_cast<i32>(load<do_debug>(src));
    invalidate();
}

template<bool do_debug>
void core::ins_RETI() {
    store_SR(pop<do_debug>(2));
    regs.PC = pop<do_debug>(4);
    regs.INTNEST--;
    invalidate();
}

template<bool do_debug>
void core::ins_SWI(u8 vector) {
    do_interrupt<do_debug>((vector & 7) << 2);
}

template<bool do_debug>
void core::ins_HALT() {
    halted = true;
}

template<bool do_debug>
void core::ins_RCF() { regs.SR.C = 0; regs.SR.N = 0; regs.SR.H = 0; }
template<bool do_debug>
void core::ins_SCF() { regs.SR.C = 1; regs.SR.N = 0; regs.SR.H = 0; }
template<bool do_debug>
void core::ins_CCF() { regs.SR.C ^= 1; regs.SR.N = 0; regs.SR.H = 0; }
template<bool do_debug>
void core::ins_ZCF() { regs.SR.C = regs.SR.Z ^ 1; regs.SR.N = 0; regs.SR.H = 0; }

void core::set_rfp(u8 val) { regs.SR.RFP = val & 3; }
template<bool do_debug>
void core::ins_INCF() { set_rfp(regs.SR.RFP + 1); }
template<bool do_debug>
void core::ins_DECF() { set_rfp(regs.SR.RFP - 1); }
template<bool do_debug>
void core::ins_LDF(u8 val) { set_rfp(val); }

template<bool do_debug>
void core::ins_EI(u8 val) { regs.SR.IFF = val & 7; }

template<bool do_debug>
void core::ins_EX(const operand &dst, const operand &src) {
    u32 t = load<do_debug>(dst);
    store<do_debug>(dst, load<do_debug>(src));
    store<do_debug>(src, t);
}

static i32 sgn(u32 v, u8 sz) {
    switch (sz) {
        case 1: return static_cast<i8>(v);
        case 2: return static_cast<i16>(v);
        default: return static_cast<i32>(v);
    }
}

static u16 mirror16(u16 v) {
    v = (v >> 8) | (v << 8);
    v = ((v & 0xF0F0) >> 4) | ((v & 0x0F0F) << 4);
    v = ((v & 0xCCCC) >> 2) | ((v & 0x3333) << 2);
    v = ((v & 0xAAAA) >> 1) | ((v & 0x5555) << 1);
    return v;
}

operand core::expand_reg(const operand &r) const {
    if (r.size == 1) return op_reg(r.id & ~1, 2);
    return op_reg(r.id & ~3, 4);
}

u32 core::algorithm_rotated(u32 result, u8 sz) {
    result &= masksz[sz];
    regs.SR.N = 0;
    regs.SR.V = parity(result, sz);
    regs.SR.H = 0;
    regs.SR.Z = result == 0;
    regs.SR.S = result & maskmsb[sz] ? 1 : 0;
    return result;
}

template<bool do_debug>
void core::ins_MUL(const operand &dst, const operand &src) {
    u32 product = (load<do_debug>(dst) & masksz[dst.size]) * (load<do_debug>(src) & masksz[dst.size]);
    store<do_debug>(expand_reg(dst), product);
}

template<bool do_debug>
void core::ins_MULS(const operand &dst, const operand &src) {
    i32 product = sgn(load<do_debug>(dst), dst.size) * sgn(load<do_debug>(src), dst.size);
    store<do_debug>(expand_reg(dst), static_cast<u32>(product));
}

template<bool do_debug>
void core::ins_DIV(const operand &dst, const operand &src) {
    u8 sz = dst.size, bits = sz * 8;
    operand wide = expand_reg(dst);
    u32 tmask = masksz[sz], emask = masksz[sz * 2];
    u32 dividend = load<do_debug>(wide) & emask;
    u32 divisor = load<do_debug>(src) & tmask;
    u32 quotient, remainder;
    if (divisor) { quotient = dividend / divisor; remainder = dividend % divisor; }
    else { quotient = (~(dividend >> bits)) & tmask; remainder = dividend & tmask; }
    store<do_debug>(wide, ((remainder & tmask) << bits) | (quotient & tmask));
    regs.SR.V = (!divisor || ((remainder & emask) >> bits)) ? 1 : 0;
}

template<bool do_debug>
void core::ins_DIVS(const operand &dst, const operand &src) {
    u8 sz = dst.size, bits = sz * 8;
    operand wide = expand_reg(dst);
    u32 tmask = masksz[sz], emask = masksz[sz * 2];
    i32 dividend = sgn(load<do_debug>(wide), sz * 2);
    i32 divisor = sgn(load<do_debug>(src), sz);
    u32 quotient, remainder;
    if (divisor) { quotient = static_cast<u32>((dividend / divisor)); remainder = static_cast<u32>((dividend % divisor)); }
    else { quotient = (~(static_cast<u32>((dividend >> bits)))) & tmask; remainder = static_cast<u32>(dividend) & tmask; }
    store<do_debug>(wide, ((remainder & tmask) << bits) | (quotient & tmask));
    regs.SR.V = (!divisor || ((remainder & emask) >> bits)) ? 1 : 0;
}

template<bool do_debug>
void core::ins_RLC(const operand &dst, const operand &amount) {
    u8 sz = dst.size; u32 msb = maskmsb[sz], mask = masksz[sz];
    u32 result = load<do_debug>(dst) & mask;
    u32 length = load<do_debug>(amount) & 15; if (!length) length = 16;
    prefetch<do_debug>((length >> 2) << 1);
    for (u32 n = 0; n < length; n++) {
        regs.SR.C = (result & msb) ? 1 : 0;
        result = ((result << 1) | regs.SR.C) & mask;
    }
    store<do_debug>(dst, algorithm_rotated(result, sz));
}

template<bool do_debug>
void core::ins_RRC(const operand &dst, const operand &amount) {
    u8 sz = dst.size; u32 msb = maskmsb[sz], mask = masksz[sz];
    u32 result = load<do_debug>(dst) & mask;
    u32 length = load<do_debug>(amount) & 15; if (!length) length = 16;
    prefetch<do_debug>((length >> 2) << 1);
    for (u32 n = 0; n < length; n++) {
        regs.SR.C = result & 1;
        result = ((regs.SR.C ? msb : 0) | (result >> 1)) & mask;
    }
    store<do_debug>(dst, algorithm_rotated(result, sz));
}

template<bool do_debug>
void core::ins_RL(const operand &dst, const operand &amount) {
    u8 sz = dst.size; u32 msb = maskmsb[sz], mask = masksz[sz];
    u32 result = load<do_debug>(dst) & mask;
    u32 length = load<do_debug>(amount) & 15; if (!length) length = 16;
    prefetch<do_debug>((length >> 2) << 1);
    for (u32 n = 0; n < length; n++) {
        u32 cf = (result & msb) ? 1 : 0;
        result = ((result << 1) | regs.SR.C) & mask;
        regs.SR.C = cf;
    }
    store<do_debug>(dst, algorithm_rotated(result, sz));
}

template<bool do_debug>
void core::ins_RR(const operand &dst, const operand &amount) {
    u8 sz = dst.size; u32 msb = maskmsb[sz], mask = masksz[sz];
    u32 result = load<do_debug>(dst) & mask;
    u32 length = load<do_debug>(amount) & 15; if (!length) length = 16;
    prefetch<do_debug>((length >> 2) << 1);
    for (u32 n = 0; n < length; n++) {
        u32 cf = result & 1;
        result = ((regs.SR.C ? msb : 0) | (result >> 1)) & mask;
        regs.SR.C = cf;
    }
    store<do_debug>(dst, algorithm_rotated(result, sz));
}

template<bool do_debug>
void core::ins_SLA(const operand &dst, const operand &amount) {
    u8 sz = dst.size; u32 msb = maskmsb[sz], mask = masksz[sz];
    u32 result = load<do_debug>(dst) & mask;
    u32 length = load<do_debug>(amount) & 15; if (!length) length = 16;
    prefetch<do_debug>((length >> 2) << 1);
    for (u32 n = 0; n < length; n++) {
        regs.SR.C = (result & msb) ? 1 : 0;
        result = (result << 1) & mask;
    }
    store<do_debug>(dst, algorithm_rotated(result, sz));
}

template<bool do_debug>
void core::ins_SLL(const operand &dst, const operand &amount) {
    ins_SLA<do_debug>(dst, amount);
}

template<bool do_debug>
void core::ins_SRA(const operand &dst, const operand &amount) {
    u8 sz = dst.size; u32 msb = maskmsb[sz], mask = masksz[sz];
    u32 result = load<do_debug>(dst) & mask;
    u32 length = load<do_debug>(amount) & 15; if (!length) length = 16;
    prefetch<do_debug>((length >> 2) << 1);
    for (u32 n = 0; n < length; n++) {
        u32 sign = result & msb;
        regs.SR.C = result & 1;
        result = (result >> 1);
        if (sign) result |= msb;
    }
    store<do_debug>(dst, algorithm_rotated(result, sz));
}

template<bool do_debug>
void core::ins_SRL(const operand &dst, const operand &amount) {
    u8 sz = dst.size; u32 mask = masksz[sz];
    u32 result = load<do_debug>(dst) & mask;
    u32 length = load<do_debug>(amount) & 15; if (!length) length = 16;
    prefetch<do_debug>((length >> 2) << 1);
    for (u32 n = 0; n < length; n++) {
        regs.SR.C = result & 1;
        result = (result >> 1) & mask;
    }
    store<do_debug>(dst, algorithm_rotated(result, sz));
}

template<bool do_debug>
void core::ins_BIT(const operand &src, const operand &off) {
    u32 bit = load<do_debug>(off);
    regs.SR.N = 0;
    regs.SR.V = 0;
    regs.SR.H = 1;
    regs.SR.Z = ((load<do_debug>(src) >> bit) & 1) ? 0 : 1;
    regs.SR.S = 0;
}

template<bool do_debug>
void core::ins_RES(const operand &dst, const operand &off) {
    u32 bit = load<do_debug>(off) & (dst.size * 8 - 1);
    store<do_debug>(dst, load<do_debug>(dst) & ~(1u << bit));
}

template<bool do_debug>
void core::ins_SET(const operand &dst, const operand &off) {
    u32 bit = load<do_debug>(off) & (dst.size * 8 - 1);
    store<do_debug>(dst, load<do_debug>(dst) | (1u << bit));
}

template<bool do_debug>
void core::ins_CHG(const operand &dst, const operand &off) {
    u32 bit = load<do_debug>(off) & (dst.size * 8 - 1);
    store<do_debug>(dst, load<do_debug>(dst) ^ (1u << bit));
}

template<bool do_debug>
void core::ins_TSET(const operand &dst, const operand &off) {
    u32 bit = load<do_debug>(off) & (dst.size * 8 - 1);
    u32 v = load<do_debug>(dst);
    regs.SR.N = 0;
    regs.SR.V = 0;
    regs.SR.H = 1;
    regs.SR.Z = ((v >> bit) & 1) ? 0 : 1;
    regs.SR.S = 0;
    store<do_debug>(dst, v | (1u << bit));
}

template<bool do_debug>
void core::ins_CPL(const operand &dst) {
    store<do_debug>(dst, ~load<do_debug>(dst));
    regs.SR.N = 1;
    regs.SR.H = 1;
}

template<bool do_debug>
void core::ins_NEG(const operand &dst) {
    store<do_debug>(dst, SUB(0, load<do_debug>(dst), dst.size, 0));
}

template<bool do_debug>
void core::ins_EXTZ(const operand &dst) {
    operand low = op_reg(dst.id, dst.size == 4 ? 2 : 1);
    store<do_debug>(dst, load<do_debug>(low));
}

template<bool do_debug>
void core::ins_EXTS(const operand &dst) {
    operand low = op_reg(dst.id, dst.size == 4 ? 2 : 1);
    store<do_debug>(dst, static_cast<u32>(sgn(load<do_debug>(low), low.size)));
}

template<bool do_debug>
void core::ins_DAA(const operand &dst) {
    u8 input = load<do_debug>(dst) & 0xFF;
    u8 val = input;
    if (regs.SR.C || val > 0x99) val += regs.SR.N ? static_cast<u8>((-0x60)) : 0x60;
    if (regs.SR.H || (val & 0x0F) > 0x09) val += regs.SR.N ? static_cast<u8>((-0x06)) : 0x06;
    if (regs.SR.N == 0) regs.SR.C = regs.SR.C | (val < input ? 1 : 0);
    if (regs.SR.N == 1) regs.SR.C = regs.SR.C | (val > input ? 1 : 0);
    regs.SR.V = parity(val, 1);
    regs.SR.H = ((val ^ input) & 0x10) ? 1 : 0;
    regs.SR.Z = val == 0;
    regs.SR.S = (val & 0x80) ? 1 : 0;
    store<do_debug>(dst, val);
}

template<bool do_debug>
void core::ins_PAA(const operand &dst) {
    u32 v = load<do_debug>(dst);
    store<do_debug>(dst, v + (v & 1));
}

template<bool do_debug>
void core::ins_MIRR(const operand &dst) {
    store<do_debug>(dst, mirror16(load<do_debug>(dst)));
}

template<bool do_debug>
void core::ins_MINC(u32 modulo, const operand &dst, const operand &src) {
    u32 result = load<do_debug>(dst);
    u32 number = load<do_debug>(src);
    if ((result & number) == number) result -= number;
    else result += modulo;
    store<do_debug>(dst, result);
}

template<bool do_debug>
void core::ins_MDEC(u32 modulo, const operand &dst, const operand &src) {
    u32 result = load<do_debug>(dst);
    u32 number = load<do_debug>(src);
    if ((result & number) == number) result += number;
    else result -= modulo;
    store<do_debug>(dst, result);
}

template<bool do_debug>
void core::ins_DJNZ(const operand &dst, const operand &off) {
    u32 result = (load<do_debug>(dst) - 1) & masksz[dst.size];
    store<do_debug>(dst, result);
    if (result == 0) return;
    prefetch<do_debug>(2);
    regs.PC = regs.PC + static_cast<i32>(load<do_debug>(off));
    invalidate();
    prefetch<do_debug>(2);
}

template<bool do_debug>
void core::ins_SCC(u8 code, const operand &dst) {
    store<do_debug>(dst, condition(code) ? 1 : 0);
}

template<bool do_debug>
void core::ins_BS1F(const operand &src) {
    u32 val = load<do_debug>(src) & 0xFFFF;
    for (u32 i = 0; i < 16; i++) {
        if ((val >> i) & 1) { regs.SR.V = 0; regs.set_reg8(0xE0, i); return; }
    }
    regs.SR.V = 1;
}

template<bool do_debug>
void core::ins_BS1B(const operand &src) {
    u32 val = load<do_debug>(src) & 0xFFFF;
    for (i32 i = 15; i >= 0; i--) {
        if ((val >> i) & 1) { regs.SR.V = 0; regs.set_reg8(0xE0, i); return; }
    }
    regs.SR.V = 1;
}

template<bool do_debug>
void core::ins_MULA(const operand &dst) {
    operand wide = expand_reg(dst);
    u32 xde = regs.reg32(0xE8);
    u32 xhl = regs.reg32(0xEC);
    i32 product = sgn(load<do_debug>(op_mem(xde, 2)), 2) * sgn(load<do_debug>(op_mem(xhl, 2)), 2);
    u32 source = load<do_debug>(wide);
    store<do_debug>(wide, source + static_cast<u32>(product));
    regs.set_reg32(0xEC, regs.reg32(0xEC) - 2);
    u32 result = load<do_debug>(wide);
    regs.SR.V = ((static_cast<u32>(product) ^ result) & (source ^ result) & 0x80000000u) ? 1 : 0;
    regs.SR.Z = result == 0;
    regs.SR.S = (result & 0x80000000u) ? 1 : 0;
}

template<bool do_debug>
void core::ins_LINK(const operand &dst, const operand &off) {
    regs.R[XSP].dw -= 4;
    bus_write<do_debug>(4, regs.R[XSP].dw, load<do_debug>(dst));
    store<do_debug>(dst, regs.R[XSP].dw);
    regs.R[XSP].dw += static_cast<i32>(load<do_debug>(off));
}

template<bool do_debug>
void core::ins_UNLK(const operand &dst) {
    regs.R[XSP].dw = load<do_debug>(dst);
    u32 v = bus_read<do_debug>(4, regs.R[XSP].dw);
    store<do_debug>(dst, v);
    regs.R[XSP].dw += 4;
}

template<bool do_debug>
void core::ins_ANDCF(const operand &src, const operand &off) {
    u32 o = load<do_debug>(off);
    if (src.size == 1 && off.kind == OPK_REG && (o & 8)) return;
    u32 bit = o & (src.size * 8 - 1);
    regs.SR.C = regs.SR.C & ((load<do_debug>(src) >> bit) & 1);
}

template<bool do_debug>
void core::ins_ORCF(const operand &src, const operand &off) {
    u32 o = load<do_debug>(off);
    if (src.size == 1 && off.kind == OPK_REG && (o & 8)) return;
    u32 bit = o & (src.size * 8 - 1);
    regs.SR.C = regs.SR.C | ((load<do_debug>(src) >> bit) & 1);
}

template<bool do_debug>
void core::ins_XORCF(const operand &src, const operand &off) {
    u32 o = load<do_debug>(off);
    if (src.size == 1 && off.kind == OPK_REG && (o & 8)) return;
    u32 bit = o & (src.size * 8 - 1);
    regs.SR.C = regs.SR.C ^ ((load<do_debug>(src) >> bit) & 1);
}

template<bool do_debug>
void core::ins_LDCF(const operand &src, const operand &off) {
    u32 o = load<do_debug>(off);
    if (src.size == 1 && off.kind == OPK_REG && (o & 8)) return;
    u32 bit = o & (src.size * 8 - 1);
    regs.SR.C = (load<do_debug>(src) >> bit) & 1;
}

template<bool do_debug>
void core::ins_STCF(const operand &dst, const operand &off) {
    u32 o = load<do_debug>(off);
    if (dst.size == 1 && (o & 8)) return;
    u32 mask_bit = static_cast<u32>((1ull << (o & 63)));
    store<do_debug>(dst, (load<do_debug>(dst) & ~mask_bit) | (regs.SR.C ? mask_bit : 0));
}

static u8 reg3_long(u8 code) {
    static const u8 wl[8] = {0xE0, 0xE4, 0xE8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC};
    return wl[code & 7];
}

template<bool do_debug>
void core::ins_LDI(u8 sz, i32 adjust) {
    u8 t_id = (OP & 7) == 5 ? 0xF0 : 0xE8;
    u8 s_id = (OP & 7) == 5 ? 0xF4 : 0xEC;
    u32 taddr = regs.reg32(t_id);
    u32 saddr = regs.reg32(s_id);
    store<do_debug>(op_mem(taddr, sz), load<do_debug>(op_mem(saddr, sz)));
    regs.set_reg32(s_id, saddr + adjust);
    regs.set_reg32(t_id, taddr + adjust);
    u32 bc = (regs.reg16(0xE4) - 1) & 0xFFFF;
    regs.set_reg16(0xE4, bc);
    regs.SR.N = 0;
    regs.SR.V = bc != 0 ? 1 : 0;
    regs.SR.H = 0;
}

template<bool do_debug>
void core::ins_LDIR(u8 sz, i32 adjust) {
    do {
        prefetch<do_debug>(14);
        ins_LDI<do_debug>(sz, adjust);
    } while (regs.reg16(0xE4) != 0);
}

template<bool do_debug>
void core::ins_CPI(u8 sz, i32 adjust) {
    u8 cf = regs.SR.C;
    u8 s_id = reg3_long(OP);
    u32 saddr = regs.reg32(s_id);
    operand acc = op_reg(0xE0, sz);
    SUB(load<do_debug>(acc), load<do_debug>(op_mem(saddr, sz)), sz, 0);
    regs.set_reg32(s_id, saddr + adjust);
    u32 bc = (regs.reg16(0xE4) - 1) & 0xFFFF;
    regs.set_reg16(0xE4, bc);
    regs.SR.C = cf;
    regs.SR.V = bc != 0 ? 1 : 0;
}

template<bool do_debug>
void core::ins_CPIR(u8 sz, i32 adjust) {
    do {
        prefetch<do_debug>(14);
        ins_CPI<do_debug>(sz, adjust);
    } while (regs.reg16(0xE4) != 0 && !regs.SR.Z);
}

template<bool do_debug>
void core::ins_RLD(const operand &mem) {
    u8 a = regs.reg8(0xE0);
    u8 v = load<do_debug>(mem);
    u8 newA = (a & 0xF0) | ((v >> 4) & 0x0F);
    u8 newM = static_cast<u8>(((v << 4) & 0xF0)) | (a & 0x0F);
    regs.set_reg8(0xE0, newA);
    store<do_debug>(mem, newM);
    regs.SR.N = 0;
    regs.SR.V = parity(newA, 1);
    regs.SR.H = 0;
    regs.SR.Z = newA == 0;
    regs.SR.S = (newA & 0x80) ? 1 : 0;
}

template<bool do_debug>
void core::ins_RRD(const operand &mem) {
    u8 a = regs.reg8(0xE0);
    u8 v = load<do_debug>(mem);
    u8 newA = (a & 0xF0) | (v & 0x0F);
    u8 newM = static_cast<u8>(((a << 4) & 0xF0)) | ((v >> 4) & 0x0F);
    regs.set_reg8(0xE0, newA);
    store<do_debug>(mem, newM);
    regs.SR.N = 0;
    regs.SR.V = parity(newA, 1);
    regs.SR.H = 0;
    regs.SR.Z = newA == 0;
    regs.SR.S = (newA & 0x80) ? 1 : 0;
}

template void core::ins_NOP<false>();
template void core::ins_NOP<true>();
template void core::ins_LD<false>(const operand &dst, const operand &src);
template void core::ins_LD<true>(const operand &dst, const operand &src);
template void core::ins_PUSH<false>(const operand &src);
template void core::ins_PUSH<true>(const operand &src);
template void core::ins_POP<false>(const operand &dst);
template void core::ins_POP<true>(const operand &dst);
template void core::ins_ADD<false>(const operand &dst, const operand &src);
template void core::ins_ADD<true>(const operand &dst, const operand &src);
template void core::ins_ADC<false>(const operand &dst, const operand &src);
template void core::ins_ADC<true>(const operand &dst, const operand &src);
template void core::ins_SUB<false>(const operand &dst, const operand &src);
template void core::ins_SUB<true>(const operand &dst, const operand &src);
template void core::ins_SBC<false>(const operand &dst, const operand &src);
template void core::ins_SBC<true>(const operand &dst, const operand &src);
template void core::ins_AND<false>(const operand &dst, const operand &src);
template void core::ins_AND<true>(const operand &dst, const operand &src);
template void core::ins_OR<false>(const operand &dst, const operand &src);
template void core::ins_OR<true>(const operand &dst, const operand &src);
template void core::ins_XOR<false>(const operand &dst, const operand &src);
template void core::ins_XOR<true>(const operand &dst, const operand &src);
template void core::ins_CP<false>(const operand &dst, const operand &src);
template void core::ins_CP<true>(const operand &dst, const operand &src);
template void core::ins_INC<false>(const operand &dst, const operand &src);
template void core::ins_INC<true>(const operand &dst, const operand &src);
template void core::ins_DEC<false>(const operand &dst, const operand &src);
template void core::ins_DEC<true>(const operand &dst, const operand &src);
template void core::ins_JP<false>(const operand &src);
template void core::ins_JP<true>(const operand &src);
template void core::ins_JR<false>(const operand &src);
template void core::ins_JR<true>(const operand &src);
template void core::ins_CALL<false>(const operand &src);
template void core::ins_CALL<true>(const operand &src);
template void core::ins_CALR<false>(const operand &src);
template void core::ins_CALR<true>(const operand &src);
template void core::ins_RET<false>();
template void core::ins_RET<true>();
template void core::ins_RETD<false>(const operand &src);
template void core::ins_RETD<true>(const operand &src);
template void core::ins_RETI<false>();
template void core::ins_RETI<true>();
template void core::ins_SWI<false>(u8 vector);
template void core::ins_SWI<true>(u8 vector);
template void core::ins_HALT<false>();
template void core::ins_HALT<true>();
template void core::ins_RCF<false>();
template void core::ins_RCF<true>();
template void core::ins_SCF<false>();
template void core::ins_SCF<true>();
template void core::ins_CCF<false>();
template void core::ins_CCF<true>();
template void core::ins_ZCF<false>();
template void core::ins_ZCF<true>();
template void core::ins_INCF<false>();
template void core::ins_INCF<true>();
template void core::ins_DECF<false>();
template void core::ins_DECF<true>();
template void core::ins_LDF<false>(u8 val);
template void core::ins_LDF<true>(u8 val);
template void core::ins_EI<false>(u8 val);
template void core::ins_EI<true>(u8 val);
template void core::ins_EX<false>(const operand &dst, const operand &src);
template void core::ins_EX<true>(const operand &dst, const operand &src);
template void core::ins_MUL<false>(const operand &dst, const operand &src);
template void core::ins_MUL<true>(const operand &dst, const operand &src);
template void core::ins_MULS<false>(const operand &dst, const operand &src);
template void core::ins_MULS<true>(const operand &dst, const operand &src);
template void core::ins_DIV<false>(const operand &dst, const operand &src);
template void core::ins_DIV<true>(const operand &dst, const operand &src);
template void core::ins_DIVS<false>(const operand &dst, const operand &src);
template void core::ins_DIVS<true>(const operand &dst, const operand &src);
template void core::ins_RLC<false>(const operand &dst, const operand &amount);
template void core::ins_RLC<true>(const operand &dst, const operand &amount);
template void core::ins_RRC<false>(const operand &dst, const operand &amount);
template void core::ins_RRC<true>(const operand &dst, const operand &amount);
template void core::ins_RL<false>(const operand &dst, const operand &amount);
template void core::ins_RL<true>(const operand &dst, const operand &amount);
template void core::ins_RR<false>(const operand &dst, const operand &amount);
template void core::ins_RR<true>(const operand &dst, const operand &amount);
template void core::ins_SLA<false>(const operand &dst, const operand &amount);
template void core::ins_SLA<true>(const operand &dst, const operand &amount);
template void core::ins_SLL<false>(const operand &dst, const operand &amount);
template void core::ins_SLL<true>(const operand &dst, const operand &amount);
template void core::ins_SRA<false>(const operand &dst, const operand &amount);
template void core::ins_SRA<true>(const operand &dst, const operand &amount);
template void core::ins_SRL<false>(const operand &dst, const operand &amount);
template void core::ins_SRL<true>(const operand &dst, const operand &amount);
template void core::ins_BIT<false>(const operand &src, const operand &off);
template void core::ins_BIT<true>(const operand &src, const operand &off);
template void core::ins_RES<false>(const operand &dst, const operand &off);
template void core::ins_RES<true>(const operand &dst, const operand &off);
template void core::ins_SET<false>(const operand &dst, const operand &off);
template void core::ins_SET<true>(const operand &dst, const operand &off);
template void core::ins_CHG<false>(const operand &dst, const operand &off);
template void core::ins_CHG<true>(const operand &dst, const operand &off);
template void core::ins_TSET<false>(const operand &dst, const operand &off);
template void core::ins_TSET<true>(const operand &dst, const operand &off);
template void core::ins_CPL<false>(const operand &dst);
template void core::ins_CPL<true>(const operand &dst);
template void core::ins_NEG<false>(const operand &dst);
template void core::ins_NEG<true>(const operand &dst);
template void core::ins_EXTZ<false>(const operand &dst);
template void core::ins_EXTZ<true>(const operand &dst);
template void core::ins_EXTS<false>(const operand &dst);
template void core::ins_EXTS<true>(const operand &dst);
template void core::ins_DAA<false>(const operand &dst);
template void core::ins_DAA<true>(const operand &dst);
template void core::ins_PAA<false>(const operand &dst);
template void core::ins_PAA<true>(const operand &dst);
template void core::ins_MIRR<false>(const operand &dst);
template void core::ins_MIRR<true>(const operand &dst);
template void core::ins_MINC<false>(u32 modulo, const operand &dst, const operand &src);
template void core::ins_MINC<true>(u32 modulo, const operand &dst, const operand &src);
template void core::ins_MDEC<false>(u32 modulo, const operand &dst, const operand &src);
template void core::ins_MDEC<true>(u32 modulo, const operand &dst, const operand &src);
template void core::ins_DJNZ<false>(const operand &dst, const operand &off);
template void core::ins_DJNZ<true>(const operand &dst, const operand &off);
template void core::ins_SCC<false>(u8 code, const operand &dst);
template void core::ins_SCC<true>(u8 code, const operand &dst);
template void core::ins_BS1F<false>(const operand &src);
template void core::ins_BS1F<true>(const operand &src);
template void core::ins_BS1B<false>(const operand &src);
template void core::ins_BS1B<true>(const operand &src);
template void core::ins_MULA<false>(const operand &dst);
template void core::ins_MULA<true>(const operand &dst);
template void core::ins_LINK<false>(const operand &dst, const operand &off);
template void core::ins_LINK<true>(const operand &dst, const operand &off);
template void core::ins_UNLK<false>(const operand &dst);
template void core::ins_UNLK<true>(const operand &dst);
template void core::ins_ANDCF<false>(const operand &src, const operand &off);
template void core::ins_ANDCF<true>(const operand &src, const operand &off);
template void core::ins_ORCF<false>(const operand &src, const operand &off);
template void core::ins_ORCF<true>(const operand &src, const operand &off);
template void core::ins_XORCF<false>(const operand &src, const operand &off);
template void core::ins_XORCF<true>(const operand &src, const operand &off);
template void core::ins_LDCF<false>(const operand &src, const operand &off);
template void core::ins_LDCF<true>(const operand &src, const operand &off);
template void core::ins_STCF<false>(const operand &dst, const operand &off);
template void core::ins_STCF<true>(const operand &dst, const operand &off);
template void core::ins_LDI<false>(u8 sz, i32 adjust);
template void core::ins_LDI<true>(u8 sz, i32 adjust);
template void core::ins_LDIR<false>(u8 sz, i32 adjust);
template void core::ins_LDIR<true>(u8 sz, i32 adjust);
template void core::ins_CPI<false>(u8 sz, i32 adjust);
template void core::ins_CPI<true>(u8 sz, i32 adjust);
template void core::ins_CPIR<false>(u8 sz, i32 adjust);
template void core::ins_CPIR<true>(u8 sz, i32 adjust);
template void core::ins_RLD<false>(const operand &mem);
template void core::ins_RLD<true>(const operand &mem);
template void core::ins_RRD<false>(const operand &mem);
template void core::ins_RRD<true>(const operand &mem);

}
