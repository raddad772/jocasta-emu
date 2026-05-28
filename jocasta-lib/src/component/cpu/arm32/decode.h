#pragma once

#define TVOID template <armtype cpukind, typename scheduler_kind>void core<cpukind, scheduler_kind>::
#define TU32 template <armtype cpukind, typename scheduler_kind>u32 core<cpukind, scheduler_kind>::
#define TU32P template <armtype cpukind, typename scheduler_kind>u32 *core<cpukind, scheduler_kind>::
#define OB(a,b) (0b ## a ## b)
#define OPCd(n,a,b) if ((opc & a) == b) return AIK::n
#define PC R[15]

#define THUMB_OP_TAG(name) \
struct thumb_op_##name { \
template<typename Core, bool do_debug, bool cached, bool... Bs> \
static constexpr auto fn = &Core::template THUMB_ins_##name<do_debug, cached, Bs...>; \
};

#define THUMB_OP_TAG_U8(name) \
struct thumb_op_##name { \
template<typename Core, bool do_debug, bool cached, u8 V> \
static constexpr auto fn = &Core::template THUMB_ins_##name<do_debug, cached, V>; \
};

template<typename Op, typename Core, typename Ins, auto Index, auto Count, bool cached, bool... Bs
>
static inline void set_thumb_exec_boolpack_impl(Ins &out, const bool (&flags)[Count]) {
    if constexpr (Index == Count) {
        out.exec       = Op::template fn<Core, false, cached, Bs...>;
        out.exec_debug = Op::template fn<Core, true,  cached, Bs...>;
    }
    else {
        if (flags[Index]) {
            set_thumb_exec_boolpack_impl<
                Op, Core, Ins, Index + 1, Count, cached, Bs..., true
            >(out, flags);
        }
        else {
            set_thumb_exec_boolpack_impl<
                Op, Core, Ins, Index + 1, Count, cached, Bs..., false
            >(out, flags);
        }
    }
}

template<typename Op, typename Core, bool cached, auto Count, typename Ins>
static inline void set_thumb_exec_boolpack(Ins &out, const bool (&flags)[Count]) {
    set_thumb_exec_boolpack_impl<Op, Core, Ins, 0, Count, cached>(out, flags);
}

#define SET_THUMB_EXEC_U8_0_3(name, value) do { \
using this_core = core<cpukind, scheduler_kind>; \
set_thumb_exec_u8_0_3<thumb_op_##name, this_core, cached>(ins, static_cast<u8>(value)); \
} while (0)

#define SET_THUMB_EXEC_BOOLPACK(name, ...) do { \
using this_core = core<cpukind, scheduler_kind>; \
const bool thumb_exec_flags[] = { __VA_ARGS__ }; \
set_thumb_exec_boolpack<thumb_op_##name, this_core, cached>(ins, thumb_exec_flags); \
} while (0)

#define SET_THUMB_EXEC_0BOOL(name) do { \
using this_core = core<cpukind, scheduler_kind>; \
ins.exec       = &this_core::template THUMB_ins_##name<false, cached>; \
ins.exec_debug = &this_core::template THUMB_ins_##name<true,  cached>; \
} while (0)

