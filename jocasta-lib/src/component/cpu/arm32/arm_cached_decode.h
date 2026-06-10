#pragma once

#define PC R[15]
#define is_ARM7x (cpukind <= AT_ARM7TDMI)
#define ARMdecode(name) template<armtype cpukind, typename scheduler_kind> template<bool do_block_end> void core<cpukind, scheduler_kind>::decode_cached_ARM_##name(u32 opcode, arm32_cached_ins<cpukind, scheduler_kind> &out)

template<typename Op, typename Core, auto Count, typename Ins>
static inline void set_arm_exec_boolpack(Ins &out, const bool (&flags)[Count]) {
    set_arm_exec_boolpack_impl<Op, Core, Ins, 0, Count>(out, flags);
}

#define SET_ARM_EXEC_BOOLPACK(name, ...) do { \
using this_core = core<cpukind, scheduler_kind>; \
const bool arm_exec_flags[] = { __VA_ARGS__ }; \
set_arm_exec_boolpack<arm_op_##name, this_core>(out, arm_exec_flags); \
} while (0)

#define SET_ARM_EXEC_0BOOL(name) do { \
using this_core = core<cpukind, scheduler_kind>; \
out.exec       = &this_core::template ARM_cached_ins_##name<false>; \
out.exec_debug = &this_core::template ARM_cached_ins_##name<true>; \
} while (0)



ARMdecode(MUL_MLA) {
    out.Rdd = getbits<16, 19>(opcode);
    out.Rnd = getbits<12, 15>(opcode);
    out.Rsd = getbits<8, 11>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool S = getbit<20>(opcode);
    bool accumulate = getbit<21>(opcode);
    bool Rd_is_15 = out.Rdd == 15;
    if constexpr(do_block_end) out.ends_block = Rd_is_15;
    SET_ARM_EXEC_BOOLPACK(MUL_MLA, accumulate, S, Rd_is_15);
}

ARMdecode(MULL_MLAL) {
    out.Rdd = getbits<16, 19>(opcode);
    out.Rnd = getbits<12, 15>(opcode);
    out.Rsd = getbits<8, 11>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool S = getbit<20>(opcode);
    bool accumulate = getbit<21>(opcode);
    bool Rn_or_Rd_is_15 = (out.Rdd == 15) || (out.Rnd == 15);
    bool sign = getbit<22>(opcode);
    if constexpr(do_block_end) out.ends_block = Rn_or_Rd_is_15;

    SET_ARM_EXEC_BOOLPACK(MULL_MLAL, accumulate, S, Rn_or_Rd_is_15, sign);
}

ARMdecode(SWP) {
    out.Rdd = getbits<12, 15>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool B = getbit<22>(opcode);
    bool Rdd_is_15 = out.Rdd == 15;
    if constexpr(do_block_end) {
        out.ends_block = Rdd_is_15;
    }
    SET_ARM_EXEC_BOOLPACK(SWP, B, Rdd_is_15);
}

ARMdecode(LDRH_STRH) {
    out.Rdd = getbits<12, 15>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    bool P = getbit<24>(opcode);
    bool U = getbit<23>(opcode);
    bool I = getbit<22>(opcode);
    bool L = getbit<20>(opcode);
    bool W = true;
    if (P) W = getbit<21>(opcode);
    out.immediate_offset = getbits<0, 3>(opcode);
    if constexpr(do_block_end) {
        if (W && L) {
            // In the case of Rnd == 15 and Rdd == 15, it fails actually.
            // Otherwise, if either Rnd or Rdd is 15, well yeah.
            if ((out.Rdd == 15) && (out.Rnd == 15)) {

            }
            else if ((out.Rdd == 15) || (out.Rnd == 15)) {
                out.ends_block = true;
            }
        }
        else if (L) {
            if (out.Rdd == 15) out.ends_block = true;
        }
    }
    setbits<4,7>(out.immediate_offset, getbits<8, 11>(opcode));
    SET_ARM_EXEC_BOOLPACK(LDRH_STRH, P, U, I, L, W);
}

