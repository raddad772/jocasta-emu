#include "tlcs900h.h"

namespace TLCS900H {

static constexpr u8 ID_A = 0xE0;

static operand reg3(u8 code, u8 sz) {
    static const u8 b[8] = {0xE1, 0xE0, 0xE5, 0xE4, 0xE9, 0xE8, 0xED, 0xEC};
    static const u8 wl[8] = {0xE0, 0xE4, 0xE8, 0xEC, 0xF0, 0xF4, 0xF8, 0xFC};
    return op_reg(sz == 1 ? b[code & 7] : wl[code & 7], sz);
}

template<bool do_debug>
void core::undefined() {
    ins_SWI<do_debug>(2);
}

template<bool do_debug>
void core::decode() {
    u8 data = fetch8<do_debug>();
    OP = data;

    switch (data) {
        case 0x00: prefetch<do_debug>(4); return ins_NOP<do_debug>();
        case 0x01: return undefined<do_debug>();
        case 0x02: prefetch<do_debug>(4); push<do_debug>(2, load_SR()); return;
        case 0x03: prefetch<do_debug>(6); store_SR(pop<do_debug>(2)); return;
        case 0x04: return undefined<do_debug>();
        case 0x05: prefetch<do_debug>(12); return ins_HALT<do_debug>();
        case 0x06: { u8 n = fetch8<do_debug>(); prefetch<do_debug>(6); if ((n & 7) == 7) prefetch<do_debug>(2); return ins_EI<do_debug>(n & 7); }
        case 0x07: prefetch<do_debug>(18); ins_RETI<do_debug>(); return prefetch<do_debug>(2);
        case 0x08: { u32 a = fetch8<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(8); return ins_LD<do_debug>(op_mem(a, 1), op_imm(v, 1)); }
        case 0x09: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_PUSH<do_debug>(op_imm(v, 1)); }
        case 0x0a: { u32 a = fetch8<do_debug>(); u32 v = fetch16<do_debug>(); prefetch<do_debug>(10); return ins_LD<do_debug>(op_mem(a, 2), op_imm(v, 2)); }
        case 0x0b: { u32 v = fetch16<do_debug>(); prefetch<do_debug>(8); return ins_PUSH<do_debug>(op_imm(v, 2)); }
        case 0x0c: prefetch<do_debug>(4); return ins_INCF<do_debug>();
        case 0x0d: prefetch<do_debug>(4); return ins_DECF<do_debug>();
        case 0x0e: prefetch<do_debug>(14); ins_RET<do_debug>(); return prefetch<do_debug>(2);
        case 0x0f: { i32 d = sign_extend<16>(fetch16<do_debug>()); prefetch<do_debug>(18); ins_RETD<do_debug>(op_imm(static_cast<u32>(d), 2)); return prefetch<do_debug>(2); }
        case 0x10: prefetch<do_debug>(4); return ins_RCF<do_debug>();
        case 0x11: prefetch<do_debug>(4); return ins_SCF<do_debug>();
        case 0x12: prefetch<do_debug>(4); return ins_CCF<do_debug>();
        case 0x13: prefetch<do_debug>(4); return ins_ZCF<do_debug>();
        case 0x14: prefetch<do_debug>(4); return ins_PUSH<do_debug>(op_reg(ID_A, 1));
        case 0x15: prefetch<do_debug>(6); return ins_POP<do_debug>(op_reg(ID_A, 1));
        case 0x16: { prefetch<do_debug>(4); u8 t = load_F(); store_F(regs.F_); regs.F_ = t; return; }
        case 0x17: { u8 n = fetch8<do_debug>(); prefetch<do_debug>(4); return ins_LDF<do_debug>(n & 3); }
        case 0x18: prefetch<do_debug>(4); push<do_debug>(1, load_F()); return;
        case 0x19: prefetch<do_debug>(6); store_F(pop<do_debug>(1)); return;
        case 0x1a: { u32 a = fetch16<do_debug>(); prefetch<do_debug>(8); ins_JP<do_debug>(op_imm(a, 4)); return prefetch<do_debug>(2); }
        case 0x1b: { u32 a = fetch24<do_debug>(); prefetch<do_debug>(8); ins_JP<do_debug>(op_imm(a, 4)); return prefetch<do_debug>(2); }
        case 0x1c: { u32 a = fetch16<do_debug>(); prefetch<do_debug>(14); ins_CALL<do_debug>(op_imm(a, 4)); return prefetch<do_debug>(2); }
        case 0x1d: { u32 a = fetch24<do_debug>(); prefetch<do_debug>(16); ins_CALL<do_debug>(op_imm(a, 4)); return prefetch<do_debug>(2); }
        case 0x1e: { i32 d = sign_extend<16>(fetch16<do_debug>()); prefetch<do_debug>(16); ins_CALR<do_debug>(op_imm(static_cast<u32>(d), 2)); return prefetch<do_debug>(2); }
        case 0x1f: return undefined<do_debug>();
        default: break;
    }