THUMB_OP_TAG(INVALID);
THUMB_OP_TAG(ADD_SUB);
THUMB_OP_TAG_U8(LSL_LSR_ASR)
THUMB_OP_TAG_U8(MOV_CMP_ADD_SUB)
THUMB_OP_TAG_U8(data_proc)
THUMB_OP_TAG(BX_BLX);
THUMB_OP_TAG_U8(ADD_CMP_MOV_hi);
THUMB_OP_TAG(LDR_PC_relative);
THUMB_OP_TAG(LDRH_STRH_reg_offset);
THUMB_OP_TAG(LDRSH_LDRSB_reg_offset);
THUMB_OP_TAG(LDR_STR_reg_offset);
THUMB_OP_TAG(LDRB_STRB_reg_offset);
THUMB_OP_TAG(LDR_STR_imm_offset);
THUMB_OP_TAG(LDRB_STRB_imm_offset);
THUMB_OP_TAG(LDRH_STRH_imm_offset);
THUMB_OP_TAG(LDR_STR_SP_relative);
THUMB_OP_TAG(ADD_SP_or_PC);
THUMB_OP_TAG(ADD_SUB_SP);
THUMB_OP_TAG(PUSH_POP);
THUMB_OP_TAG(LDM_STM);
THUMB_OP_TAG(SWI);
THUMB_OP_TAG(UNDEFINED_BCC);
THUMB_OP_TAG(BCC);
THUMB_OP_TAG(B);
THUMB_OP_TAG(BL_BLX_prefix);
THUMB_OP_TAG(BL_suffix);
THUMB_OP_TAG(BKPT);
THUMB_OP_TAG(BLX_suffix);

template<armtype cpukind>
inline AIK::ins_kind decode_arm32(u32 opc)
{
    OPCd(MUL_MLA,                   OB(11111100,1111), OB(00000000,1001)); // .... 000'000.. 1001  MUL, MLA
    if constexpr (cpukind >= AT_ARM7TDMI) {
        OPCd(MULL_MLAL,                 OB(11111000,1111), OB(00001000,1001)); // .... 000'01... 1001  MULL, MLAL
    }
    if constexpr (cpukind >= AT_ARM946ES) {
        OPCd(SMLAxy,                    OB(11111111,1001), OB(00010000,1000)); // .... 000'10000 1..0  SMLAxy
        OPCd(SMLAWy,                    OB(11111111,1011), OB(00010010,1000)); // .... 000'10010 1.00  SMLAWy
        OPCd(SMULWy,                    OB(11111111,1011), OB(00010010,1010)); // .... 000'10010 1.10  SMULWy
        OPCd(SMLALxy,                   OB(11111111,1001), OB(00010100,1000)); // .... 000'10100 1..0  SMLALxy
        OPCd(SMULxy,                    OB(11111111,1001), OB(00010110,1000)); // .... 000'10110 1..0  SMULxy
    }
    OPCd(SWP,                       OB(11111011,1111), OB(00010000,1001)); // .... 000'10.00 1001  SWP
    OPCd(LDRH_STRH,                 OB(11100000,1111), OB(00000000,1011)); // .... 000'..... 1011  LDRH, STRH
    if constexpr (cpukind >= AT_ARM946ES) {
        OPCd(LDRD_STRD,                 OB(11100001,1101), OB(00000000,1101)); // .... 000'....0 11.1  LDRD, STRD
    }
    OPCd(LDRSB_LDRSH,               OB(11100001,1101), OB(00000001,1101)); // .... 000'....1 11.1  LDRSB, LDRSH
    OPCd(MRS,                       OB(11111011,1111), OB(00010000,0000)); // .... 000'10.00 0000  MRS
    OPCd(MSR_reg,                   OB(11111011,1111), OB(00010010,0000)); // .... 000'10.10 0000  MSR (register)
    OPCd(MSR_imm,                   OB(11111011,0000), OB(00110010,0000)); // .... 001'10.10 ....  MSR (immediate)
    OPCd(BX,                        OB(11111111,1111), OB(00010010,0001)); // .... 000'10010 0001  BX
    if constexpr (cpukind >= AT_ARM946ES) {
        OPCd(CLZ,                       OB(11111111,1111), OB(00010110,0001)); // .... 000'10110 0001  CLZ
        OPCd(BLX_reg,                   OB(11111111,1111), OB(00010010,0011)); // .... 000'10010 0011  BLX (register)
        OPCd(QADD_QSUB_QDADD_QDSUB,     OB(11111001,1111), OB(00010000,0101)); // .... 000'10..0 0101  QADD, QSUB, QDADD, QDSUB
        OPCd(BKPT,                      OB(11111111,1111), OB(00010010,0111)); // .... 000'10010 0111  BKPT
    }
    OPCd(data_proc_immediate_shift, OB(11100000,0001), OB(00000000,0000)); // .... 000'..... ...0  Data Processing (immediate shift)
    OPCd(data_proc_register_shift,  OB(11100000,1001), OB(00000000,0001)); // .... 000'..... 0..1  Data Processing (register shift)
    OPCd(data_proc_undefined,       OB(11111011,0000), OB(00110000,0000)); // .... 001'10.00 ....  Undefined instructions in Data Processing
    OPCd(data_proc_immediate,       OB(11100000,0000), OB(00100000,0000)); // .... 001'..... ....  Data Processing (immediate value)
    OPCd(LDR_STR_immediate_offset,  OB(11100000,0000), OB(01000000,0000)); // .... 010'..... ....  LDR, STR (immediate offset)
    OPCd(LDR_STR_register_offset,   OB(11100000,0001), OB(01100000,0000)); // .... 011'..... ...0  LDR, STR (register offset)
    OPCd(LDM_STM,                   OB(11100000,0000), OB(10000000,0000)); // .... 100'..... ....  LDM, STM
    OPCd(B_BL,                      OB(11100000,0000), OB(10100000,0000)); // .... 101'..... ....  B, BL, BLX
    OPCd(STC_LDC,                   OB(11100000,0000), OB(11000000,0000)); // .... 110'..... ....  STC, LDC
    OPCd(CDP,                       OB(11110000,0001), OB(11100000,0000)); // .... 111'0.... ...0  CDP
    OPCd(MCR_MRC,                   OB(11110000,0001), OB(11100000,0001)); // .... 111'0.... ...1  MCR, MRC
    OPCd(SWI,                       OB(11110000,0000), OB(11110000,0000)); // .... 111'1.... ....  SWI
    return AIK::INVALID;
}