ARMdecode(LDRSB_LDRSH) {
    out.Rdd = getbits<12, 15>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool P = getbit<24>(opcode);
    bool U = getbit<23>(opcode);
    bool I = getbit<22>(opcode);
    bool H = getbit<5>(opcode);
    bool W = true;
    if (P) W = getbit<21>(opcode);
    out.immediate_offset = getbits<0, 3>(opcode);
    setbits<4,7>(out.immediate_offset, getbits<8, 11>(opcode));
    SET_ARM_EXEC_BOOLPACK(LDRSB_LDRSH, P, U, I, W, H);
}

ARMdecode(MRS) {
    out.Rdd = getbits<12, 15>(opcode);
    bool PSR = getbit<22>(opcode);
    SET_ARM_EXEC_BOOLPACK(MRS, PSR);
}

ARMdecode(MSR_reg) {
    out.Rmd = getbits<0, 3>(opcode);
    out.fsxc = getbits<16, 19>(opcode);
    bool PSR = getbit<22>(opcode);
    bool do_idle = (out.fsxc & 0b111) > 0;
    SET_ARM_EXEC_BOOLPACK(MSR_reg, PSR, do_idle);
}

ARMdecode(MSR_imm) {
    out.Rmd = getbits<0, 3>(opcode);
    out.fsxc = getbits<16, 19>(opcode);
    u32 Is = getbits<8, 11>(opcode) << 1;
    out.immediate_offset = getbits<0, 7>(opcode);
    if (Is) out.immediate_offset = (out.immediate_offset << (32 - Is)) | (out.immediate_offset >> Is);
    bool PSR = getbit<22>(opcode);
    bool do_idle = (out.fsxc & 0b111) > 0;
    SET_ARM_EXEC_BOOLPACK(MSR_imm, PSR, do_idle);
}

ARMdecode(BX) {
    out.Rnd = getbits<0, 3>(opcode);
    if constexpr (do_block_end) out.ends_block = true;
    SET_ARM_EXEC_0BOOL(BX);
}

ARMdecode(data_proc_immediate_shift) {
    out.Rnd = getbits<16, 19>(opcode);
    out.Rdd = getbits<12, 15>(opcode);
    bool S = getbit<20>(opcode);
    out.alu_opcode = getbits<21, 24>(opcode);
    out.immediate_offset = getbits<7, 11>(opcode); // shift amount
    out.Rmd = getbits<0, 3>(opcode);
    out.sub_opcode = getbits<5, 6>(opcode);
    bool Rdd_is_15 = out.Rdd == 15;
    switch (out.sub_opcode) {
        case 0: break;
        case 1:
        case 2:
            if (!out.immediate_offset) out.immediate_offset = 32;
            break;
        case 3:
            if (!out.immediate_offset) out.sub_opcode = 4;
            break;
        default: NOGOHERE;
    }
    if constexpr(do_block_end) {
        out.ends_block = Rdd_is_15 && ((out.alu_opcode < 8) || (out.alu_opcode > 11));
    }

    SET_ARM_EXEC_BOOLPACK(data_proc_immediate_shift, S, Rdd_is_15);
}


ARMdecode(data_proc_register_shift) {
    out.Rmd = getbits<0, 3>(opcode);
    out.sub_opcode = getbits<5, 6>(opcode);
    out.immediate_offset = getbits<8, 11>(opcode);
    out.Rdd = getbits<12, 15>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    bool S = getbit<20>(opcode);
    out.alu_opcode = getbits<21, 24>(opcode);

    bool Rdd_is_15 = out.Rdd == 15;
    if constexpr(do_block_end) {
        out.ends_block = Rdd_is_15 && ((out.alu_opcode < 8) || (out.alu_opcode > 11));
    }

    SET_ARM_EXEC_BOOLPACK(data_proc_register_shift, S, Rdd_is_15);
}

ARMdecode(undefined_instruction) {
    SET_ARM_EXEC_0BOOL(undefined_instruction);
}