    if (data <= 0x27) { u32 v = fetch8<do_debug>(); prefetch<do_debug>(4); return ins_LD<do_debug>(reg3(data, 1), op_imm(v, 1)); }
    if (data <= 0x2f) { prefetch<do_debug>(4); return ins_PUSH<do_debug>(reg3(data, 2)); }
    if (data <= 0x37) { u32 v = fetch16<do_debug>(); prefetch<do_debug>(6); return ins_LD<do_debug>(reg3(data, 2), op_imm(v, 2)); }
    if (data <= 0x3f) { prefetch<do_debug>(6); return ins_PUSH<do_debug>(reg3(data, 4)); }
    if (data <= 0x47) { u32 v = fetch32<do_debug>(); prefetch<do_debug>(10); return ins_LD<do_debug>(reg3(data, 4), op_imm(v, 4)); }
    if (data <= 0x4f) { prefetch<do_debug>(6); return ins_POP<do_debug>(reg3(data, 2)); }
    if (data <= 0x57) return undefined<do_debug>();
    if (data <= 0x5f) { prefetch<do_debug>(8); return ins_POP<do_debug>(reg3(data, 4)); }
    if (data <= 0x6f) {
        i32 d = sign_extend<8>(fetch8<do_debug>());
        prefetch<do_debug>(4);
        if (!condition(data & 15)) return;
        prefetch<do_debug>(4);
        ins_JR<do_debug>(op_imm(static_cast<u32>(d), 1));
        return prefetch<do_debug>(2);
    }
    if (data <= 0x7f) {
        i32 d = sign_extend<16>(fetch16<do_debug>());
        prefetch<do_debug>(4);
        if (!condition(data & 15)) return;
        prefetch<do_debug>(4);
        ins_JR<do_debug>(op_imm(static_cast<u32>(d), 2));
        return prefetch<do_debug>(2);
    }

    switch (data) {
        case 0xc7: { operand r = op_reg(fetch8<do_debug>(), 1); prefetch<do_debug>(2); return decode_reg<do_debug>(r); }
        case 0xd7: { operand r = op_reg(fetch8<do_debug>(), 2); prefetch<do_debug>(2); return decode_reg<do_debug>(r); }
        case 0xe7: { operand r = op_reg(fetch8<do_debug>(), 4); prefetch<do_debug>(2); return decode_reg<do_debug>(r); }
        default: break;
    }
    if (data >= 0xc8 && data <= 0xcf) return decode_reg<do_debug>(reg3(data, 1));
    if (data >= 0xd8 && data <= 0xdf) return decode_reg<do_debug>(reg3(data, 2));
    if (data >= 0xe8 && data <= 0xef) return decode_reg<do_debug>(reg3(data, 4));
    if (data >= 0xf8) return ins_SWI<do_debug>(data & 7);

    if (data >= 0x80 && data <= 0xbf) {
        u32 addr = load<do_debug>(reg3(data, 4));
        if (data & 8) { addr += sign_extend<8>(fetch8<do_debug>()); prefetch<do_debug>(2); }
        if (data <= 0x8f) return decode_src<do_debug>(op_mem(addr, 1));
        if (data <= 0x9f) return decode_src<do_debug>(op_mem(addr, 2));
        if (data <= 0xaf) return decode_src<do_debug>(op_mem(addr, 4));
        return decode_dst<do_debug>(addr);
    }

