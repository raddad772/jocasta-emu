#include "tlcs900h.h"
#include "tlcs900h_disassembler.h"

#include <cstdio>
#include <cstring>

namespace TLCS900H {

static inline u8 dbg_read8(jsm_debug_read_trace &trace, u32 &PC)
{
    u8 v = static_cast<u8>(trace.read_trace(trace.ptr, PC));
    PC++;
    return v;
}

static inline u16 dbg_read16(jsm_debug_read_trace &trace, u32 &PC)
{
    u8 lo = dbg_read8(trace, PC);
    u8 hi = dbg_read8(trace, PC);
    return static_cast<u16>((lo | (static_cast<u16>(hi) << 8)));
}

static inline u32 dbg_read24(jsm_debug_read_trace &trace, u32 &PC)
{
    u8 b0 = dbg_read8(trace, PC);
    u8 b1 = dbg_read8(trace, PC);
    u8 b2 = dbg_read8(trace, PC);
    return static_cast<u32>(b0) | (static_cast<u32>(b1) << 8) | (static_cast<u32>(b2) << 16);
}

static inline u32 dbg_read32(jsm_debug_read_trace &trace, u32 &PC)
{
    u8 b0 = dbg_read8(trace, PC);
    u8 b1 = dbg_read8(trace, PC);
    u8 b2 = dbg_read8(trace, PC);
    u8 b3 = dbg_read8(trace, PC);
    return static_cast<u32>(b0) | (static_cast<u32>(b1) << 8) | (static_cast<u32>(b2) << 16) | (static_cast<u32>(b3) << 24);
}

static const char * const registers8[] = {
    "ra0","rw0","qa0","qw0","rc0","rb0","qc0","qb0","re0","rd0","qe0","qd0","rl0","rh0","ql0","qh0",
    "ra1","rw1","qa1","qw1","rc1","rb1","qc1","qb1","re1","rd1","qe1","qd1","rl1","rh1","ql1","qh1",
    "ra2","rw2","qa2","qw2","rc2","rb2","qc2","qb2","re2","rd2","qe2","qd2","rl2","rh2","ql2","qh2",
    "ra3","rw3","qa3","qw3","rc3","rb3","qc3","qb3","re3","rd3","qe3","qd3","rl3","rh3","ql3","qh3",
    "a'", "w'", "qa'","qw'","c'", "b'", "qc'","qb'","e'", "d'", "qe'","qd'","l'", "h'", "ql'","qh'",
    "a", "w", "qa", "qw", "c", "b", "qc", "qb", "e", "d", "qe", "qd", "l", "h", "ql", "qh",
    "ixl","ixh","qixl","qixh","iyl","iyh","qiyl","qiyh","izl","izh","qizl","qizh","spl","sph","qspl","qsph",
};

static const char * const registers16[] = {
    "rwa0","qwa0","rbc0","qbc0","rde0","qde0","rhl0","qhl0",
    "rwa1","qwa1","rbc1","qbc1","rde1","qde1","rhl1","qhl1",
    "rwa2","qwa2","rbc2","qbc2","rde2","qde2","rhl2","qhl2",
    "rwa3","qwa3","rbc3","qbc3","rde3","qde3","rhl3","qhl3",
    "wa'","qwa'","bc'","qbc'","de'","qde'","hl'","qhl'",
    "wa", "qwa", "bc", "qbc", "de", "qde", "hl", "qhl",
    "ix", "qix", "iy", "qiy", "iz", "qiz", "sp", "qsp",
};

static const char * const registers32[] = {
    "xwa0","xbc0","xde0","xhl0",
    "xwa1","xbc1","xde1","xhl1",
    "xwa2","xbc2","xde2","xhl2",
    "xwa3","xbc3","xde3","xhl3",
    "xwa'","xbc'","xde'","xhl'",
    "xwa", "xbc", "xde", "xhl",
    "xix", "xiy", "xiz", "xsp",
};

static const char * const controls16[] = {
    "dmas0l","dmas0h","dmas1l","dmas1h","dmas2l","dmas2h","dmas3l","dmas3h",
    "dmad0l","dmad0h","dmad1l","dmad1h","dmad2l","dmad2h","dmad3l","dmad3h",
    "dmac0", "dmac0h","dmac1", "dmac1h","dmac2", "dmac2h","dmac3", "dmac3h",
};

static const char * const controls32[] = {
    "dmas0","dmas1","dmas2","dmas3",
    "dmad0","dmad1","dmad2","dmad3",
    "dmam0","dmam1","dmam2","dmam3",
};

static const char * const conditions[] = {
    "f", "lt", "le", "ule", "ov", "mi", "eq", "ult",
    "t", "ge", "gt", "ugt", "nov", "pl", "ne", "uge",
};

static const char *reg8_name(u8 id, char *scratch, size_t sz)
{
    if (id < 0x40) return registers8[id];
    if (id >= 0xd0) return registers8[0x40 + (id - 0xd0)];
    snprintf(scratch, sz, "rb?[%02x]", id);
    return scratch;
}

static const char *reg16_name(u8 id, char *scratch, size_t sz)
{
    if (id < 0x40) return registers16[id >> 1];
    if (id >= 0xd0) return registers16[0x20 + ((id - 0xd0) >> 1)];
    snprintf(scratch, sz, "rw?[%02x]", id);
    return scratch;
}

static const char *reg32_name(u8 id, char *scratch, size_t sz)
{
    if (id < 0x40) return registers32[id >> 2];
    if (id >= 0xd0) return registers32[0x10 + ((id - 0xd0) >> 2)];
    snprintf(scratch, sz, "rl?[%02x]", id);
    return scratch;
}

static const char *regN_name(u32 size, u8 id, char *scratch, size_t sz)
{
    if (size == 1) return reg8_name (id, scratch, sz);
    if (size == 2) return reg16_name(id, scratch, sz);
    if (size == 4) return reg32_name(id, scratch, sz);
    snprintf(scratch, sz, "r?[%02x]", id);
    return scratch;
}

static const char *ctrl8_name(u8 id, char *scratch, size_t sz)
{
    snprintf(scratch, sz, "c%02x", id);
    return scratch;
}

static const char *ctrl16_name(u8 id, char *scratch, size_t sz)
{
    if (id < 0x30) return controls16[id >> 1];
    if (id >= 0x3c && id <= 0x3d) return "intnest";
    snprintf(scratch, sz, "cw?[%02x]", id);
    return scratch;
}

static const char *ctrl32_name(u8 id, char *scratch, size_t sz)
{
    if (id < 0x30) return controls32[id >> 2];
    if (id >= 0x3c && id <= 0x3f) return "intnest";
    snprintf(scratch, sz, "cl?[%02x]", id);
    return scratch;
}

static const char *ctrlN_name(u32 size, u8 id, char *scratch, size_t sz)
{
    if (size == 1) return ctrl8_name (id, scratch, sz);
    if (size == 2) return ctrl16_name(id, scratch, sz);
    if (size == 4) return ctrl32_name(id, scratch, sz);
    snprintf(scratch, sz, "c?[%02x]", id);
    return scratch;
}

static const u8 LOOKUP8 [8] = {0xe1,0xe0,0xe5,0xe4,0xe9,0xe8,0xed,0xec};
static const u8 LOOKUP16[8] = {0xe0,0xe4,0xe8,0xec,0xf0,0xf4,0xf8,0xfc};

static u8 lookup(u32 size, u8 reg3)
{
    reg3 &= 7;
    return (size == 1) ? LOOKUP8[reg3] : LOOKUP16[reg3];
}

enum OpMode : u8 {
    OM_Null = 0,
    OM_Text,
    OM_Condition,
    OM_Register,
    OM_Control,
    OM_Immediate,
    OM_Displacement,
    OM_DisplacementPC,
    OM_IndirectRegister,
    OM_IndirectRegisterDecrement,
    OM_IndirectRegisterIncrement,
    OM_IndirectRegisterRegister8,
    OM_IndirectRegisterRegister16,
    OM_IndirectRegisterDisplacement8,
    OM_IndirectRegisterDisplacement16,
    OM_IndirectImmediate8,
    OM_IndirectImmediate16,
    OM_IndirectImmediate24,
};

struct Op {
    OpMode mode = OM_Null;
    u32 size = 0;
    char text[36] = {};
    u8 cond = 0;
    u8 reg = 0;
    u8 regAdd = 0;
    u32 imm = 0;
    i32 disp = 0;