ARMdecode(data_proc_immediate) {
    out.Rdd = getbits<12, 15>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    out.alu_opcode = getbits<21, 24>(opcode);
    bool S = getbit<20>(opcode);
    bool Rdd_is_15 = out.Rdd == 15;

    bool carryout = false;

    u32 Rm = getbits<0, 7>(opcode);
    if (u32 imm_ROR_amount = (opcode >> 7) & 30) {
        Rm = (Rm << (32 - imm_ROR_amount)) | (Rm >> imm_ROR_amount);
        out.sub_opcode = (Rm >> 31) & 1;   // cached carry-out
        carryout = true;
    }
    out.immediate_offset = Rm;
    if constexpr(do_block_end) {
        out.ends_block = Rdd_is_15 && ((out.alu_opcode < 8) || (out.alu_opcode > 11));
    }

    SET_ARM_EXEC_BOOLPACK(data_proc_immediate, S, Rdd_is_15, carryout);
}

ARMdecode(LDR_STR_immediate_offset) {
    bool P = getbit<24>(opcode);
    bool U = getbit<23>(opcode);
    bool B = getbit<22>(opcode);
    bool W = true;
    if (P) W = getbit<21>(opcode);
    bool L = getbit<20>(opcode);
    out.Rdd = getbits<12, 15>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    out.immediate_offset = getbits<0, 11>(opcode);
    if constexpr(do_block_end) {
        // if L && Rdd == 15
        // or W && Rnd == 15
        if ((L && out.Rdd == 15) || (W && out.Rnd == 15))
            out.ends_block = true;
    }

    SET_ARM_EXEC_BOOLPACK(LDR_STR_immediate_offset, P, U, B, W, L);
}

ARMdecode(LDR_STR_register_offset) {
    bool P = getbit<24>(opcode);
    bool U = getbit<23>(opcode);
    bool B = getbit<22>(opcode);
    bool W = true;
    if (P) W = getbit<21>(opcode);
    bool L = getbit<20>(opcode);
    out.Rdd = getbits<12, 15>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    out.immediate_offset = getbits<7, 11>(opcode);
    out.sub_opcode = getbits<5, 6>(opcode);
    switch (out.sub_opcode) {
        case 0: break;
        case 1: case 2:
            if (!out.immediate_offset) out.immediate_offset = 32;
            break;
        case 3:
            if (!out.immediate_offset) out.sub_opcode = 4;
            break;
    }
    if constexpr(do_block_end) {
        // if L && Rdd == 15
        // or W && Rnd == 15
        if ((L && out.Rdd == 15) || (W && out.Rnd == 15))
            out.ends_block = true;
    }
    SET_ARM_EXEC_BOOLPACK(LDR_STR_register_offset, P, U, B, W, L);
}

ARMdecode(LDM_STM_ARM7TDMI) {
    bool P = getbit<24>(opcode);
    bool U = getbit<23>(opcode);
    bool S = getbit<22>(opcode);
    bool W = getbit<21>(opcode);
    bool L = getbit<20>(opcode);
    if (!U) P = !P;
    out.Rnd = getbits<16, 19>(opcode);
    out.immediate_offset = getbits<0, 15>(opcode);

    u32 rcount = 0;
    //u32 *Rd = regmap[Rnd);
    int first = -1;
    u32 bit = 0;
    bool move_pc = false;
    // Get first register and register count
    for (u32 i = 0; i < 16; i++) {
        u32 mbit = (out.immediate_offset >> (bit++)) & 1;
        rcount += mbit;
        if (mbit && (first==-1)) first = static_cast<int>(i);
    }
    u32 byte_sz = rcount << 2; // 4 byte per register

    if (out.immediate_offset == 0) {
        /* according to NBA, "If the register list is empty, only r15 will be loaded/stored but
         * the base will be incremented/decremented as if each register was transferred."
         */
        out.immediate_offset = 0x8000;
        first = 15;
        byte_sz = 64;
    }
    move_pc = (out.immediate_offset >> 15) & 1;
    bool do_mode_switch = S && (!L || !move_pc);

    out.sub_opcode = first;
    out.Rmd = byte_sz;
    // Rmd = byte_sz, sub_opcode = first, immediate_offset = rlist
    if constexpr(do_block_end) {
        if ((W && out.Rnd == 15) || (L && move_pc)) out.ends_block = true;
    }
    SET_ARM_EXEC_BOOLPACK(LDM_STM_ARM7TDMI, P, U, S, W, L, move_pc, do_mode_switch);
}