    if (data == 0xf3) {
        u8 d = fetch8<do_debug>();
        if ((d & 3) == 0) { u32 r = regs.reg32(d); prefetch<do_debug>(2); return decode_dst<do_debug>(r); }
        if ((d & 3) == 1) { u32 r = regs.reg32(d); i32 dd = sign_extend<16>(fetch16<do_debug>()); prefetch<do_debug>(6); return decode_dst<do_debug>(r + dd); }
        if (d == 0x03) { u8 ri = fetch8<do_debug>(); u8 di = fetch8<do_debug>(); u32 r = regs.reg32(ri); i32 dd = static_cast<i8>(regs.reg8(di)); prefetch<do_debug>(6); return decode_dst<do_debug>(r + dd); }
        if (d == 0x07) { u8 ri = fetch8<do_debug>(); u8 di = fetch8<do_debug>(); u32 r = regs.reg32(ri); i32 dd = static_cast<i16>(regs.reg16(di)); prefetch<do_debug>(6); return decode_dst<do_debug>(r + dd); }
        if (d == 0x13) {
            i32 dd = sign_extend<16>(fetch16<do_debug>());
            u32 address = regs.PC + dd;
            u8 sub = fetch8<do_debug>();
            if (sub >= 0x20 && sub <= 0x27) { prefetch<do_debug>(14); return ins_LD<do_debug>(reg3(sub, 2), op_imm(address, 2)); }
            if (sub >= 0x30 && sub <= 0x37) { prefetch<do_debug>(14); return ins_LD<do_debug>(reg3(sub, 4), op_imm(address, 4)); }
        }
        return undefined<do_debug>();
    }

    if (data == 0xf7) {
        fetch8<do_debug>();
        u32 a = fetch8<do_debug>();
        fetch8<do_debug>();
        u32 v = fetch8<do_debug>();
        fetch8<do_debug>();
        prefetch<do_debug>(14);
        return ins_LD<do_debug>(op_mem(a, 1), op_imm(v, 1));
    }

    if ((data & 0x0f) <= 5 && data >= 0xc0) {
        u32 addr = ea_extended<do_debug>(data & 7);
        u8 hi = data & 0xf0;
        if (hi == 0xc0) return decode_src<do_debug>(op_mem(addr, 1));
        if (hi == 0xd0) return decode_src<do_debug>(op_mem(addr, 2));
        if (hi == 0xe0) return decode_src<do_debug>(op_mem(addr, 4));
        return decode_dst<do_debug>(addr);
    }

    return undefined<do_debug>();
}

template<bool do_debug>
u32 core::ea_extended(u8 mode)
{
    switch (mode) {
        case 0: { u32 a = fetch8<do_debug>(); prefetch<do_debug>(2); return a; }
        case 1: { u32 a = fetch16<do_debug>(); prefetch<do_debug>(4); return a; }
        case 2: { u32 a = fetch24<do_debug>(); prefetch<do_debug>(6); return a; }
        case 3: {
            u8 d = fetch8<do_debug>();
            if ((d & 3) == 0) { u32 r = regs.reg32(d); prefetch<do_debug>(2); return r; }
            if ((d & 3) == 1) { u32 r = regs.reg32(d); i32 dd = sign_extend<16>(fetch16<do_debug>()); prefetch<do_debug>(6); return r + dd; }
            if (d == 0x03) { u8 ri = fetch8<do_debug>(); u8 di = fetch8<do_debug>(); u32 r = regs.reg32(ri); i32 dd = static_cast<i8>(regs.reg8(di)); prefetch<do_debug>(6); return r + dd; }
            if (d == 0x07) { u8 ri = fetch8<do_debug>(); u8 di = fetch8<do_debug>(); u32 r = regs.reg32(ri); i32 dd = static_cast<i16>(regs.reg16(di)); prefetch<do_debug>(6); return r + dd; }
            return 0;
        }
        case 4: {
            u8 d = fetch8<do_debug>();
            prefetch<do_debug>(2);
            u32 amt = (d & 3) == 0 ? 1 : (d & 3) == 1 ? 2 : (d & 3) == 2 ? 4 : 0;
            u32 loc = regs.reg32(d) - amt;
            regs.set_reg32(d, loc);
            return loc;
        }
        case 5: {
            u8 d = fetch8<do_debug>();
            prefetch<do_debug>(2);
            u32 amt = (d & 3) == 0 ? 1 : (d & 3) == 1 ? 2 : (d & 3) == 2 ? 4 : 0;
            u32 loc = regs.reg32(d);
            regs.set_reg32(d, loc + amt);
            return loc;
        }
    }
    return 0;
}