inline AIK::ins_kind decode_arm32_never(u32 opc)
{
    OPCd(PLD,                       OB(11010111,0000), OB(01010101,0000)); // 1111 01.'1.101 ....  PLD
    OPCd(undefined_instruction,     OB(10000000,0000), OB(00000000,0000)); // 1111 0..'..... ....  Undefined
    OPCd(undefined_instruction,     OB(11100000,0000), OB(10000000,0000)); // 1111 100'..... ....  Undefined
    OPCd(undefined_instruction,     OB(11110000,0000), OB(11110000,0000)); // 1111 111'1.... ....  Undefined
    OPCd(BLX,                       OB(11100000,0000), OB(10100000,0000)); // .... 101'..... ....  B, BL, BLX
    OPCd(MCR_MRC,                   OB(11110000,0001), OB(11100000,0001)); // .... 111'0.... ...1  MCR, MRC
    return AIK::INVALID;
}
#undef OPCd
#undef OB

inline TIK::ins_kind decode_thumb32(u16 opc)
{
    //ins_no->opcode = ins_yes->opcode = opc;
#define OPCd(n, mask, val) if ((opc & mask) == val) return TIK::n
    OPCd(ADD_SUB, 0b1111100000000000, 0b0001100000000000);
    OPCd(LSL_LSR_ASR, 0b1110000000000000, 0b0000000000000000);
    OPCd(MOV_CMP_ADD_SUB, 0b1110000000000000, 0b0010000000000000);
    OPCd(data_proc, 0b1111110000000000, 0b0100000000000000);
    OPCd(BX_BLX, 0b1111111100000000, 0b0100011100000000);
    OPCd(ADD_CMP_MOV_hi, 0b1111110000000000, 0b0100010000000000);
    OPCd(LDR_PC_relative, 0b1111100000000000, 0b0100100000000000);
    OPCd(LDRH_STRH_reg_offset, 0b1111011000000000, 0b0101001000000000);
    OPCd(LDRSH_LDRSB_reg_offset, 0b1111011000000000, 0b0101011000000000);
    OPCd(LDR_STR_reg_offset, 0b1111011000000000, 0b0101000000000000);
    OPCd(LDRB_STRB_reg_offset, 0b1111011000000000, 0b0101010000000000);
    OPCd(LDR_STR_imm_offset, 0b1111000000000000, 0b0110000000000000);
    OPCd(LDRB_STRB_imm_offset, 0b1111000000000000, 0b0111000000000000);
    OPCd(LDRH_STRH_imm_offset, 0b1111000000000000, 0b1000000000000000);
    OPCd(LDR_STR_SP_relative, 0b1111000000000000, 0b1001000000000000);
    OPCd(ADD_SP_or_PC, 0b1111000000000000, 0b1010000000000000);
    OPCd(ADD_SUB_SP, 0b1111111100000000, 0b1011000000000000);
    OPCd(PUSH_POP, 0b1111011000000000, 0b1011010000000000);
    OPCd(BKPT, 0b1111111100000000, 0b1011111000000000);
    OPCd(LDM_STM, 0b1111000000000000, 0b1100000000000000);
    OPCd(SWI, 0b1111111100000000, 0b1101111100000000);
    OPCd(UNDEFINED_BCC, 0b1111111100000000, 0b1101111000000000);
    OPCd(BCC, 0b1111000000000000, 0b1101000000000000);
    OPCd(B, 0b1111100000000000, 0b1110000000000000);
    OPCd(BLX_suffix, 0b1111100000000000, 0b1110100000000000);
    OPCd(BL_BLX_prefix, 0b1111100000000000, 0b1111000000000000);
    OPCd(BL_suffix, 0b1111100000000000, 0b1111100000000000);

    return TIK::INVALID;
}
#undef OPCd