    bool valid() const { return mode != OM_Null; }
    void null() { mode = OM_Null; }

    void set_text(const char *s) {
        mode = OM_Text;
        strncpy(text, s, sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    }

    void set_condition(u8 c) { mode = OM_Condition; cond = c; }

    void set_register(u32 sz, u8 id) { mode = OM_Register; size = sz; reg = id; }
    void set_register3(u32 sz, u8 code) { mode = OM_Register; size = sz; reg = lookup(sz, code); }

    void set_control(u32 sz, u8 id) { mode = OM_Control; size = sz; reg = id; }

    void set_immediate(u32 bit_sz, u32 val) { mode = OM_Immediate; size = bit_sz; imm = val; }

    void set_displacement(u32 bit_sz, i32 d) { mode = OM_Displacement; size = bit_sz; disp = d; }
    void set_displacementPC(u32 bit_sz, i32 d) { mode = OM_DisplacementPC; size = bit_sz; disp = d; }

    void set_indirect_register(u32 sz, u8 id) { mode = OM_IndirectRegister; size = sz; reg = id; }
    void set_indirect_register3(u32 sz, u8 code) { mode = OM_IndirectRegister; size = sz; reg = lookup(4, code); }
    void set_indirect_decrement(u32 sz, u8 id) { mode = OM_IndirectRegisterDecrement; size = sz; reg = id; }
    void set_indirect_increment(u32 sz, u8 id) { mode = OM_IndirectRegisterIncrement; size = sz; reg = id; }
    void set_indirect_increment3(u32 sz, u8 code) { mode = OM_IndirectRegisterIncrement; size = sz; reg = lookup(4, code); }
    void set_indirect_reg8(u32 sz, u8 r32, u8 r8) { mode = OM_IndirectRegisterRegister8; size = sz; reg = r32; regAdd = r8; }
    void set_indirect_reg16(u32 sz, u8 r32, u8 r16) { mode = OM_IndirectRegisterRegister16; size = sz; reg = r32; regAdd = r16; }
    void set_indirect_disp8(u32 sz, u8 code, i32 d) { mode = OM_IndirectRegisterDisplacement8; size = sz; reg = lookup(4, code); disp = d; }
    void set_indirect_disp16(u32 sz, u8 id, i32 d) { mode = OM_IndirectRegisterDisplacement16; size = sz; reg = id; disp = d; }
    void set_indirect_imm8 (u32 sz, u32 v) { mode = OM_IndirectImmediate8; size = sz; imm = v; }
    void set_indirect_imm16(u32 sz, u32 v) { mode = OM_IndirectImmediate16; size = sz; imm = v; }
    void set_indirect_imm24(u32 sz, u32 v) { mode = OM_IndirectImmediate24; size = sz; imm = v; }
};

static void format_operand(const Op &op, u32 pc_after, char *buf, size_t buf_sz)
{
    char sc[32];
    buf[0] = '\0';

    switch (op.mode) {
        case OM_Null:
            break;

        case OM_Text:
            snprintf(buf, buf_sz, "%s", op.text);
            break;

        case OM_Condition:
            snprintf(buf, buf_sz, "%s", conditions[op.cond & 15]);
            break;

        case OM_Register:
            snprintf(buf, buf_sz, "%s", regN_name(op.size, op.reg, sc, sizeof(sc)));
            break;

        case OM_Control:
            snprintf(buf, buf_sz, "%s", ctrlN_name(op.size, op.reg, sc, sizeof(sc)));
            break;

        case OM_Immediate:
            if (op.size <= 7) snprintf(buf, buf_sz, "%u", op.imm);
            else if (op.size == 8) snprintf(buf, buf_sz, "0x%02x", op.imm);
            else if (op.size == 16) snprintf(buf, buf_sz, "0x%04x", op.imm);
            else if (op.size == 24) snprintf(buf, buf_sz, "0x%06x", op.imm);
            else snprintf(buf, buf_sz, "0x%08x", op.imm);
            break;

        case OM_Displacement: {
            i32 d = static_cast<i8>((op.disp & 0xff));
            if (op.size == 16) d = static_cast<i8>((op.disp & 0xff));
            if (d < 0) snprintf(buf, buf_sz, "-0x%02x", static_cast<u32>((-d)));
            else snprintf(buf, buf_sz, "+0x%02x", static_cast<u32>((+d)));
            break;
        }

        case OM_DisplacementPC: {
            u32 target;
            if (op.size == 1)
                target = pc_after + static_cast<u32>(static_cast<i32>(static_cast<i8>((op.disp & 0xff))));
            else
                target = pc_after + static_cast<u32>(static_cast<i32>(static_cast<i16>((op.disp & 0xffff))));
            snprintf(buf, buf_sz, "0x%06x", target);
            break;
        }

        case OM_IndirectRegister:
            snprintf(buf, buf_sz, "(%s)", regN_name(op.size, op.reg, sc, sizeof(sc)));
            break;

        case OM_IndirectRegisterDecrement:
            snprintf(buf, buf_sz, "(-%s)", reg32_name(op.reg, sc, sizeof(sc)));
            break;

        case OM_IndirectRegisterIncrement:
            snprintf(buf, buf_sz, "(%s+)", reg32_name(op.reg, sc, sizeof(sc)));
            break;

        case OM_IndirectRegisterRegister8: {
            char sc2[32];
            snprintf(buf, buf_sz, "(%s+%s)",
                     reg32_name(op.reg, sc, sizeof(sc)),
                     reg8_name (op.regAdd, sc2, sizeof(sc2)));
            break;
        }

        case OM_IndirectRegisterRegister16: {
            char sc2[32];
            snprintf(buf, buf_sz, "(%s+%s)",
                     reg32_name(op.reg, sc, sizeof(sc)),
                     reg16_name(op.regAdd, sc2, sizeof(sc2)));
            break;
        }

        case OM_IndirectRegisterDisplacement8: {
            i32 d = static_cast<i8>((op.disp & 0xff));
            const char *rn = reg32_name(op.reg, sc, sizeof(sc));
            if (d == 0) snprintf(buf, buf_sz, "(%s)", rn);
            else if (d < 0) snprintf(buf, buf_sz, "(%s-0x%02x)", rn, static_cast<u32>((-d)));
            else snprintf(buf, buf_sz, "(%s+0x%02x)", rn, static_cast<u32>((+d)));
            break;
        }

        case OM_IndirectRegisterDisplacement16: {
            i32 d = static_cast<i16>((op.disp & 0xffff));
            const char *rn = reg32_name(op.reg, sc, sizeof(sc));
            if (d == 0) snprintf(buf, buf_sz, "(%s)", rn);
            else if (d < 0) snprintf(buf, buf_sz, "(%s-0x%04x)", rn, static_cast<u32>((-d)));
            else snprintf(buf, buf_sz, "(%s+0x%04x)", rn, static_cast<u32>((+d)));
            break;
        }

        case OM_IndirectImmediate8:
            snprintf(buf, buf_sz, "(0x%02x)", op.imm);
            break;

        case OM_IndirectImmediate16:
            snprintf(buf, buf_sz, "(0x%04x)", op.imm);
            break;

        case OM_IndirectImmediate24:
            snprintf(buf, buf_sz, "(0x%06x)", op.imm);
            break;
    }
}

static const u32 OP_SIZES[4] = {1, 2, 4, 0};

void disassemble(core & , u32 &PC, jsm_debug_read_trace &trace, jsm_string &outstr)
{
    u8 op[8];
    u8 ops = 0;

    auto read8 = [&]() -> u8 {
        u8 v = dbg_read8(trace, PC);
        if (ops < 8) op[ops++] = v;
        return v;
    };
    auto read16 = [&]() -> u16 {
        u8 lo = read8(), hi = read8();
        return static_cast<u16>((lo | (static_cast<u16>(hi) << 8)));
    };
    auto read24 = [&]() -> u32 {
        u8 b0 = read8(), b1 = read8(), b2 = read8();
        return static_cast<u32>(b0) | (static_cast<u32>(b1) << 8) | (static_cast<u32>(b2) << 16);
    };
    auto read32 = [&]() -> u32 {
        u8 b0 = read8(), b1 = read8(), b2 = read8(), b3 = read8();
        return static_cast<u32>(b0) | (static_cast<u32>(b1) << 8) | (static_cast<u32>(b2) << 16) | (static_cast<u32>(b3) << 24);
    };
    auto reads = [&](u32 sz) -> u32 {
        if (sz == 1) return read8();
        if (sz == 2) return read16();
        if (sz == 4) return read32();
        return 0;
    };

    const char *name = nullptr;
    Op lhs, rhs;

    bool opRegister = false;
    bool opSourceMemory = false;
    bool opTargetMemory = false;

    u8 fetch = read8();

#define OPSIZE OP_SIZES[((fetch) >> 4) & 3]

    switch (fetch) {
        case 0x00: name = "nop"; break;
        case 0x01: break;
        case 0x02: name = "push"; lhs.set_text("sr"); break;
        case 0x03: name = "pop"; lhs.set_text("sr"); break;
        case 0x04: break;
        case 0x05: name = "halt"; break;
        case 0x06: {
            u8 n = read8() & 7;
            if (n == 7) { name = "di"; }
            else { name = "ei"; lhs.set_immediate(3, n); }
            break;
        }
        case 0x07: name = "reti"; break;
        case 0x08: { u8 a = read8(); u8 v = read8();
                     name = "ld"; lhs.set_indirect_imm8(1, a); rhs.set_immediate(8, v); break; }
        case 0x09: { u8 v = read8();
                     name = "push"; lhs.set_immediate(8, v); break; }
        case 0x0a: { u8 a = read8(); u16 v = read16();
                     name = "ldw"; lhs.set_indirect_imm8(1, a); rhs.set_immediate(16, v); break; }
        case 0x0b: { u16 v = read16();
                     name = "pushw"; lhs.set_immediate(16, v); break; }
        case 0x0c: name = "incf"; break;
        case 0x0d: name = "decf"; break;
        case 0x0e: name = "ret"; break;
        case 0x0f: { i32 d = static_cast<i16>(read16());
                     name = "retd"; lhs.set_displacement(16, d); break; }
        case 0x10: name = "rcf"; break;
        case 0x11: name = "scf"; break;
        case 0x12: name = "ccf"; break;
        case 0x13: name = "zcf"; break;
        case 0x14: name = "push"; lhs.set_register3(1, 1); break;
        case 0x15: name = "pop"; lhs.set_register3(1, 1); break;
        case 0x16: name = "ex"; lhs.set_text("f"); rhs.set_text("f'"); break;
        case 0x17: { u8 n = read8(); name = "ldf"; lhs.set_immediate(2, n & 3); break; }
        case 0x18: name = "push"; lhs.set_text("f"); break;
        case 0x19: name = "pop"; lhs.set_text("f"); break;
        case 0x1a: { u16 a = read16(); name = "jp"; lhs.set_immediate(16, a); break; }
        case 0x1b: { u32 a = read24(); name = "jp"; lhs.set_immediate(24, a); break; }
        case 0x1c: { u16 a = read16(); name = "call"; lhs.set_immediate(16, a); break; }
        case 0x1d: { u32 a = read24(); name = "call"; lhs.set_immediate(24, a); break; }
        case 0x1e: { i32 d = static_cast<i16>(read16()); name = "calr"; lhs.set_displacementPC(16, d); break; }
        case 0x1f: break;

        case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x24: case 0x25: case 0x26: case 0x27: {
            u8 v = read8(); name = "ld"; lhs.set_register3(1, fetch); rhs.set_immediate(8, v); break;
        }
        case 0x28: case 0x29: case 0x2a: case 0x2b:
        case 0x2c: case 0x2d: case 0x2e: case 0x2f:
            name = "push"; lhs.set_register3(2, fetch); break;

        case 0x30: case 0x31: case 0x32: case 0x33:
        case 0x34: case 0x35: case 0x36: case 0x37: {
            u16 v = read16(); name = "ld"; lhs.set_register3(2, fetch); rhs.set_immediate(16, v); break;
        }
        case 0x38: case 0x39: case 0x3a: case 0x3b:
        case 0x3c: case 0x3d: case 0x3e: case 0x3f:
            name = "push"; lhs.set_register3(4, fetch); break;

        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47: {
            u32 v = read32(); name = "ld"; lhs.set_register3(4, fetch); rhs.set_immediate(32, v); break;
        }
        case 0x48: case 0x49: case 0x4a: case 0x4b:
        case 0x4c: case 0x4d: case 0x4e: case 0x4f:
            name = "pop"; lhs.set_register3(2, fetch); break;

        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            break;

        case 0x58: case 0x59: case 0x5a: case 0x5b:
        case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            name = "pop"; lhs.set_register3(4, fetch); break;

        case 0x60: case 0x61: case 0x62: case 0x63:
        case 0x64: case 0x65: case 0x66: case 0x67:
        case 0x68: case 0x69: case 0x6a: case 0x6b:
        case 0x6c: case 0x6d: case 0x6e: case 0x6f: {
            i32 d = static_cast<i8>(read8()); name = "jr";
            lhs.set_condition(fetch & 0x0f); rhs.set_displacementPC(1, d); break;
        }
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7a: case 0x7b:
        case 0x7c: case 0x7d: case 0x7e: case 0x7f: {
            i32 d = static_cast<i16>(read16()); name = "jrl";
            lhs.set_condition(fetch & 0x0f); rhs.set_displacementPC(16, d); break;
        }

        case 0x80: case 0x81: case 0x82: case 0x83:
        case 0x84: case 0x85: case 0x86: case 0x87:
            opSourceMemory = true; lhs.set_indirect_register3(1, fetch); break;

        case 0x88: case 0x89: case 0x8a: case 0x8b:
        case 0x8c: case 0x8d: case 0x8e: case 0x8f: {
            i32 d = static_cast<i8>(read8()); opSourceMemory = true; lhs.set_indirect_disp8(1, fetch, d); break;
        }
        case 0x90: case 0x91: case 0x92: case 0x93:
        case 0x94: case 0x95: case 0x96: case 0x97:
            opSourceMemory = true; lhs.set_indirect_register3(2, fetch); break;

        case 0x98: case 0x99: case 0x9a: case 0x9b:
        case 0x9c: case 0x9d: case 0x9e: case 0x9f: {
            i32 d = static_cast<i8>(read8()); opSourceMemory = true; lhs.set_indirect_disp8(2, fetch, d); break;
        }
        case 0xa0: case 0xa1: case 0xa2: case 0xa3:
        case 0xa4: case 0xa5: case 0xa6: case 0xa7:
            opSourceMemory = true; lhs.set_indirect_register3(4, fetch); break;

        case 0xa8: case 0xa9: case 0xaa: case 0xab:
        case 0xac: case 0xad: case 0xae: case 0xaf: {
            i32 d = static_cast<i8>(read8()); opSourceMemory = true; lhs.set_indirect_disp8(4, fetch, d); break;
        }
        case 0xb0: case 0xb1: case 0xb2: case 0xb3:
        case 0xb4: case 0xb5: case 0xb6: case 0xb7:
            opTargetMemory = true; lhs.set_indirect_register3(0, fetch); break;

        case 0xb8: case 0xb9: case 0xba: case 0xbb:
        case 0xbc: case 0xbd: case 0xbe: case 0xbf: {
            i32 d = static_cast<i8>(read8()); opTargetMemory = true; lhs.set_indirect_disp8(0, fetch, d); break;
        }

        case 0xc0: case 0xd0: case 0xe0: case 0xf0: {
            u8 a = read8();
            if (fetch < 0xf0) { opSourceMemory = true; lhs.set_indirect_imm8(OPSIZE, a); }
            else { opTargetMemory = true; lhs.set_indirect_imm8(0, a); }
            break;
        }
        case 0xc1: case 0xd1: case 0xe1: case 0xf1: {
            u16 a = read16();
            if (fetch < 0xf0) { opSourceMemory = true; lhs.set_indirect_imm16(OPSIZE, a); }
            else { opTargetMemory = true; lhs.set_indirect_imm16(0, a); }
            break;
        }
        case 0xc2: case 0xd2: case 0xe2: case 0xf2: {
            u32 a = read24();
            if (fetch < 0xf0) { opSourceMemory = true; lhs.set_indirect_imm24(OPSIZE, a); }
            else { opTargetMemory = true; lhs.set_indirect_imm24(0, a); }
            break;
        }
        case 0xc3: case 0xd3: case 0xe3: case 0xf3: {
            bool is_src = (fetch < 0xf0);
            u32 sz = OPSIZE;
            if (is_src) opSourceMemory = true; else opTargetMemory = true;

            u8 d = read8();
            if ((d & 3) == 0) {
                if (is_src) lhs.set_indirect_register(sz, d);
                else lhs.set_indirect_register(0, d);
            } else if ((d & 3) == 1) {
                i32 dd = static_cast<i16>(read16());
                if (is_src) lhs.set_indirect_disp16(sz, d, dd);
                else lhs.set_indirect_disp16(0, d, dd);
            } else if (d == 0x03) {
                u8 r32 = read8(), r8 = read8();
                if (is_src) lhs.set_indirect_reg8(sz, r32, r8);
                else lhs.set_indirect_reg8(0, r32, r8);
            } else if (d == 0x07) {
                u8 r32 = read8(), r16 = read8();
                if (is_src) lhs.set_indirect_reg16(sz, r32, r16);
                else lhs.set_indirect_reg16(0, r32, r16);
            } else if (d == 0x13 && fetch == 0xf3) {
                i32 dd = static_cast<i16>(read16());
                u32 target = PC + static_cast<u32>(static_cast<i32>(dd));
                lhs.set_indirect_imm24(0, target);
            }
            break;
        }
        case 0xc4: case 0xd4: case 0xe4: case 0xf4: {
            u8 r = read8();
            if (fetch < 0xf0) { opSourceMemory = true; lhs.set_indirect_decrement(OPSIZE, r); }
            else { opTargetMemory = true; lhs.set_indirect_decrement(0, r); }
            break;
        }
        case 0xc5: case 0xd5: case 0xe5: case 0xf5: {
            u8 r = read8();
            if (fetch < 0xf0) { opSourceMemory = true; lhs.set_indirect_increment(OPSIZE, r); }
            else { opTargetMemory = true; lhs.set_indirect_increment(0, r); }
            break;
        }
        case 0xc6: case 0xd6: case 0xe6: case 0xf6: break;

        case 0xc7: { u8 r = read8(); opRegister = true; lhs.set_register(1, r); break; }
        case 0xd7: { u8 r = read8(); opRegister = true; lhs.set_register(2, r); break; }
        case 0xe7: { u8 r = read8(); opRegister = true; lhs.set_register(4, r); break; }

        case 0xf7: {
            read8(); u8 a = read8();
            read8(); u8 v = read8();
            read8();
            name = "ldx";
            lhs.set_indirect_imm8(1, a);
            rhs.set_immediate(8, v);
            break;
        }

        case 0xc8: case 0xc9: case 0xca: case 0xcb:
        case 0xcc: case 0xcd: case 0xce: case 0xcf:
            opRegister = true; lhs.set_register3(1, fetch); break;
        case 0xd8: case 0xd9: case 0xda: case 0xdb:
        case 0xdc: case 0xdd: case 0xde: case 0xdf:
            opRegister = true; lhs.set_register3(2, fetch); break;
        case 0xe8: case 0xe9: case 0xea: case 0xeb:
        case 0xec: case 0xed: case 0xee: case 0xef:
            opRegister = true; lhs.set_register3(4, fetch); break;

        case 0xf8: case 0xf9: case 0xfa: case 0xfb:
        case 0xfc: case 0xfd: case 0xfe: case 0xff:
            name = "swi"; lhs.set_immediate(3, fetch & 7); break;
    }
#undef OPSIZE

    if (opRegister) {
        u8 f2 = read8();
        u32 sz = lhs.size;
        const u8 ID_A = 0xe0;

        if (f2 >= 0x40 && f2 <= 0x47) { name = "mul"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x48 && f2 <= 0x4f) { name = "muls"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x50 && f2 <= 0x57) { name = "div"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x58 && f2 <= 0x5f) { name = "divs"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x60 && f2 <= 0x67) {
            name = "inc";
            rhs.set_immediate(4, (f2 & 7) ? static_cast<u32>((f2 & 7)) : 8u);
        }
        else if (f2 >= 0x68 && f2 <= 0x6f) {
            name = "dec";
            rhs.set_immediate(4, (f2 & 7) ? static_cast<u32>((f2 & 7)) : 8u);
        }
        else if (f2 >= 0x70 && f2 <= 0x7f) { name = "scc"; rhs = lhs; lhs.set_condition(f2 & 0x0f); }
        else if (f2 >= 0x80 && f2 <= 0x87) { name = "add"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x88 && f2 <= 0x8f) { name = "ld"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x90 && f2 <= 0x97) { name = "adc"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x98 && f2 <= 0x9f) { name = "ld"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0xa0 && f2 <= 0xa7) { name = "sub"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xa8 && f2 <= 0xaf) { name = "ld"; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xb0 && f2 <= 0xb7) { name = "sbb"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xb8 && f2 <= 0xbf) { name = "ex"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xc0 && f2 <= 0xc7) { name = "and"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 == 0xc8) { name = "add"; rhs.set_immediate(sz * 8u, reads(sz)); }
        else if (f2 == 0xc9) { name = "adc"; rhs.set_immediate(sz * 8u, reads(sz)); }
        else if (f2 == 0xca) { name = "sub"; rhs.set_immediate(sz * 8u, reads(sz)); }
        else if (f2 == 0xcb) { name = "sbb"; rhs.set_immediate(sz * 8u, reads(sz)); }
        else if (f2 == 0xcc) { name = "and"; rhs.set_immediate(sz * 8u, reads(sz)); }
        else if (f2 == 0xcd) { name = "xor"; rhs.set_immediate(sz * 8u, reads(sz)); }
        else if (f2 == 0xce) { name = "or"; rhs.set_immediate(sz * 8u, reads(sz)); }
        else if (f2 == 0xcf) { name = "cp"; rhs.set_immediate(sz * 8u, reads(sz)); }
        else if (f2 >= 0xd0 && f2 <= 0xd7) { name = "xor"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xd8 && f2 <= 0xdf) { name = "cp"; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xe0 && f2 <= 0xe7) { name = "or"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 == 0xe8) { u8 v = read8(); name = "rlc"; rhs.set_immediate(5, v ? static_cast<u32>(v) : 16u); }
        else if (f2 == 0xe9) { u8 v = read8(); name = "rrc"; rhs.set_immediate(5, v ? static_cast<u32>(v) : 16u); }
        else if (f2 == 0xea) { u8 v = read8(); name = "rl"; rhs.set_immediate(5, v ? static_cast<u32>(v) : 16u); }
        else if (f2 == 0xeb) { u8 v = read8(); name = "rr"; rhs.set_immediate(5, v ? static_cast<u32>(v) : 16u); }
        else if (f2 == 0xec) { u8 v = read8(); name = "sla"; rhs.set_immediate(5, v ? static_cast<u32>(v) : 16u); }
        else if (f2 == 0xed) { u8 v = read8(); name = "sra"; rhs.set_immediate(5, v ? static_cast<u32>(v) : 16u); }
        else if (f2 == 0xee) { u8 v = read8(); name = "sll"; rhs.set_immediate(5, v ? static_cast<u32>(v) : 16u); }
        else if (f2 == 0xef) { u8 v = read8(); name = "srl"; rhs.set_immediate(5, v ? static_cast<u32>(v) : 16u); }
        else if (f2 >= 0xf0 && f2 <= 0xf7) { name = "cp"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 == 0xf8) { name = "rlc"; rhs.set_register(1, ID_A); }
        else if (f2 == 0xf9) { name = "rrc"; rhs.set_register(1, ID_A); }
        else if (f2 == 0xfa) { name = "rl"; rhs.set_register(1, ID_A); }
        else if (f2 == 0xfb) { name = "rr"; rhs.set_register(1, ID_A); }
        else if (f2 == 0xfc) { name = "sla"; rhs.set_register(1, ID_A); }
        else if (f2 == 0xfd) { name = "sra"; rhs.set_register(1, ID_A); }
        else if (f2 == 0xfe) { name = "sll"; rhs.set_register(1, ID_A); }
        else if (f2 == 0xff) { name = "srl"; rhs.set_register(1, ID_A); }
        else {
            switch (f2) {
                case 0x00: case 0x01: case 0x02: break;
                case 0x03: { u32 v = reads(sz); name = "ld"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x04: name = "push"; break;
                case 0x05: name = "pop"; break;
                case 0x06: name = "cpl"; break;
                case 0x07: name = "neg"; break;
                case 0x08: { u32 v = reads(sz); name = "mul";
                             rhs.set_immediate(sz * 8u, v); lhs.set_register3(2, op[0]); break; }
                case 0x09: { u32 v = reads(sz); name = "muls";
                             rhs.set_immediate(sz * 8u, v); lhs.set_register3(2, op[0]); break; }
                case 0x0a: { u32 v = reads(sz); name = "div";
                             rhs.set_immediate(sz * 8u, v); lhs.set_register3(2, op[0]); break; }
                case 0x0b: { u32 v = reads(sz); name = "divs";
                             rhs.set_immediate(sz * 8u, v); lhs.set_register3(2, op[0]); break; }
                case 0x0c: { i32 d = static_cast<i16>(read16()); name = "link"; rhs.set_displacement(16, d); break; }
                case 0x0d: name = "unlk"; break;
                case 0x0e: { name = "bs1f"; rhs = lhs; lhs.set_register(1, ID_A); break; }
                case 0x0f: { name = "bs1b"; rhs = lhs; lhs.set_register(1, ID_A); break; }
                case 0x10: name = "daa"; break;
                case 0x11: break;
                case 0x12: name = "extz"; break;
                case 0x13: name = "exts"; break;
                case 0x14: name = "paa"; break;
                case 0x15: break;
                case 0x16: name = "mirr"; break;
                case 0x17: case 0x18: break;
                case 0x19: name = "mula"; break;
                case 0x1a: case 0x1b: break;
                case 0x1c: { i32 d = static_cast<i8>(read8()); name = "djnz"; rhs.set_displacementPC(1, d); break; }
                case 0x1d: case 0x1e: case 0x1f: break;
                case 0x20: { u8 v = read8(); name = "andcf"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x21: { u8 v = read8(); name = "orcf"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x22: { u8 v = read8(); name = "xorcf"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x23: { u8 v = read8(); name = "ldcf"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x24: { u8 v = read8(); name = "stcf"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x25: case 0x26: case 0x27: break;
                case 0x28: { name = "andcf"; rhs.set_register(1, ID_A); break; }
                case 0x29: { name = "orcf"; rhs.set_register(1, ID_A); break; }
                case 0x2a: { name = "xorcf"; rhs.set_register(1, ID_A); break; }
                case 0x2b: { name = "ldcf"; rhs.set_register(1, ID_A); break; }
                case 0x2c: { name = "stcf"; rhs.set_register(1, ID_A); break; }
                case 0x2d: break;
                case 0x2e: { u8 cr = read8(); name = "ldc"; rhs = lhs; lhs.set_control(sz, cr); break; }
                case 0x2f: { u8 cr = read8(); name = "ldc"; rhs.set_control(sz, cr); break; }
                case 0x30: { u8 v = read8(); name = "res"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x31: { u8 v = read8(); name = "set"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x32: { u8 v = read8(); name = "chg"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x33: { u8 v = read8(); name = "bit"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x34: { u8 v = read8(); name = "tset"; rhs.set_immediate(4, v & 0x0f); break; }
                case 0x35: case 0x36: case 0x37: break;
                case 0x38: { u16 v = read16(); name = "minc1"; rhs.set_immediate(16, v); break; }
                case 0x39: { u16 v = read16(); name = "minc2"; rhs.set_immediate(16, v); break; }
                case 0x3a: { u16 v = read16(); name = "minc4"; rhs.set_immediate(16, v); break; }
                case 0x3b: break;
                case 0x3c: { u16 v = read16(); name = "mdec1"; rhs.set_immediate(16, v); break; }
                case 0x3d: { u16 v = read16(); name = "mdec2"; rhs.set_immediate(16, v); break; }
                case 0x3e: { u16 v = read16(); name = "mdec4"; rhs.set_immediate(16, v); break; }
                case 0x3f: break;
                default: break;
            }
        }
    }

    if (opSourceMemory) {
        u8 f2 = read8();
        u32 sz = lhs.size;
        const u8 ID_A = 0xe0;
        const u8 ID_WA = 0xe0;

        const u8 ID_XDE = 0xe8, ID_XHL = 0xec;
        const u8 ID_XIX = 0xf0, ID_XIY = 0xf4;
        u8 dst_base = ((op[0] & 7) != 5) ? ID_XDE : ID_XIX;
        u8 src_base = ((op[0] & 7) != 5) ? ID_XHL : ID_XIY;

        if (f2 >= 0x20 && f2 <= 0x27) { name = "ld"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x30 && f2 <= 0x37) { name = "ex"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0x40 && f2 <= 0x47) { name = "mul"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x48 && f2 <= 0x4f) { name = "muls"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x50 && f2 <= 0x57) { name = "div"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x58 && f2 <= 0x5f) { name = "divs"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x60 && f2 <= 0x67) {
            name = "inc";
            rhs.set_immediate(4, (f2 & 7) ? static_cast<u32>((f2 & 7)) : 8u);
        }
        else if (f2 >= 0x68 && f2 <= 0x6f) {
            name = "dec";
            rhs.set_immediate(4, (f2 & 7) ? static_cast<u32>((f2 & 7)) : 8u);
        }
        else if (f2 >= 0x80 && f2 <= 0x87) { name = "add"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x88 && f2 <= 0x8f) { name = "add"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0x90 && f2 <= 0x97) { name = "adc"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0x98 && f2 <= 0x9f) { name = "adc"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0xa0 && f2 <= 0xa7) { name = "sub"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xa8 && f2 <= 0xaf) { name = "sub"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0xb0 && f2 <= 0xb7) { name = "sbb"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xb8 && f2 <= 0xbf) { name = "sbb"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0xc0 && f2 <= 0xc7) { name = "and"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xc8 && f2 <= 0xcf) { name = "and"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0xd0 && f2 <= 0xd7) { name = "xor"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xd8 && f2 <= 0xdf) { name = "xor"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0xe0 && f2 <= 0xe7) { name = "or"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xe8 && f2 <= 0xef) { name = "or"; rhs.set_register3(sz, f2); }
        else if (f2 >= 0xf0 && f2 <= 0xf7) { name = "cp"; rhs = lhs; lhs.set_register3(sz, f2); }
        else if (f2 >= 0xf8 && f2 <= 0xff) { name = "cp"; rhs.set_register3(sz, f2); }
        else {
            switch (f2) {
                case 0x00: case 0x01: case 0x02: case 0x03: break;
                case 0x04: name = (sz == 1) ? "push" : "pushw"; break;
                case 0x05: break;
                case 0x06: name = "rld"; rhs = lhs; lhs.set_register(1, ID_A); break;
                case 0x07: name = "rrd"; rhs = lhs; lhs.set_register(1, ID_A); break;
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                case 0x0c: case 0x0d: case 0x0e: case 0x0f: break;
                case 0x10:
                    name = (sz == 1) ? "ldi" : "ldiw";
                    lhs.set_indirect_increment(4, dst_base);
                    rhs.set_indirect_increment(4, src_base); break;
                case 0x11:
                    name = (sz == 1) ? "ldir" : "ldirw";
                    lhs.set_indirect_increment(4, dst_base);
                    rhs.set_indirect_increment(4, src_base); break;
                case 0x12:
                    name = (sz == 1) ? "ldd" : "lddw";
                    lhs.set_indirect_increment(4, dst_base);
                    rhs.set_indirect_increment(4, src_base); break;
                case 0x13:
                    name = (sz == 1) ? "lddr" : "lddrw";
                    lhs.set_indirect_increment(4, dst_base);
                    rhs.set_indirect_increment(4, src_base); break;
                case 0x14:
                    name = (sz == 1) ? "cpi" : "cpiw";
                    lhs.set_indirect_increment3(4, op[0]);
                    rhs.set_register(sz, sz == 1 ? ID_A : ID_WA); break;
                case 0x15:
                    name = (sz == 1) ? "cpir" : "cpirw";
                    lhs.set_indirect_increment3(4, op[0]);
                    rhs.set_register(sz, sz == 1 ? ID_A : ID_WA); break;
                case 0x16:
                    name = (sz == 1) ? "cpd" : "cpdw";
                    lhs.set_indirect_increment3(4, op[0]);
                    rhs.set_register(sz, sz == 1 ? ID_A : ID_WA); break;
                case 0x17:
                    name = (sz == 1) ? "cpdr" : "cpdrw";
                    lhs.set_indirect_increment3(4, op[0]);
                    rhs.set_register(sz, sz == 1 ? ID_A : ID_WA); break;
                case 0x18: break;
                case 0x19: { u16 nn = read16(); name = "ld"; rhs = lhs; lhs.set_indirect_imm16(2, nn); break; }
                case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f: break;
                case 0x38: { u32 v = reads(sz); name = "add"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x39: { u32 v = reads(sz); name = "adc"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x3a: { u32 v = reads(sz); name = "sub"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x3b: { u32 v = reads(sz); name = "sbb"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x3c: { u32 v = reads(sz); name = "and"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x3d: { u32 v = reads(sz); name = "xor"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x3e: { u32 v = reads(sz); name = "or"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x3f: { u32 v = reads(sz); name = "cp"; rhs.set_immediate(sz * 8u, v); break; }
                case 0x70: case 0x71: case 0x72: case 0x73:
                case 0x74: case 0x75: case 0x76: case 0x77: break;
                case 0x78: name = "rlc"; break;
                case 0x79: name = "rrc"; break;
                case 0x7a: name = "rl"; break;
                case 0x7b: name = "rr"; break;
                case 0x7c: name = "sla"; break;
                case 0x7d: name = "sra"; break;
                case 0x7e: name = "sll"; break;
                case 0x7f: name = "srl"; break;
                default: break;
            }
        }
    }

    if (opTargetMemory) {
        u8 f2 = read8();
        const u8 ID_A = 0xe0;

        if (f2 >= 0x20 && f2 <= 0x27) { name = "lda"; lhs.size = 2; rhs = lhs; lhs.set_register3(2, f2); }
        else if (f2 >= 0x30 && f2 <= 0x37) { name = "lda"; lhs.size = 4; rhs = lhs; lhs.set_register3(4, f2); }
        else if (f2 >= 0x40 && f2 <= 0x47) { name = "ld"; lhs.size = 1; rhs.set_register3(1, f2); }
        else if (f2 >= 0x50 && f2 <= 0x57) { name = "ld"; lhs.size = 2; rhs.set_register3(2, f2); }
        else if (f2 >= 0x60 && f2 <= 0x67) { name = "ld"; lhs.size = 4; rhs.set_register3(4, f2); }
        else if (f2 >= 0x80 && f2 <= 0x87) { name = "andcf"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0x88 && f2 <= 0x8f) { name = "orcf"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0x90 && f2 <= 0x97) { name = "xorcf"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0x98 && f2 <= 0x9f) { name = "ldcf"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xa0 && f2 <= 0xa7) { name = "stcf"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xa8 && f2 <= 0xaf) { name = "tset"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xb0 && f2 <= 0xb7) { name = "res"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xb8 && f2 <= 0xbf) { name = "set"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xc0 && f2 <= 0xc7) { name = "chg"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xc8 && f2 <= 0xcf) { name = "bit"; lhs.size = 1; rhs.set_immediate(3, f2 & 7); }
        else if (f2 >= 0xd0 && f2 <= 0xdf) {
            name = "jp"; lhs.size = 4; rhs = lhs; lhs.set_condition(f2 & 0x0f);
        }
        else if (f2 >= 0xe0 && f2 <= 0xef) {
            name = "call"; lhs.size = 4; rhs = lhs; lhs.set_condition(f2 & 0x0f);
        }
        else if (f2 >= 0xf0 && f2 <= 0xff) {
            name = "ret"; lhs.set_condition(f2 & 0x0f);
        }
        else {
            switch (f2) {
                case 0x00: { u8 v = read8(); name = "ld"; lhs.size = 1; rhs.set_immediate(8, v); break; }
                case 0x01: break;
                case 0x02: { u16 v = read16(); name = "ld"; lhs.size = 2; rhs.set_immediate(16, v); break; }
                case 0x03: break;
                case 0x04: name = "pop"; lhs.size = 1; break;
                case 0x05: break;
                case 0x06: name = "pop"; lhs.size = 2; break;
                case 0x14: { u16 nn = read16(); name = "ld"; lhs.size = 1; rhs.set_indirect_imm16(2, nn); break; }
                case 0x15: break;
                case 0x16: { u16 nn = read16(); name = "ld"; lhs.size = 2; rhs.set_indirect_imm16(2, nn); break; }
                case 0x28: { name = "andcf"; lhs.size = 1; rhs.set_register(1, ID_A); break; }
                case 0x29: { name = "orcf"; lhs.size = 1; rhs.set_register(1, ID_A); break; }
                case 0x2a: { name = "xorcf"; lhs.size = 1; rhs.set_register(1, ID_A); break; }
                case 0x2b: { name = "ldcf"; lhs.size = 1; rhs.set_register(1, ID_A); break; }
                case 0x2c: { name = "stcf"; lhs.size = 1; rhs.set_register(1, ID_A); break; }
                default: break;
            }
        }
    }

    if (lhs.mode == OM_Condition && lhs.cond == 8) {
        lhs = rhs;
        rhs.null();
    }

    char lhs_buf[80] = {};
    char rhs_buf[80] = {};
    u32 pc_after = PC;

    if (name) {
        format_operand(lhs, pc_after, lhs_buf, sizeof(lhs_buf));
        format_operand(rhs, pc_after, rhs_buf, sizeof(rhs_buf));

        if (lhs.valid() && rhs.valid()) {
            outstr.sprintf("%-6s %s,%s", name, lhs_buf, rhs_buf);
        } else if (lhs.valid()) {
            outstr.sprintf("%-6s %s", name, lhs_buf);
        } else {
            outstr.sprintf("%-6s", name);
        }
    } else {
        char hex_buf[80] = {};
        char *p = hex_buf;
        size_t rem = sizeof(hex_buf);
        for (u8 i = 0; i < ops && rem > 1; i++) {
            int w = snprintf(p, rem, i == 0 ? "0x%02x" : ",0x%02x", op[i]);
            if (w > 0 && static_cast<size_t>(w) < rem) { p += w; rem -= static_cast<size_t>(w); }
        }
        outstr.sprintf("???    %s", hex_buf);
    }
}

static u32 peek_read_trace(void *ptr, u32 addr)
{
    core *c = static_cast<core *>(ptr);
    if (c->read8_peek)
        return c->read8_peek(c->mem_ptr, addr);
    return 0xFF;
}

void disassemble_entry(core &cpu, disassembly_entry &entry)
{
    entry.dasm.quickempty();
    entry.context.quickempty();

    u32 PC = entry.addr;

    jsm_debug_read_trace local_trace{};
    local_trace.ptr = &cpu;
    local_trace.read_trace = peek_read_trace;

    disassemble(cpu, PC, local_trace, entry.dasm);
    entry.ins_size_bytes = PC - entry.addr;

    entry.context.sprintf(
        "XWA:%08x XBC:%08x XDE:%08x XHL:%08x "
        "XIX:%08x XIY:%08x XIZ:%08x XSP:%08x "
        "IFF:%d RFP:%d %c%c%c%c%c%c",
        cpu.regs.reg32(0xe0),
        cpu.regs.reg32(0xe4),
        cpu.regs.reg32(0xe8),
        cpu.regs.reg32(0xec),
        cpu.regs.reg32(0xf0),
        cpu.regs.reg32(0xf4),
        cpu.regs.reg32(0xf8),
        cpu.regs.reg32(0xfc),
        static_cast<i32>(cpu.regs.SR.IFF),
        static_cast<i32>(cpu.regs.SR.RFP),
        cpu.regs.SR.S ? 'S' : 's',
        cpu.regs.SR.Z ? 'Z' : 'z',
        cpu.regs.SR.H ? 'H' : 'h',
        cpu.regs.SR.V ? 'V' : 'v',
        cpu.regs.SR.N ? 'N' : 'n',
        cpu.regs.SR.C ? 'C' : 'c'
    );
}

}