template<bool do_debug>
void core::decode_reg(const operand &reg) {
    u8 sz = reg.size;
    u8 data = fetch8<do_debug>();
    operand A = op_reg(ID_A, 1);

    if (data >= 0x40 && data <= 0x47) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(sz == 1 ? 22 : 28); return ins_MUL<do_debug>(reg3(data, sz), reg); }
    if (data >= 0x48 && data <= 0x4f) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(sz == 1 ? 18 : 24); return ins_MULS<do_debug>(reg3(data, sz), reg); }
    if (data >= 0x50 && data <= 0x57) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(sz == 1 ? 30 : 46); return ins_DIV<do_debug>(reg3(data, sz), reg); }
    if (data >= 0x58 && data <= 0x5f) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(sz == 1 ? 36 : 52); return ins_DIVS<do_debug>(reg3(data, sz), reg); }
    if (data >= 0x60 && data <= 0x67) { prefetch<do_debug>(4); return ins_INC<do_debug>(reg, op_imm(data & 7, sz)); }
    if (data >= 0x68 && data <= 0x6f) { prefetch<do_debug>(4); return ins_DEC<do_debug>(reg, op_imm(data & 7, sz)); }
    if (data >= 0x70 && data <= 0x7f) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(4); return ins_SCC<do_debug>(data & 15, reg); }
    if (data >= 0x80 && data <= 0x87) { prefetch<do_debug>(4); return ins_ADD<do_debug>(reg3(data, sz), reg); }
    if (data >= 0x88 && data <= 0x8f) { prefetch<do_debug>(4); return ins_LD<do_debug>(reg3(data, sz), reg); }
    if (data >= 0x90 && data <= 0x97) { prefetch<do_debug>(4); return ins_ADC<do_debug>(reg3(data, sz), reg); }
    if (data >= 0x98 && data <= 0x9f) { prefetch<do_debug>(4); return ins_LD<do_debug>(reg, reg3(data, sz)); }
    if (data >= 0xa0 && data <= 0xa7) { prefetch<do_debug>(4); return ins_SUB<do_debug>(reg3(data, sz), reg); }
    if (data >= 0xa8 && data <= 0xaf) { prefetch<do_debug>(4); return ins_LD<do_debug>(reg, op_imm(data & 7, sz)); }
    if (data >= 0xb0 && data <= 0xb7) { prefetch<do_debug>(4); return ins_SBC<do_debug>(reg3(data, sz), reg); }
    if (data >= 0xb8 && data <= 0xbf) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_EX<do_debug>(reg3(data, sz), reg); }
    if (data >= 0xc0 && data <= 0xc7) { prefetch<do_debug>(4); return ins_AND<do_debug>(reg3(data, sz), reg); }
    if (data >= 0xd0 && data <= 0xd7) { prefetch<do_debug>(4); return ins_XOR<do_debug>(reg3(data, sz), reg); }
    if (data >= 0xd8 && data <= 0xdf) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(4); return ins_CP<do_debug>(reg, op_imm(data & 7, sz)); }
    if (data >= 0xe0 && data <= 0xe7) { prefetch<do_debug>(4); return ins_OR<do_debug>(reg3(data, sz), reg); }
    if (data >= 0xf0 && data <= 0xf7) { prefetch<do_debug>(4); return ins_CP<do_debug>(reg3(data, sz), reg); }

    u32 pf = (sz == 1 ? 6 : sz == 2 ? 8 : 12);
    switch (data) {
        case 0x03: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_LD<do_debug>(reg, op_imm(v, sz)); }
        case 0x04: { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_PUSH<do_debug>(reg); }
        case 0x05: { prefetch<do_debug>(sz == 4 ? 12 : 8); return ins_POP<do_debug>(reg); }
        case 0x2e: { u8 cr = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_LD<do_debug>(op_cr(cr, sz), reg); }
        case 0x2f: { u8 cr = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_LD<do_debug>(reg, op_cr(cr, sz)); }
        case 0x06: { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(4); return ins_CPL<do_debug>(reg); }
        case 0x07: { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(4); return ins_NEG<do_debug>(reg); }
        case 0x08: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 24 : 30); return ins_MUL<do_debug>(reg, op_imm(v, sz)); }
        case 0x09: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 20 : 26); return ins_MULS<do_debug>(reg, op_imm(v, sz)); }
        case 0x0a: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 30 : 46); return ins_DIV<do_debug>(reg, op_imm(v, sz)); }
        case 0x0b: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 36 : 52); return ins_DIVS<do_debug>(reg, op_imm(v, sz)); }
        case 0x0c: { if (sz != 4) return undefined<do_debug>(); i32 d = sign_extend<16>(fetch16<do_debug>()); prefetch<do_debug>(12); return ins_LINK<do_debug>(reg, op_imm(static_cast<u32>(d), 2)); }
        case 0x0d: { if (sz != 4) return undefined<do_debug>(); prefetch<do_debug>(10); return ins_UNLK<do_debug>(reg); }
        case 0x0e: { if (sz != 2) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_BS1F<do_debug>(reg); }
        case 0x0f: { if (sz != 2) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_BS1B<do_debug>(reg); }
        case 0x10: { if (sz != 1) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_DAA<do_debug>(reg); }
        case 0x12: { if (sz == 1) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_EXTZ<do_debug>(reg); }
        case 0x13: { if (sz == 1) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_EXTS<do_debug>(reg); }
        case 0x14: { if (sz == 1) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_PAA<do_debug>(reg); }
        case 0x16: { if (sz != 2) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_MIRR<do_debug>(reg); }
        case 0x19: { if (sz != 2) return undefined<do_debug>(); prefetch<do_debug>(38); return ins_MULA<do_debug>(reg); }
        case 0x1c: { if (sz == 4) return undefined<do_debug>(); i32 d = sign_extend<8>(fetch8<do_debug>()); prefetch<do_debug>(8); return ins_DJNZ<do_debug>(reg, op_imm(static_cast<u32>(d), 1)); }
        case 0x20: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_ANDCF<do_debug>(reg, op_imm(v, 1)); }
        case 0x21: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_ORCF<do_debug>(reg, op_imm(v, 1)); }
        case 0x22: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_XORCF<do_debug>(reg, op_imm(v, 1)); }
        case 0x23: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_LDCF<do_debug>(reg, op_imm(v, 1)); }
        case 0x24: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_STCF<do_debug>(reg, op_imm(v, 1)); }
        case 0x28: { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_ANDCF<do_debug>(reg, A); }
        case 0x29: { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_ORCF<do_debug>(reg, A); }
        case 0x2a: { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_XORCF<do_debug>(reg, A); }
        case 0x2b: { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_LDCF<do_debug>(reg, A); }
        case 0x2c: { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(6); return ins_STCF<do_debug>(reg, A); }
        case 0x30: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_RES<do_debug>(reg, op_imm(v, 1)); }
        case 0x31: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_SET<do_debug>(reg, op_imm(v, 1)); }
        case 0x32: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_CHG<do_debug>(reg, op_imm(v, 1)); }
        case 0x33: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_BIT<do_debug>(reg, op_imm(v & 15, 1)); }
        case 0x34: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch8<do_debug>(); prefetch<do_debug>(8); return ins_TSET<do_debug>(reg, op_imm(v, 1)); }
        case 0x38: { if (sz != 2) return undefined<do_debug>(); u32 v = fetch16<do_debug>(); prefetch<do_debug>(10); return ins_MINC<do_debug>(1, reg, op_imm(v, 2)); }
        case 0x39: { if (sz != 2) return undefined<do_debug>(); u32 v = fetch16<do_debug>(); prefetch<do_debug>(10); return ins_MINC<do_debug>(2, reg, op_imm(v, 2)); }
        case 0x3a: { if (sz != 2) return undefined<do_debug>(); u32 v = fetch16<do_debug>(); prefetch<do_debug>(10); return ins_MINC<do_debug>(4, reg, op_imm(v, 2)); }
        case 0x3c: { if (sz != 2) return undefined<do_debug>(); u32 v = fetch16<do_debug>(); prefetch<do_debug>(8); return ins_MDEC<do_debug>(1, reg, op_imm(v, 2)); }
        case 0x3d: { if (sz != 2) return undefined<do_debug>(); u32 v = fetch16<do_debug>(); prefetch<do_debug>(8); return ins_MDEC<do_debug>(2, reg, op_imm(v, 2)); }
        case 0x3e: { if (sz != 2) return undefined<do_debug>(); u32 v = fetch16<do_debug>(); prefetch<do_debug>(8); return ins_MDEC<do_debug>(4, reg, op_imm(v, 2)); }
        case 0xc8: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_ADD<do_debug>(reg, op_imm(v, sz)); }
        case 0xc9: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_ADC<do_debug>(reg, op_imm(v, sz)); }
        case 0xca: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_SUB<do_debug>(reg, op_imm(v, sz)); }
        case 0xcb: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_SBC<do_debug>(reg, op_imm(v, sz)); }
        case 0xcc: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_AND<do_debug>(reg, op_imm(v, sz)); }
        case 0xcd: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_XOR<do_debug>(reg, op_imm(v, sz)); }
        case 0xce: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_OR<do_debug>(reg, op_imm(v, sz)); }
        case 0xcf: { u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(pf); return ins_CP<do_debug>(reg, op_imm(v, sz)); }
        case 0xe8: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_RLC<do_debug>(reg, op_imm(v, 1)); }
        case 0xe9: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_RRC<do_debug>(reg, op_imm(v, 1)); }
        case 0xea: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_RL<do_debug>(reg, op_imm(v, 1)); }
        case 0xeb: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_RR<do_debug>(reg, op_imm(v, 1)); }
        case 0xec: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_SLA<do_debug>(reg, op_imm(v, 1)); }
        case 0xed: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_SRA<do_debug>(reg, op_imm(v, 1)); }
        case 0xee: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_SLL<do_debug>(reg, op_imm(v, 1)); }
        case 0xef: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(6); return ins_SRL<do_debug>(reg, op_imm(v, 1)); }
        case 0xf8: { prefetch<do_debug>(6); return ins_RLC<do_debug>(reg, A); }
        case 0xf9: { prefetch<do_debug>(6); return ins_RRC<do_debug>(reg, A); }
        case 0xfa: { prefetch<do_debug>(6); return ins_RL<do_debug>(reg, A); }
        case 0xfb: { prefetch<do_debug>(6); return ins_RR<do_debug>(reg, A); }
        case 0xfc: { prefetch<do_debug>(6); return ins_SLA<do_debug>(reg, A); }
        case 0xfd: { prefetch<do_debug>(6); return ins_SRA<do_debug>(reg, A); }
        case 0xfe: { prefetch<do_debug>(6); return ins_SLL<do_debug>(reg, A); }
        case 0xff: { prefetch<do_debug>(6); return ins_SRL<do_debug>(reg, A); }
        default: return undefined<do_debug>();
    }
}