template<typename Op, typename Core, bool cached, typename Ins, u8 V>
static inline void set_thumb_exec_u8_0_15_value(Ins &out) {
    out.exec       = Op::template fn<Core, false, cached, V>;
    out.exec_debug = Op::template fn<Core, true,  cached, V>;
}

template<typename Op, typename Core, bool cached, typename Ins>
static inline void set_thumb_exec_u8_0_15(Ins &out, u8 v) {
    switch (v & 15) {
        case 0:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 0>(out);  break;
        case 1:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 1>(out);  break;
        case 2:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 2>(out);  break;
        case 3:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 3>(out);  break;
        case 4:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 4>(out);  break;
        case 5:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 5>(out);  break;
        case 6:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 6>(out);  break;
        case 7:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 7>(out);  break;
        case 8:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 8>(out);  break;
        case 9:  set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 9>(out);  break;
        case 10: set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 10>(out); break;
        case 11: set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 11>(out); break;
        case 12: set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 12>(out); break;
        case 13: set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 13>(out); break;
        case 14: set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 14>(out); break;
        case 15: set_thumb_exec_u8_0_15_value<Op, Core, cached, Ins, 15>(out); break;
        default: NOGOHERE;
    }
}

#define SET_THUMB_EXEC_U8_0_15(name, value) do { \
    using this_core = core<cpukind, scheduler_kind>; \
    set_thumb_exec_u8_0_15<thumb_op_##name, this_core, cached>(ins, static_cast<u8>(value)); \
} while (0)

template<typename Op, typename Core, bool cached, typename Ins, u8 V>
static inline void set_thumb_exec_u8_0_3_value(Ins &out) {
    out.exec       = Op::template fn<Core, false, cached, V>;
    out.exec_debug = Op::template fn<Core, true,  cached, V>;
}

template<typename Op, typename Core, bool cached, typename Ins>
static inline void set_thumb_exec_u8_0_3(Ins &out, u8 v) {
    switch (v & 3) {
        case 0: set_thumb_exec_u8_0_3_value<Op, Core, cached, Ins, 0>(out); break;
        case 1: set_thumb_exec_u8_0_3_value<Op, Core, cached, Ins, 1>(out); break;
        case 2: set_thumb_exec_u8_0_3_value<Op, Core, cached, Ins, 2>(out); break;
        case 3: set_thumb_exec_u8_0_3_value<Op, Core, cached, Ins, 3>(out); break;
        default: NOGOHERE;
    }
}