ARMdecode(LDM_STM_ARM946ES) {
    out.Rnd = getbits<16, 19>(opcode);
    out.immediate_offset = getbits<0, 15>(opcode);
    bool P = getbit<24>(opcode);
    bool S = getbit<22>(opcode);
    bool W = getbit<21>(opcode);
    bool L = getbit<20>(opcode);
    bool Rnd_is_last = false;

    bool U = getbit<23>(opcode);
    if (out.immediate_offset == 0) {
        if (U) {
            out.exec = &core::ARM_cached_ins_LDM_STM_ARM946ES_ABORT<false, true>;
            out.exec_debug = &core::ARM_cached_ins_LDM_STM_ARM946ES_ABORT<true, true>;
        }
        else {
            out.exec = &core::ARM_cached_ins_LDM_STM_ARM946ES_ABORT<false, false>;
            out.exec_debug = &core::ARM_cached_ins_LDM_STM_ARM946ES_ABORT<true, false>;
        }
        return;
    }
    // immediate_offset = rlist
    bool move_pc = (out.immediate_offset >> 15) & 1;
    bool do_mode_switch = S && (!L || !move_pc);
    u32 bytes = 0;
    if (out.immediate_offset != 0) {
        // Get size in bytes
        for (u32 i = 0; i <= 15; i++) {
            if ((out.immediate_offset >> i) & 1)
                bytes += sizeof(u32);
        }
        Rnd_is_last = (out.immediate_offset >> out.Rnd) == 1; // Get if Rnd_is_last
    } else {
        bytes = 64;
    }
    out.Rmd = bytes;
    if constexpr(do_block_end) {
        if (L && move_pc) out.ends_block = true;
    }
    // Rmd is bytes, immediate_offset is rlist,
    SET_ARM_EXEC_BOOLPACK(LDM_STM_ARM946ES, P, U, S, W, L, move_pc, do_mode_switch, Rnd_is_last);
}

ARMdecode(STC_LDC) {
    SET_ARM_EXEC_0BOOL(STC_LDC);
}

ARMdecode(CDP) {
    SET_ARM_EXEC_0BOOL(CDP);
}

ARMdecode(MCR_MRC) {
    bool copro_to_arm = !getbit<20>(opcode);
    out.immediate_offset = opcode;
    SET_ARM_EXEC_BOOLPACK(MCR_MRC, copro_to_arm);
}

ARMdecode(BKPT) {
    if ((opcode >> 28) == 14) {
        if constexpr(do_block_end) out.ends_block = true;
        SET_ARM_EXEC_0BOOL(BKPT);
    }
    else {
        SET_ARM_EXEC_0BOOL(undefined_instruction);
    }
}

ARMdecode(SWI) {
    if constexpr(do_block_end) out.ends_block = true;
    SET_ARM_EXEC_0BOOL(SWI);
}

ARMdecode(INVALID) {
    SET_ARM_EXEC_0BOOL(INVALID);
}

ARMdecode(PLD) {
    SET_ARM_EXEC_0BOOL(PLD);
}

ARMdecode(SMLAxy) {
    out.Rdd = getbits<16, 19>(opcode);
    out.Rnd = getbits<12, 15>(opcode);
    out.Rsd = getbits<8, 11>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool x = getbit<5>(opcode);
    bool y = getbit<6>(opcode);
    SET_ARM_EXEC_BOOLPACK(SMLAxy, x, y);
}

ARMdecode(SMLAWy) {
    bool y = getbit<6>(opcode);
    out.Rdd = getbits<16, 19>(opcode);
    out.Rnd = getbits<12, 15>(opcode);
    out.Rsd = getbits<8, 11>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    SET_ARM_EXEC_BOOLPACK(SMLAWy, y);
}

ARMdecode(SMULWy) {
    bool y = getbit<6>(opcode);
    out.Rdd = getbits<16, 19>(opcode);
    out.Rsd = getbits<8, 11>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool Rdd_is_15 = out.Rdd == 15;
    if constexpr(do_block_end) out.ends_block = Rdd_is_15;
    SET_ARM_EXEC_BOOLPACK(SMULWy, y, Rdd_is_15);
}