template<bool do_debug>
void core::decode_src(const operand &m) {
    u8 sz = m.size;
    u8 data = fetch8<do_debug>();
    operand A = op_reg(ID_A, 1);

    if (data >= 0x20 && data <= 0x27) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_LD<do_debug>(reg3(data, sz), m); }
    if (data >= 0x30 && data <= 0x37) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_EX<do_debug>(m, reg3(data, sz)); }
    if (data >= 0x40 && data <= 0x47) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(sz == 1 ? 24 : 30); return ins_MUL<do_debug>(reg3(data, sz), m); }
    if (data >= 0x48 && data <= 0x4f) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(sz == 1 ? 20 : 26); return ins_MULS<do_debug>(reg3(data, sz), m); }
    if (data >= 0x50 && data <= 0x57) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(sz == 1 ? 30 : 46); return ins_DIV<do_debug>(reg3(data, sz), m); }
    if (data >= 0x58 && data <= 0x5f) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(sz == 1 ? 36 : 52); return ins_DIVS<do_debug>(reg3(data, sz), m); }
    if (data >= 0x60 && data <= 0x67) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_INC<do_debug>(m, op_imm(data & 7, sz)); }
    if (data >= 0x68 && data <= 0x6f) { if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_DEC<do_debug>(m, op_imm(data & 7, sz)); }
    if (data >= 0x80 && data <= 0x87) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_ADD<do_debug>(reg3(data, sz), m); }
    if (data >= 0x88 && data <= 0x8f) { prefetch<do_debug>(sz == 4 ? 12 : 8); return ins_ADD<do_debug>(m, reg3(data, sz)); }
    if (data >= 0x90 && data <= 0x97) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_ADC<do_debug>(reg3(data, sz), m); }
    if (data >= 0x98 && data <= 0x9f) { prefetch<do_debug>(sz == 4 ? 12 : 8); return ins_ADC<do_debug>(m, reg3(data, sz)); }
    if (data >= 0xa0 && data <= 0xa7) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_SUB<do_debug>(reg3(data, sz), m); }
    if (data >= 0xa8 && data <= 0xaf) { prefetch<do_debug>(sz == 4 ? 12 : 8); return ins_SUB<do_debug>(m, reg3(data, sz)); }
    if (data >= 0xb0 && data <= 0xb7) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_SBC<do_debug>(reg3(data, sz), m); }
    if (data >= 0xb8 && data <= 0xbf) { prefetch<do_debug>(sz == 4 ? 12 : 8); return ins_SBC<do_debug>(m, reg3(data, sz)); }
    if (data >= 0xc0 && data <= 0xc7) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_AND<do_debug>(reg3(data, sz), m); }
    if (data >= 0xc8 && data <= 0xcf) { prefetch<do_debug>(sz == 4 ? 12 : 8); return ins_AND<do_debug>(m, reg3(data, sz)); }
    if (data >= 0xd0 && data <= 0xd7) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_XOR<do_debug>(reg3(data, sz), m); }
    if (data >= 0xd8 && data <= 0xdf) { prefetch<do_debug>(sz == 4 ? 12 : 8); return ins_XOR<do_debug>(m, reg3(data, sz)); }
    if (data >= 0xe0 && data <= 0xe7) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_OR<do_debug>(reg3(data, sz), m); }
    if (data >= 0xe8 && data <= 0xef) { prefetch<do_debug>(sz == 4 ? 12 : 8); return ins_OR<do_debug>(m, reg3(data, sz)); }
    if (data >= 0xf0 && data <= 0xf7) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_CP<do_debug>(reg3(data, sz), m); }
    if (data >= 0xf8 && data <= 0xff) { prefetch<do_debug>(sz == 4 ? 8 : 6); return ins_CP<do_debug>(m, reg3(data, sz)); }

    switch (data) {
        case 0x04: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_PUSH<do_debug>(m);
        case 0x06: if (sz != 1) return undefined<do_debug>(); prefetch<do_debug>(24); return ins_RLD<do_debug>(m);
        case 0x07: if (sz != 1) return undefined<do_debug>(); prefetch<do_debug>(24); return ins_RRD<do_debug>(m);
        case 0x10: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(12); return ins_LDI<do_debug>(sz, sz);
        case 0x11: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(2); return ins_LDIR<do_debug>(sz, sz);
        case 0x12: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(12); return ins_LDI<do_debug>(sz, -static_cast<i32>(sz));
        case 0x13: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(2); return ins_LDIR<do_debug>(sz, -static_cast<i32>(sz));
        case 0x14: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(10); return ins_CPI<do_debug>(sz, sz);
        case 0x15: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(2); return ins_CPIR<do_debug>(sz, sz);
        case 0x16: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(10); return ins_CPI<do_debug>(sz, -static_cast<i32>(sz));
        case 0x17: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(2); return ins_CPIR<do_debug>(sz, -static_cast<i32>(sz));
        case 0x19: { if (sz == 4) return undefined<do_debug>(); u32 nn = fetch16<do_debug>(); prefetch<do_debug>(12); return ins_LD<do_debug>(op_mem(nn, sz), m); }
        case 0x38: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 10 : 12); return ins_ADD<do_debug>(m, op_imm(v, sz)); }
        case 0x39: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 10 : 12); return ins_ADC<do_debug>(m, op_imm(v, sz)); }
        case 0x3a: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 10 : 12); return ins_SUB<do_debug>(m, op_imm(v, sz)); }
        case 0x3b: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 10 : 12); return ins_SBC<do_debug>(m, op_imm(v, sz)); }
        case 0x3c: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 10 : 12); return ins_AND<do_debug>(m, op_imm(v, sz)); }
        case 0x3d: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 10 : 12); return ins_XOR<do_debug>(m, op_imm(v, sz)); }
        case 0x3e: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 10 : 12); return ins_OR<do_debug>(m, op_imm(v, sz)); }
        case 0x3f: { if (sz == 4) return undefined<do_debug>(); u32 v = fetch_imm<do_debug>(sz); prefetch<do_debug>(sz == 1 ? 8 : 10); return ins_CP<do_debug>(m, op_imm(v, sz)); }
        case 0x78: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_RLC<do_debug>(m, op_imm(1, 1));
        case 0x79: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_RRC<do_debug>(m, op_imm(1, 1));
        case 0x7a: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_RL<do_debug>(m, op_imm(1, 1));
        case 0x7b: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_RR<do_debug>(m, op_imm(1, 1));
        case 0x7c: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_SLA<do_debug>(m, op_imm(1, 1));
        case 0x7d: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_SRA<do_debug>(m, op_imm(1, 1));
        case 0x7e: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_SLL<do_debug>(m, op_imm(1, 1));
        case 0x7f: if (sz == 4) return undefined<do_debug>(); prefetch<do_debug>(8); return ins_SRL<do_debug>(m, op_imm(1, 1));
        default: return undefined<do_debug>();
    }
}