#define SET_THUMB_EXEC_U8_0_3(name, value) do { \
using this_core = core<cpukind, scheduler_kind>; \
set_thumb_exec_u8_0_3<thumb_op_##name, this_core, cached>(ins, static_cast<u8>(value)); \
} while (0)

template<armtype cpukind, typename scheduler_kind>
template<bool do_block_end, bool cached>
void core<cpukind, scheduler_kind>::cached_decode_thumb32(u16 opc, arm32_cached_ins<cpukind, scheduler_kind> &ins) {
    auto ik = decode_thumb32(opc);
    if constexpr(do_block_end) ins.ends_block = false;
    ins.kind = ik;
    switch (ik) {
        case TIK::ADD_SUB: {
            ins.Rs = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            bool I = getbit<10>(opc);
            bool sub_opcode = getbit<9>(opc);
            if (!I)
                ins.Rn = getbits<8,6>(opc);
            else
                ins.imm = getbits<8, 6>(opc);
            SET_THUMB_EXEC_BOOLPACK(ADD_SUB, I, sub_opcode);
            return; }
        case TIK::LSL_LSR_ASR: {
            u8 sub_opcode = getbits<12, 11>(opc);
            ins.imm = getbits<10, 6>(opc);
            ins.Rs = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_U8_0_3(LSL_LSR_ASR, sub_opcode);
            return; }
        case TIK::MOV_CMP_ADD_SUB: {
            u8 sub_opcode = getbits<12, 11>(opc);
            ins.Rd = getbits<10, 8>(opc);
            ins.imm = getbits<7, 0>(opc);
            SET_THUMB_EXEC_U8_0_3(MOV_CMP_ADD_SUB, sub_opcode);
            return; }
        case TIK::data_proc: {
            u32 sub_opcode = getbits<9, 6>(opc);
            ins.Rs = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_U8_0_15(data_proc, sub_opcode);
            return; }
        case TIK::BX_BLX: {
            ins.Rs = getbit<6>(opc) << 3;
            ins.Rs |= getbits<5,3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            bool sub_opcode = getbit<7>(opc); // 0=BX, 1=BLX
            SET_THUMB_EXEC_BOOLPACK(BX_BLX, sub_opcode);
            if constexpr(do_block_end) ins.ends_block = true;
            return; }
        case TIK::ADD_CMP_MOV_hi: {
            ins.Rd = (getbit<7>(opc) << 3) | getbits<2, 0>(opc);
            ins.Rs = (getbit<6>(opc) << 3) | getbits<5, 3>(opc);
            u32 sub_opcode = getbits<9, 8>(opc);
            SET_THUMB_EXEC_U8_0_3(ADD_CMP_MOV_hi, sub_opcode);
            return; }
        case TIK::LDR_PC_relative: {
            ins.Rd = getbits<10, 8>(opc);
            ins.imm = getbits<7, 0>(opc) << 2;
            SET_THUMB_EXEC_0BOOL(LDR_PC_relative);
            return; }
        case TIK::LDRH_STRH_reg_offset: {
            bool L = getbit<11>(opc);
            ins.Ro = getbits<8, 6>(opc);
            ins.Rb = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_BOOLPACK(LDRH_STRH_reg_offset, L);
            return; }
        case TIK::LDRSH_LDRSB_reg_offset: {
            bool B = !getbit<11>(opc); // 0=byte, 1=halfword. oops!
            ins.Ro = getbits<8, 6>(opc);
            ins.Rb = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_BOOLPACK(LDRSH_LDRSB_reg_offset, B);
            return; }
        case TIK::LDR_STR_reg_offset: {
            bool L = getbit<11>(opc);
            ins.Ro = getbits<8, 6>(opc);
            ins.Rb = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_BOOLPACK(LDR_STR_reg_offset, L);
            return; }
        case TIK::LDRB_STRB_reg_offset: {
            bool L = getbit<11>(opc); // 0=STR, 1=LDR
            ins.Ro = getbits<8, 6>(opc);
            ins.Rb = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_BOOLPACK(LDRB_STRB_reg_offset, L);
            return; }
        case TIK::LDR_STR_imm_offset: {
            bool L = getbit<11>(opc);
            ins.imm = getbits<10, 6>(opc) << 2;
            ins.Rb = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_BOOLPACK(LDR_STR_imm_offset, L);
            return; }
        case TIK::LDRB_STRB_imm_offset: {
            bool L = getbit<11>(opc);
            ins.imm = getbits<10, 6>(opc);
            ins.Rb = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_BOOLPACK(LDRB_STRB_imm_offset, L);
            return; }
        case TIK::LDRH_STRH_imm_offset: {
            bool L = getbit<11>(opc);
            ins.imm = getbits<10, 6>(opc) << 1;
            ins.Rb = getbits<5, 3>(opc);
            ins.Rd = getbits<2, 0>(opc);
            SET_THUMB_EXEC_BOOLPACK(LDRH_STRH_imm_offset, L);
            return; }
        case TIK::LDR_STR_SP_relative: {
            bool L = getbit<11>(opc);
            ins.Rd = getbits<10, 8>(opc);
            ins.imm = getbits<7, 0>(opc) << 2;
            SET_THUMB_EXEC_BOOLPACK(LDR_STR_SP_relative, L);
            return; }
        case TIK::ADD_SP_or_PC: {
            bool SP = getbit<11>(opc);
            ins.Rd = getbits<10, 8>(opc);
            ins.imm = getbits<7, 0>(opc) << 2;
            SET_THUMB_EXEC_BOOLPACK(ADD_SP_or_PC, SP);
            return; }
        case TIK::ADD_SUB_SP: {
            bool S = getbit<7>(opc);
            ins.imm = getbits<6, 0>(opc) << 2;
            SET_THUMB_EXEC_BOOLPACK(ADD_SUB_SP, S);
            return; }
        case TIK::PUSH_POP: {
            bool pop = getbit<11>(opc);
            bool PC_LR = getbit<8>(opc);
            ins.rlist = getbits<7, 0>(opc);
            bool rlist_empty = ins.rlist == 0;
            SET_THUMB_EXEC_BOOLPACK(PUSH_POP, PC_LR, pop, rlist_empty);
            return; }
        case TIK::BKPT: {
            SET_THUMB_EXEC_0BOOL(BKPT);
            return; }
        case TIK::LDM_STM: {
            bool L = getbit<11>(opc);
            ins.Rb = getbits<10, 8>(opc);
            ins.rlist = getbits<7, 0>(opc);
            bool rlist_empty = ins.rlist == 0;
            if (!L) {
                u32 count = 0; // Rdd
                u32 first = 0; // Rmd
                if constexpr (cpukind <= AT_ARM7TDMI) {
                    for (int i = 7; i >= 0; i--) {
                        if (ins.rlist & (1 << i)) {
                            count++;
                            first = i;
                        }
                    }
                }
                ins.Rdd = count;
                ins.Rmd = first;
            }
            if constexpr (do_block_end && cpukind <= AT_ARM7TDMI) {
                if (L && ins.rlist == 0) ins.ends_block = true;
            }
            SET_THUMB_EXEC_BOOLPACK(LDM_STM, L, rlist_empty);
            return; }
        case TIK::SWI: {
            ins.comment = getbits<7, 0>(opc);
            if constexpr(do_block_end) ins.ends_block = true;
            SET_THUMB_EXEC_0BOOL(SWI);
            return; }
        case TIK::UNDEFINED_BCC: {
            ins.sub_opcode = getbits<11, 8>(opc);
            ins.imm = getbits<7, 0>(opc);
            ins.imm = sign_extend<8>(ins.imm);
            ins.imm <<= 1;
            ins.imm -= 2;
            SET_THUMB_EXEC_0BOOL(UNDEFINED_BCC);
            return; }
        case TIK::BCC: {
            ins.sub_opcode = getbits<11, 8>(opc);
            ins.imm = getbits<7, 0>(opc);
            ins.imm = sign_extend<8>(ins.imm);
            ins.imm <<= 1;
            ins.imm -= 2;
            SET_THUMB_EXEC_0BOOL(BCC);
            return; }
        case TIK::B: {
            ins.imm = getbits<10, 0>(opc);
            ins.imm = sign_extend<11>(ins.imm) << 1;
            if constexpr(do_block_end) ins.ends_block = true;
            SET_THUMB_EXEC_0BOOL(B);
            return; }
        case TIK::BLX_suffix: {
            ins.imm = getbits<10, 0>(opc) << 1;
            if constexpr(do_block_end) ins.ends_block = true;
            SET_THUMB_EXEC_0BOOL(BLX_suffix);
            return; }
        case TIK::BL_BLX_prefix: {
            ins.imm = static_cast<i32>(sign_extend<11>(getbits<10, 0>(opc))) << 12;
            SET_THUMB_EXEC_0BOOL(BL_BLX_prefix);
            return; }
        case TIK::BL_suffix: {
            ins.imm = getbits<10, 0>(opc) << 1;
            if constexpr(do_block_end) ins.ends_block = true;
            SET_THUMB_EXEC_0BOOL(BL_suffix);
            return; }
        case TIK::INVALID:
            break;
    }
    ins.exec = &core::THUMB_ins_INVALID<false, cached>;
    ins.exec_debug = &core::THUMB_ins_INVALID<true, cached>;
    ins.ends_block = true;
}