ARMdecode(SMLALxy) {
    bool x = getbit<5>(opcode);
    bool y = getbit<6>(opcode);
    out.Rdd = getbits<16, 19>(opcode);
    out.Rnd = getbits<12, 15>(opcode);
    out.Rsd = getbits<8, 11>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool Rdd_is_15 = out.Rdd == 15;
    if constexpr(do_block_end) out.ends_block = Rdd_is_15;
    SET_ARM_EXEC_BOOLPACK(SMLALxy, x, y, Rdd_is_15);
}

ARMdecode(SMULxy) {
    bool x = getbit<5>(opcode);
    bool y = getbit<6>(opcode);
    out.Rdd = getbits<16, 19>(opcode);
    out.Rsd = getbits<8, 11>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool Rdd_is_15 = out.Rdd == 15;
    if constexpr(do_block_end) out.ends_block = Rdd_is_15;
    SET_ARM_EXEC_BOOLPACK(SMULxy, x, y, Rdd_is_15);
}

ARMdecode(LDRD_STRD) {
    bool P = getbit<24>(opcode);
    bool U = getbit<23>(opcode);
    bool I = getbit<22>(opcode);
    bool L = !getbit<5>(opcode);
    bool W = true;
    if (P) W = getbit<21>(opcode);

    out.Rdd = getbits<12, 15>(opcode);
    out.Rnd = getbits<16, 19>(opcode);
    u32 imm_off = 0;
    imm_off = ((opcode >> 8) & 15) << 4;
    imm_off |= (opcode & 15);
    out.immediate_offset = imm_off;
    out.Rmd = getbits<0, 3>(opcode);
    if constexpr(do_block_end) {
        if (L) {
            if (W) {
                if (!((out.Rnd == 15) && (out.Rdd == 14))) {
                    if (out.Rnd == 15) {
                        out.ends_block = true;
                    }
                }
            }
            if (out.Rdd >= 14) out.ends_block = true;
        }
        else {
            if (W) {
                out.ends_block = out.Rnd == 15;
            }
        }
    }

    SET_ARM_EXEC_BOOLPACK(LDRD_STRD, P, U, I, L, W);
}

ARMdecode(CLZ) {
    out.Rdd = getbits<12, 15>(opcode);
    out.Rmd = getbits<0, 3>(opcode);
    bool Rdd_is_15 = out.Rdd == 15;
    SET_ARM_EXEC_BOOLPACK(CLZ, Rdd_is_15);
}

ARMdecode(BLX_reg) {
    out.Rsd = getbits<0, 3>(opcode);
    if constexpr(do_block_end) out.ends_block = true;
    SET_ARM_EXEC_0BOOL(BLX_reg);
}

ARMdecode(QADD_QSUB_QDADD_QDSUB) {
    out.src1 = getbits<0, 3>(opcode);
    out.src2 = getbits<16, 19>(opcode);
    out.dst = getbits<12, 15>(opcode);
    bool subtract = getbit<21>(opcode);
    bool double_op2 = getbit<22>(opcode);
    SET_ARM_EXEC_BOOLPACK(QADD_QSUB_QDADD_QDSUB, subtract, double_op2);
}

ARMdecode(B_BL) {
    bool link = getbit<24>(opcode);;
    i32 offset = sign_extend<24>(opcode & 0xFFFFFF);
    offset <<= 2;
    out.immediate_offset = static_cast<u32>(offset);
    if constexpr(do_block_end) out.ends_block = true;
    SET_ARM_EXEC_BOOLPACK(B_BL, link);
}

ARMdecode(BLX) {
    i32 offset = sign_extend<24>(opcode & 0xFFFFFF);
    offset <<= 2;
    out.immediate_offset = static_cast<u32>(offset);
    u32 H = getbit<24>(opcode) << 1;
    out.immediate_offset += H;
    if constexpr(do_block_end) out.ends_block = true;
    SET_ARM_EXEC_0BOOL(BLX);
}


#undef PC
#undef is_ARM7x
#undef ARMdecode
#undef SET_ARM_EXEC_BOOLPACK
#undef SET_ARM_EXEC_0BOOL