template<bool do_debug>
void core::decode_dst(u32 address) {
    u8 data = fetch8<do_debug>();
    operand A = op_reg(ID_A, 1);

    if (data >= 0x20 && data <= 0x27) { prefetch<do_debug>(8); return ins_LD<do_debug>(reg3(data, 2), op_imm(address, 2)); }
    if (data >= 0x30 && data <= 0x37) { prefetch<do_debug>(8); return ins_LD<do_debug>(reg3(data, 4), op_imm(address, 4)); }
    if (data >= 0x40 && data <= 0x47) { prefetch<do_debug>(6); return ins_LD<do_debug>(op_mem(address, 1), reg3(data, 1)); }
    if (data >= 0x50 && data <= 0x57) { prefetch<do_debug>(6); return ins_LD<do_debug>(op_mem(address, 2), reg3(data, 2)); }
    if (data >= 0x60 && data <= 0x67) { prefetch<do_debug>(8); return ins_LD<do_debug>(op_mem(address, 4), reg3(data, 4)); }
    if (data >= 0x80 && data <= 0x87) { prefetch<do_debug>(10); return ins_ANDCF<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0x88 && data <= 0x8f) { prefetch<do_debug>(10); return ins_ORCF<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0x90 && data <= 0x97) { prefetch<do_debug>(10); return ins_XORCF<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0x98 && data <= 0x9f) { prefetch<do_debug>(10); return ins_LDCF<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0xa0 && data <= 0xa7) { prefetch<do_debug>(10); return ins_STCF<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0xa8 && data <= 0xaf) { prefetch<do_debug>(10); return ins_TSET<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0xb0 && data <= 0xb7) { prefetch<do_debug>(10); return ins_RES<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0xb8 && data <= 0xbf) { prefetch<do_debug>(10); return ins_SET<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0xc0 && data <= 0xc7) { prefetch<do_debug>(10); return ins_CHG<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0xc8 && data <= 0xcf) { prefetch<do_debug>(10); return ins_BIT<do_debug>(op_mem(address, 1), op_imm(data & 7, 1)); }
    if (data >= 0xd0 && data <= 0xdf) { prefetch<do_debug>(8); if (!condition(data & 15)) return; prefetch<do_debug>(4); ins_JP<do_debug>(op_imm(address, 4)); return prefetch<do_debug>(2); }
    if (data >= 0xe0 && data <= 0xef) { if (!condition(data & 15)) { prefetch<do_debug>(8); return; } prefetch<do_debug>(20); ins_CALL<do_debug>(op_imm(address, 4)); return prefetch<do_debug>(2); }
    if (data >= 0xf0 && data <= 0xff) { if (!condition(data & 15)) { prefetch<do_debug>(8); return; } prefetch<do_debug>(20); ins_RET<do_debug>(); return prefetch<do_debug>(2); }

    switch (data) {
        case 0x00: { u32 v = fetch8<do_debug>(); prefetch<do_debug>(8); return ins_LD<do_debug>(op_mem(address, 1), op_imm(v, 1)); }
        case 0x02: { u32 v = fetch16<do_debug>(); prefetch<do_debug>(10); return ins_LD<do_debug>(op_mem(address, 2), op_imm(v, 2)); }
        case 0x04: prefetch<do_debug>(10); return ins_POP<do_debug>(op_mem(address, 1));
        case 0x06: prefetch<do_debug>(10); return ins_POP<do_debug>(op_mem(address, 2));
        case 0x14: { u32 nn = fetch16<do_debug>(); prefetch<do_debug>(12); return ins_LD<do_debug>(op_mem(address, 1), op_mem(nn, 1)); }
        case 0x16: { u32 nn = fetch16<do_debug>(); prefetch<do_debug>(12); return ins_LD<do_debug>(op_mem(address, 2), op_mem(nn, 2)); }
        case 0x28: prefetch<do_debug>(10); return ins_ANDCF<do_debug>(op_mem(address, 1), A);
        case 0x29: prefetch<do_debug>(10); return ins_ORCF<do_debug>(op_mem(address, 1), A);
        case 0x2a: prefetch<do_debug>(10); return ins_XORCF<do_debug>(op_mem(address, 1), A);
        case 0x2b: prefetch<do_debug>(10); return ins_LDCF<do_debug>(op_mem(address, 1), A);
        case 0x2c: prefetch<do_debug>(12); return ins_STCF<do_debug>(op_mem(address, 1), A);
        default: return undefined<do_debug>();
    }
}

template void core::decode<false>();
template void core::decode<true>();
template void core::undefined<false>();
template void core::undefined<true>();
template void core::decode_reg<false>(const operand &reg);
template void core::decode_reg<true>(const operand &reg);
template void core::decode_src<false>(const operand &m);
template void core::decode_src<true>(const operand &m);
template void core::decode_dst<false>(u32 address);
template void core::decode_dst<true>(u32 address);
template u32 core::ea_extended<false>(u8 mode);
template u32 core::ea_extended<true>(u8 mode);

}