template<armtype cpukind, typename scheduler_kind>
template<bool do_block_end>
void core<cpukind, scheduler_kind>::cached_decode_arm32(u32 opc, arm32_cached_ins<cpukind, scheduler_kind> &ins) {
    ins.cc = static_cast<condition_codes>(opc >> 28);
    AIK::ins_kind ik;
    ins.ends_block = false;
    // 4-7 and 20-27
    u32 decode = ((opc >> 4) & 15) | ((opc >> 16) & 0xFF0);
    if (getbits<31,28>(opc) == 0xF) {
        ik = decode_arm32_never(decode);
        if (ik != AIK::INVALID) {
            ins.cc = CC_AL; // Always execute these! :-D
        }
    }
    else ik = decode_arm32<cpukind>(decode);
    ins.kind = ik;

#define dd(x) case AIK::x: decode_cached_ARM_##x<do_block_end>(opc, ins); return
    switch (ik) {
        dd(MUL_MLA);
        dd(MULL_MLAL);
        dd(SWP);
        dd(LDRH_STRH);
        dd(LDRSB_LDRSH);
        dd(MRS);
        dd(MSR_reg);
        dd(MSR_imm);
        dd(BX);
        dd(data_proc_immediate_shift);
        dd(data_proc_register_shift);
        dd(undefined_instruction);
        dd(data_proc_immediate);
        dd(LDR_STR_immediate_offset);
        dd(LDR_STR_register_offset);
        case AIK::LDM_STM:
            if constexpr(cpukind <= AT_ARM7TDMI) {
                decode_cached_ARM_LDM_STM_ARM7TDMI<do_block_end>(opc, ins);
            }
            else {
                decode_cached_ARM_LDM_STM_ARM946ES<do_block_end>(opc, ins);
            }
            return;
        //dd(data_proc_undefined);
        dd(STC_LDC);
        dd(CDP);
        dd(MCR_MRC);
        dd(SWI);
        dd(INVALID);
        dd(PLD);
        dd(SMLAxy);
        dd(SMLAWy);
        dd(SMULWy);
        dd(SMLALxy);
        dd(SMULxy);
        dd(LDRD_STRD);
        dd(CLZ);
        dd(BLX_reg);
        dd(QADD_QSUB_QDADD_QDSUB);
        dd(BKPT);
        dd(B_BL);
        dd(BLX);
        default:
            break;
    }
#undef dd
    printf("\nWARN BAD ARM INSTRUCTION FOUND");
    ins.exec = &core::ARM_cached_ins_INVALID<false>;
    ins.exec_debug = &core::ARM_cached_ins_INVALID<true>;
    ins.ends_block = true;
}


TVOID fill_ARM_table() {
    for (u32 opc = 0; opc < 4096; opc++) {
        arm32_ins<cpukind, scheduler_kind> *ins_no = &this->opcode_table_arm[opc];
        arm32_ins<cpukind, scheduler_kind> *ins_yes = &this->opcode_table_arm_debug[opc];
        AIK::ins_kind k = decode_arm32<cpukind>(opc);
        switch(k) {
#define I(x) case AIK::x: ins_no->exec = &core::ARM_ins_##x<false>; ins_yes->exec = &core::ARM_ins_##x<true>; break;
            I(MUL_MLA)
            I(MULL_MLAL)
            I(SWP)
            I(LDRH_STRH)
            I(LDRSB_LDRSH)
            I(MRS)
            I(MSR_reg)
            I(MSR_imm)
            I(BX)
            I(data_proc_immediate_shift)
            I(data_proc_register_shift)
            I(undefined_instruction)
            I(data_proc_immediate)
            I(LDR_STR_immediate_offset)
            I(LDR_STR_register_offset)
            case AIK::LDM_STM:
                if constexpr(cpukind <= AT_ARM7TDMI) {
                    ins_no->exec = &core::ARM_ins_LDM_STM_ARM7TDMI<false>;
                    ins_yes->exec = &core::ARM_ins_LDM_STM_ARM7TDMI<true>;
                }
                else {
                    ins_no->exec = &core::ARM_ins_LDM_STM_ARM946ES<false>;
                    ins_yes->exec = &core::ARM_ins_LDM_STM_ARM946ES<true>;
                }
                break;
            I(STC_LDC)
            I(CDP)
            I(MCR_MRC)
            I(BKPT)
            I(SWI)
            I(INVALID)
            I(PLD)
            I(SMLAxy)
            I(SMLAWy)
            I(SMULWy)
            I(SMLALxy)
            I(SMULxy)
            I(LDRD_STRD)
            I(CLZ)
            I(BLX_reg)
            I(QADD_QSUB_QDADD_QDSUB)
            I(B_BL)
            default: {
                ins_no->exec = &core::ARM_ins_INVALID<false>;
                ins_yes->exec = &core::ARM_ins_INVALID<true>;
            }
            break;

        }
        // Now do the same but for the NEVER table
        if constexpr(cpukind >= AT_ARM946ES) {
            ins_no = &this->opcode_table_arm_never[opc];
            ins_yes = &this->opcode_table_arm_never_debug[opc];
            k = decode_arm32_never(opc);
#undef I
#define I(x) case AIK::x: ins_no->exec = &core::ARM_ins_##x<false>; ins_yes->exec = &core::ARM_ins_##x<true>; ins_no->valid = ins_yes->valid = true; break;

            switch(k) {
                I(PLD);
                I(undefined_instruction);
                I(BLX);
                I(MCR_MRC);
                case AIK::INVALID:
                ins_no->valid = ins_yes->valid = false;
                break;
                default: break;
            }
#undef I
        }
    }
}


#undef TVOID
#undef TU32
#undef TU32P
#undef OPCd
#undef PC
#undef SET_THUMB_EXEC_U8_0_15
#undef SET_THUMB_EXEC_U8_0_3
#undef THUMB_OP_TAG
#undef SET_THUMB_EXEC_BOOLPACK
#undef SET_THUMB_EXEC_0BOOL
#undef THUMB_OP_TAG_U8
