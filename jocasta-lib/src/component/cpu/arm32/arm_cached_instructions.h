#pragma once

#define PC R[15]

#define ARMip template <armtype cpukind, typename scheduler_kind>
#define ARMIs(name) bool core<cpukind, scheduler_kind>::ARM_cached_ins_##name(const arm32_cached_ins<cpukind, scheduler_kind> &ins)
#define is_ARM7x (cpukind <= AT_ARM7TDMI)

// Together: Rdd, Rnd, Rsd, Rmd

#define ARM_OP_TAG(name) \
struct arm_op_##name { \
    template<typename Core, bool do_debug, bool... Bs> \
    static constexpr auto fn = &Core::template ARM_cached_ins_##name<do_debug, Bs...>; \
};

template<typename Op, typename Core, typename Ins, auto Index, auto Count, bool... Bs>
static inline void set_arm_exec_boolpack_impl(Ins &out, const bool (&flags)[Count]) {
    if constexpr (Index == Count) {
        out.exec       = Op::template fn<Core, false, Bs...>;
        out.exec_debug = Op::template fn<Core, true,  Bs...>;
    }
    else {
        if (flags[Index]) {
            set_arm_exec_boolpack_impl<Op, Core, Ins, Index + 1, Count, Bs..., true>(out, flags);
        }
        else {
            set_arm_exec_boolpack_impl<Op, Core, Ins, Index + 1, Count, Bs..., false>(out, flags);
        }
    }
}

ARM_OP_TAG(MUL_MLA)
ARM_OP_TAG(MULL_MLAL)
ARM_OP_TAG(SWP)
ARM_OP_TAG(LDRH_STRH)
ARM_OP_TAG(LDRSB_LDRSH)
ARM_OP_TAG(MRS);
ARM_OP_TAG(MSR_reg);
ARM_OP_TAG(MSR_imm);
ARM_OP_TAG(BX);
ARM_OP_TAG(data_proc_immediate_shift);
ARM_OP_TAG(data_proc_register_shift);
ARM_OP_TAG(undefined_instruction);
ARM_OP_TAG(data_proc_immediate);
ARM_OP_TAG(LDR_STR_immediate_offset);
ARM_OP_TAG(LDR_STR_register_offset);
ARM_OP_TAG(LDM_STM_ARM7TDMI);
ARM_OP_TAG(LDM_STM_ARM946ES);
ARM_OP_TAG(STC_LDC);
ARM_OP_TAG(CDP);
ARM_OP_TAG(MCR_MRC);
ARM_OP_TAG(SWI);
ARM_OP_TAG(INVALID);
ARM_OP_TAG(PLD);
ARM_OP_TAG(SMLAxy);
ARM_OP_TAG(SMLAWy);
ARM_OP_TAG(SMULWy);
ARM_OP_TAG(SMLALxy);
ARM_OP_TAG(SMULxy);
ARM_OP_TAG(LDRD_STRD);
ARM_OP_TAG(CLZ);
ARM_OP_TAG(BLX_reg);
ARM_OP_TAG(QADD_QSUB_QDADD_QDSUB);
ARM_OP_TAG(BKPT);
ARM_OP_TAG(B_BL);
ARM_OP_TAG(BLX);

ARMip
template<bool do_debug, bool accumulate, bool S, bool Rd_is_15>
ARMIs(MUL_MLA)
{
    if constexpr (accumulate) idle(1);
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    const u32 Rn = *regmap[ins.Rnd];
    const u32 Rm = *regmap[ins.Rmd];
    const u32 Rs = *regmap[ins.Rsd];
    *regmap[ins.Rdd] = MUL(accumulate ? Rn : 0, Rm, Rs, S);
    if constexpr (Rd_is_15) {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug, bool accumulate, bool S, bool Rn_or_Rd_is_15, bool sign>
ARMIs(MULL_MLAL) {
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    const u64 Rm = *regmap[ins.Rmd];
    const u64 Rs = *regmap[ins.Rsd];

    u32 n = 2 + accumulate;
    u64 result = 0;
    if constexpr (sign) {
        if ((Rs >> 8) && ((Rs >> 8) != 0xFFFFFF)) n++;
        if ((Rs >> 16) && ((Rs >> 16) != 0xFFFF)) n++;
        if ((Rs >> 24) && ((Rs >> 24) != 0xFF)) n++;
        result = static_cast<u64>(static_cast<i64>(static_cast<i32>(Rm)) * static_cast<i64>(static_cast<i32>(Rs)));
    }
    else {
        if (Rs >> 8) n++;
        if (Rs >> 16) n++;
        if (Rs >> 24) n++;
        result = Rm * Rs;
    }
    idle(n);

    u32 *Rd = regmap[ins.Rdd];
    u32 *Rn = regmap[ins.Rnd];
    if constexpr (accumulate) result += (static_cast<u64>(*Rd) << 32) | static_cast<u64>(*Rn);
    *Rn = result & 0xFFFF'FFFF;
    *Rd = result >> 32;
    if constexpr (S) {
        regs.CPSR.N = (result >> 63) & 1;
        regs.CPSR.Z = result == 0;
    }
    if constexpr (Rn_or_Rd_is_15) {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug, bool B, bool Rdd_is_15>
ARMIs(SWP) {
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    // Rd=[Rn], [Rn]=Rm
    const u32 *Rn = regmap[ins.Rnd];
    u32 *Rd = regmap[ins.Rdd];
    const u32 *Rm = regmap[ins.Rmd];
    u32 mask, tmp;;
    if constexpr (B) {
        mask = 0xFF;
        tmp = read<1, do_debug>(*Rn, ARM32P_nonsequential) & mask;
        write<1, do_debug, true>(*Rn, ARM32P_nonsequential | ARM32P_lock, (*Rm) & mask); // Rm = [Rn]
    }
    else {
        mask = 0xFFFF'FFFF;
        tmp = read<4, do_debug>(*Rn, ARM32P_nonsequential) & mask;
        write<4, do_debug, true>(*Rn, ARM32P_nonsequential | ARM32P_lock, (*Rm) & mask); // Rm = [Rn]
    }
    idle(1);
    if constexpr (!B) tmp = align_val(*Rn, tmp);
    *Rd = tmp;
    if constexpr (Rdd_is_15) {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug, bool P, bool U, bool I, bool L, bool W>
ARMIs(LDRH_STRH)
{
    u32 *Rn = regmap[ins.Rnd];
    u32 *Rd = regmap[ins.Rdd];
    u32 Rm;
    if constexpr(I) Rm = ins.immediate_offset;
    else Rm = *regmap[ins.Rmd];
    u32 addr = *Rn;
    if constexpr (P) {
        if constexpr(U) addr += Rm;
        else addr -= Rm;
    }
    // L = 0 is load.
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (L) {
        u32 val = read<2, do_debug>(addr, ARM32P_nonsequential);
        if (addr &  1) { // read of a halfword to a unaligned address produces a weird ROR
            val = ((val >> 8) & 0xFF) | (val << 24);
        }
        if constexpr (!P) {
            if constexpr(U) addr += Rm;
            else addr -= Rm;
        }
        bool flushed = false;
        if constexpr (W) {
            if (!((ins.Rnd == 15) && (ins.Rdd == 15))) {
                if (ins.Rnd == 15) {
                    // writeback fails. technically invalid here
                    *Rn = addr + 4;
                    flushed = true;
                }
                else
                    *Rn = addr;
            }
        }
        if constexpr (is_ARM7x) idle(1);
        flushed |= ins.Rdd == 15;
        *Rd = val;
        if (flushed) {
            reload_pipeline<do_debug, true>();
            return true;
        }
    }
    else {
        u32 val = *Rd;
        write<2, do_debug, true>(addr, ARM32P_nonsequential, val & 0xFFFF);
        if constexpr (!P) {
            if constexpr(U) addr += Rm;
            else addr -= Rm;
        }
        if constexpr (W) {
            if (ins.Rnd == 15) {
                // writeback fails. technically invalid here
                *Rn = addr + 4;
                reload_pipeline<do_debug, true>();
                return true;
            }
            else
                *Rn = addr;
        }
    }
    return false;
}

ARMip
template<bool do_debug, bool P, bool U, bool I, bool W, bool H>
ARMIs(LDRSB_LDRSH)
{
    u32 *Rn = regmap[ins.Rnd];
    u32 *Rd = regmap[ins.Rdd];
    u32 Rm;
    if constexpr(I) Rm = ins.immediate_offset;
    else Rm = *regmap[ins.Rmd];
    u32 addr = *Rn;
    if constexpr (P) {
        if constexpr(U) addr += Rm;
        else addr -= Rm;
    }

    u32 val;
    if constexpr (H) val = read<2, do_debug>(addr, ARM32P_nonsequential);
    else val = read<1, do_debug>(addr, ARM32P_nonsequential);

    if (H && !(addr & 1)) { // read of a halfword to a unaligned address produces a byte-extend
        val = sign_extend<16>(val);
    }
    else {
        if constexpr (H) val = (val >> 8); // what we said above...
        val = sign_extend<8>(val);
    }
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (!P) {
        if constexpr(U) addr += Rm;
        else addr -= Rm;
    }
    bool flush = false;
    if constexpr (W) {
        if (ins.Rnd == 15) {// writeback fails. technically invalid here
            if (ins.Rdd != 15) {
                *Rn = addr + 4;
                flush = true;
            }
        }
        else {
            *Rn = addr;
            flush = ins.Rnd == 15;
        }
    }
    if constexpr (is_ARM7x) idle(1);
    *Rd = val;
    flush |= ins.Rdd == 15;
    if (flush) {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug, bool PSR>
ARMIs(MRS)
{
    u32 *Rd = regmap[ins.Rdd];
    if constexpr (PSR) {
        if (regs.CPSR.mode == M_system) *Rd = regs.CPSR.u;
        else *Rd = *get_SPSR_by_mode();
    }
    else {
        *Rd = regs.CPSR.u;
    }

    pipeline.access = ARM32P_sequential | ARM32P_code;
    regs.PC += 4;
    return false;
}


ARMip
template <bool do_debug, bool PSR, bool do_idle>
ARMIs(MSR_reg) {
    bool f = getbit<3>(ins.fsxc);
    bool s = getbit<2>(ins.fsxc);
    bool x = getbit<1>(ins.fsxc);
    bool c = getbit<0>(ins.fsxc);
    u32 mask = 0;
    if (f) mask |= 0xFF000000;
    if (s) { mask |= 0xFF0000; };
    if (x) { mask |= 0xFF00; };
    if (c) { mask |= 0xFF; };
    u32 imm = *regmap[ins.Rmd];
    if constexpr (!PSR) { // CPSR
        if (regs.CPSR.mode == M_user)
            mask &= 0xFF000000;
        if (mask & 0xFF)
            imm |= 0x10; // force this bit always
        //u32 old_mode = regs.CPSR.mode;
        schedule_IRQ_check<true>();
        regs.CPSR.u = (~mask & regs.CPSR.u) | (imm & mask);
        if (mask & 0x0F) {
            fill_regmap();
        }
        if constexpr (!is_ARM7x) { if constexpr (do_idle) idle(2); }
    }
    else {
        if ((regs.CPSR.mode != M_user) && (regs.CPSR.mode != M_system)) {
            u32 *v = get_SPSR_by_mode();
            *v = (~mask & *v) | (imm & mask);
        }
        if constexpr (!is_ARM7x) idle(2);
    }
    pipeline.access = ARM32P_sequential | ARM32P_code;
    regs.PC += 4;
    return false;
}


ARMip
template<bool do_debug, bool PSR, bool do_idle>
ARMIs(MSR_imm) {
    u32 mask = 0;
    bool f = getbit<3>(ins.fsxc);
    bool s = getbit<2>(ins.fsxc);
    bool x = getbit<1>(ins.fsxc);
    bool c = getbit<0>(ins.fsxc);
    if (f) mask |= 0xFF000000;
    if (s) mask |= 0xFF0000;
    if (x) mask |= 0xFF00;
    if (c) mask |= 0xFF;
    u32 imm = ins.immediate_offset;
    if constexpr (!PSR) { // CPSR
        if (regs.CPSR.mode == M_user)
            mask &= 0xFF000000;
        if (mask & 0xFF)
            imm |= 0x10; // force this bit always
        schedule_IRQ_check<true>();
        regs.CPSR.u = (~mask & regs.CPSR.u) | (imm & mask);
        if (mask & 0x0F) {
            fill_regmap();
        }
        if constexpr (!is_ARM7x) { if constexpr(do_idle) idle(2); }
    }
    else {
        if ((regs.CPSR.mode != M_user) && (regs.CPSR.mode != M_system)) {
            u32 *v = get_SPSR_by_mode();
            *v = (~mask & *v) | (imm & mask);
        }
        if constexpr (!is_ARM7x) idle(2);
    }
    regs.PC += 4;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    return false;
}

ARMip
template<bool do_debug>
ARMIs(BX) {
    u32 addr = *regmap[ins.Rnd];
    regs.CPSR.T = addr & 1;
    addr &= 0xFFFFFFFE;
    regs.PC = addr;
    reload_pipeline<do_debug, true>();
    return true;
}


ARMip
template<bool do_debug, bool S, bool Rdd_is_15>
ARMIs(data_proc_immediate_shift) {
    // R(bit4) = 0 for this
    pipeline.access = ARM32P_code | ARM32P_sequential;

    const u32 Rn = *regmap[ins.Rnd];
    u32 Rm = *regmap[ins.Rmd];
    u32 *Rd = regmap[ins.Rdd];
    temp_carry = regs.CPSR.C;
    regs.PC += 4;
    switch(ins.sub_opcode) {
        case 0: //
            Rm = LSL(Rm, ins.immediate_offset);
            break;
        case 1:
            Rm = LSR(Rm, ins.immediate_offset);
            break;
        case 2:
            Rm = ASR(Rm, ins.immediate_offset);
            break;
        case 3:
            Rm = ROR(Rm, ins.immediate_offset);
            break;
        case 4:
            Rm = RRX(Rm);
            break;
        default:
            NOGOHERE;
    }

    ALU(Rn, Rm, ins.alu_opcode, S, Rd);
    if constexpr (Rdd_is_15) {
        if constexpr(S) {
            if (regs.CPSR.mode != M_system) {
                schedule_IRQ_check<true>();
                regs.CPSR.u = *get_SPSR_by_mode();
            }
            fill_regmap();
        }
        if ((ins.alu_opcode < 8) || (ins.alu_opcode > 11)) {
            reload_pipeline<do_debug, true>();
            return true;
        }
    }
    return false;
}


ARMip
template<bool do_debug, bool S, bool Rdd_is_15>
ARMIs(data_proc_register_shift)
{
    const u32 Is = (*regmap[ins.immediate_offset]) & 0xFF; // shift amount
    idle(1);
    pipeline.access = ARM32P_code | ARM32P_nonsequential; // weird quirk of ARM
    regs.PC += 4;
    const u32 Rn = *regmap[ins.Rnd];
    u32 Rm = *regmap[ins.Rmd];
    u32 *Rd = regmap[ins.Rdd];
    temp_carry = regs.CPSR.C;
    switch(ins.sub_opcode) {
        case 0: //
            Rm = LSL(Rm, Is);
            break;
        case 1:
            Rm = LSR(Rm, Is);
            break;
        case 2:
            Rm = ASR(Rm, Is);
            break;
        case 3:
            Rm = ROR(Rm, Is);
            break;
    }

    ALU(Rn, Rm, ins.alu_opcode, S, Rd);

    if constexpr (Rdd_is_15) {
        if constexpr(S) {
            if (regs.CPSR.mode != M_system) {
                schedule_IRQ_check<true>();
                regs.CPSR.u = *get_SPSR_by_mode();
            }
            fill_regmap();
        }
        if ((ins.alu_opcode < 8) || (ins.alu_opcode > 11)) {
            reload_pipeline<do_debug, true>();
            return true;
        }
    }
    return false;
}


ARMip
template<bool do_debug>
ARMIs(undefined_instruction) {
    printf("\nARM9 UNDEFINED INS!");
    return false;
}

ARMip
template<bool do_debug, bool S, bool Rdd_is_15, bool carryout>
ARMIs(data_proc_immediate) {
    const u32 Rn = *regmap[ins.Rnd];
    regs.PC += 4;
    u32 *Rd = regmap[ins.Rdd];
    pipeline.access = ARM32P_code | ARM32P_sequential;
    if constexpr(carryout) temp_carry = ins.sub_opcode;
    else temp_carry = regs.CPSR.C;

    ALU(Rn, ins.immediate_offset, ins.alu_opcode, S, Rd);
    if constexpr (Rdd_is_15) {
        if constexpr(S) {
            if (regs.CPSR.mode != M_system) {
                schedule_IRQ_check<true>();
                regs.CPSR.u = *get_SPSR_by_mode();
            }
            fill_regmap();
        }
        if ((ins.alu_opcode < 8) || (ins.alu_opcode > 11)) {
            reload_pipeline<do_debug, true>();
            return true;
        }
    }
    return false;
}


ARMip
template<bool do_debug, bool P, bool U, bool B, bool W, bool L>
ARMIs(LDR_STR_immediate_offset) {
    // TODO: T (force unprivileged)
    u32 *Rn = regmap[ins.Rnd];
    u32 *Rd = regmap[ins.Rdd];

    u32 addr = *Rn;
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    //if (Rnd == 15) addr += 4;
    if constexpr (P) {
        if constexpr(U) addr += ins.immediate_offset;
        else addr -= ins.immediate_offset;
    }
    if constexpr (L) {// store to RAM
        // B ? 1, or 4
        u32 v;
        if (B) v = read<1, do_debug>(addr, ARM32P_nonsequential);
        else v = read<4, do_debug>(addr, ARM32P_nonsequential);
        if constexpr (!B) {
            if (addr & 3) v = align_val(addr, v);
        }
        if constexpr (!P) {
            if constexpr(U) addr += ins.immediate_offset;
            else addr -= ins.immediate_offset;
        }
        bool flush = false;
        if constexpr (W) {
            if (ins.Rnd == 15) {
                *Rn = addr + 4;
                flush = true;
            }
            else
                *Rn = addr;
        }
        *Rd = v;

        if constexpr (is_ARM7x) {
            idle(1);
            flush |= ins.Rdd == 15;
            if (flush)  {
                reload_pipeline<do_debug, true>();
                return true;
            }
        }
        else {
            if (ins.Rdd == 15) {
                regs.CPSR.T = regs.PC & 1;
                reload_pipeline<do_debug, true>();
                return true;
            }
        }
    }
    else {
        if constexpr(B) write<1, do_debug, true>(addr, ARM32P_nonsequential, *Rd & 0xFF);
        else write<4, do_debug, true>(addr, ARM32P_nonsequential, *Rd);
        if constexpr (!P) {
            if constexpr (U) addr += ins.immediate_offset;
            else addr -= ins.immediate_offset;
        }
        if constexpr (W) {
            if (ins.Rnd == 15) {
                *Rn = addr + 4;
                reload_pipeline<do_debug, true>();
                return true;
            }
            else
                *Rn = addr;
        }
    }
    return false;
}

ARMip
template<bool do_debug, bool P, bool U, bool B, bool W, bool L>
ARMIs(LDR_STR_register_offset) {
    u32 *Rn = regmap[ins.Rnd];
    u32 Rm = *regmap[ins.Rmd];
    u32 *Rd = regmap[ins.Rdd];
    temp_carry = regs.CPSR.C;
    switch(ins.sub_opcode) {
        case 0: Rm = LSL(Rm, ins.immediate_offset); break;
        case 1: Rm = LSR(Rm, ins.immediate_offset); break;
        case 2: Rm = ASR(Rm, ins.immediate_offset); break;
        case 3: Rm = ROR(Rm, ins.immediate_offset); break;
        case 4: Rm = RRX(Rm); break;
    }

    u32 addr = *Rn;
    if constexpr (P) {
        if constexpr(U) addr += Rm;
        else addr -= Rm;
    }

    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (L) { // LDR from RAM
        u32 v;
        if constexpr (B) v = read<1, do_debug>(addr, ARM32P_nonsequential);
        else v = read<4, do_debug>(addr, ARM32P_nonsequential);
        if constexpr(!B) {
            if (addr & 3) v = align_val(addr, v);
        }
        if constexpr (!P) {
            if constexpr(U) addr += Rm;
            else addr -= Rm;
        }
        bool flush = false;
        if constexpr (W)  {
            if (ins.Rnd == 15) {
                flush = true;
                *Rn = addr+4;
            }
            else
                *Rn = addr;
        }
        if constexpr (is_ARM7x) idle(1);
        flush |= ins.Rdd == 15;
        *Rd = v;
        if (flush) {
            reload_pipeline<do_debug, true>();
            return true;
        }
    }
    else { // STR to RAM
        if constexpr(B) write<1, do_debug, true>(addr, ARM32P_nonsequential, *Rd & 0xFF);
        else write<4, do_debug, true>(addr, ARM32P_nonsequential, *Rd);
        if constexpr (!P) {
            if constexpr(U) addr += Rm;
            else addr -= Rm;
        }
        if constexpr (W) {
            if (ins.Rnd == 15) {
                *Rn = addr + 4;
                reload_pipeline<do_debug, true>();
            }
            else
                *Rn = addr;
        }
    }
    return false;
}

ARMip
template<bool do_debug, bool P, bool U, bool S, bool W, bool L, bool move_pc, bool do_mode_switch>
ARMIs(LDM_STM_ARM7TDMI) {
    //u32 *Rd = regmap[Rnd);
    int first = ins.sub_opcode;
    u32 byte_sz = ins.Rmd;

    u32 cur_addr = *regmap[ins.Rnd];
    u32 base_addr = cur_addr;

    u32 old_mode = regs.CPSR.mode;
    if constexpr (do_mode_switch) {
        regs.CPSR.mode = M_user;
        fill_regmap();
    }

    if constexpr (!U) {
        cur_addr -= byte_sz;
        base_addr -= byte_sz;
    }
    else {
        base_addr += byte_sz;
    }

    pipeline.access = ARM32P_code | ARM32P_nonsequential;
    regs.PC += 4;
    int access_type = ARM32P_nonsequential;
    bool flush = false;
    for (int i = first; i < 16; ++i) {
        if (~ins.immediate_offset & (1 << i)) {
            continue;
        }
        if constexpr (P) cur_addr += 4;

        if constexpr (L) {
            const u32 v = read<4, do_debug>(cur_addr, access_type);
            if constexpr (W) {
                if (i == first) {
                    *regmap[ins.Rnd] = base_addr;
                    flush |= ins.Rnd == 15;
                }
            }
            *regmap[i] = v;
            flush |= i == 15;
        }
        else {
            write<4, do_debug, true>(cur_addr, access_type, *regmap[i]);
            if constexpr (W) {
                if (i == first) {
                    *regmap[ins.Rnd] = base_addr;
                    flush |= ins.Rnd == 15;
                }
            }
        }

        if constexpr (!P) cur_addr += 4;
        access_type = ARM32P_sequential;
    }
    if constexpr (L) {
        idle(1);
        if constexpr (do_mode_switch) {
            // According to MBA,
            /*"     During the following two cycles of a usermode LDM,\n"
                   register accesses will go to both the user bank and original bank.
                   */
            // TODO: th
        }

        if constexpr (move_pc) {
            if constexpr (S) { // If force usermode...
                regs.CPSR.u |= 0x10;
                switch(old_mode) {
                    case M_system:
                    case M_user:
                        break;
                    case M_fiq:
                        schedule_IRQ_check<true>();
                        regs.CPSR.u = regs.SPSR_fiq; break;
                    case M_irq:
                        schedule_IRQ_check<true>();
                        regs.CPSR.u = regs.SPSR_irq; break;
                    case M_supervisor:
                        schedule_IRQ_check<true>();
                        regs.CPSR.u = regs.SPSR_svc; break;
                    case M_abort:
                        schedule_IRQ_check<true>();
                        regs.CPSR.u = regs.SPSR_abt; break;
                    case M_undefined:
                        schedule_IRQ_check<true>();
                        regs.CPSR.u = regs.SPSR_und; break;
                    default:
                        break;
                }
                flush = true;
            }
            fill_regmap();
        }
    }
    if constexpr (do_mode_switch) {
        regs.CPSR.mode = old_mode;
        fill_regmap();
    }
    if (flush) {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug, bool U>
ARMIs(LDM_STM_ARM946ES_ABORT) {
    u32 base_new;
    u32 addr = *regmap[ins.Rnd];
    u32 bytes = ins.Rmd;

    if constexpr (U) {
        base_new = addr + bytes;
    } else {
        addr -= bytes;
        base_new = addr;
    }

    *regmap[ins.Rnd] = base_new;
    regs.PC += 4;
    return false;
}

ARMip
template<bool do_debug, bool P, bool U, bool S, bool W, bool L, bool move_pc, bool do_mode_switch, bool Rnd_is_last>
ARMIs(LDM_STM_ARM946ES) {
    u32 bytes = ins.Rmd;
    u32 base_new;
    u32 addr = *regmap[ins.Rnd];

    if constexpr (U) {
        base_new = addr + bytes;
    } else {
        addr -= bytes;
        base_new = addr;
    }

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_nonsequential;

    const u32 mode = regs.CPSR.mode;

    if constexpr (S && (!L || !move_pc)) {
        regs.CPSR.mode = M_user;
        fill_regmap();
    }

    u32 i = 0;
    u32 remaining = ins.immediate_offset;
    u32 access = ARM32P_nonsequential;
    while (remaining != 0) {
        while (((remaining >> i) &  1) == 0) i++;

        if constexpr (P == U)
            addr += 4;

        if constexpr (L) *regmap[i] = read<4, do_debug>(addr, access);
        else write<4, do_debug, true>(addr, access, *regmap[i]);

        access = ARM32P_sequential;

        if constexpr (P != U)
            addr += 4;

        remaining &= ~(1 << i);
    }

    if constexpr (S) {
        if constexpr (L && move_pc) {
            if (regs.CPSR.mode != M_system) {
                schedule_IRQ_check<true>();
                regs.CPSR.u = *get_SPSR_by_mode();
            }
        } else {
            regs.CPSR.mode = mode;
        }
        fill_regmap();
    }

    if constexpr (W) {
        if constexpr (L) {
            // writeback if base is the only register or *NOT* the "last" register
            if (!Rnd_is_last || ins.immediate_offset == (1 << ins.Rnd)) {
                *regmap[ins.Rnd] = base_new;
            }
        } else {
            *regmap[ins.Rnd] = base_new;
        }
    }

    if constexpr (L && move_pc) {
        if ((regs.PC & 1) && !S) {
            regs.CPSR.T = 1;
            regs.PC &= 0xFFFFFFFE;
        }
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug>
ARMIs(STC_LDC) {
    printf("\nWARNING STC/LDC");
    regs.PC += 4;
    return false;
}

ARMip
template<bool do_debug>
ARMIs(CDP) {
    printf("\nWARNING CDP!");
    regs.PC += 4;
    return false;
}

ARMip
template<bool do_debug, bool copro_to_arm>
ARMIs(MCR_MRC) {
    // TODO: this more???
    if (regs.CPSR.mode == M_user) {
        undefined_exception<do_debug, false>();
        return true;
    }
    u32 opcode = ins.immediate_offset;

    u32 v2 = ((opcode >> 28) & 15) == 15;
    const u32 cp_opc = (opcode >> 21) & 7; // CP Opc - Coprocessor operation code
    const u32 Cnd = (opcode >> 16) & 15; // Cn     - Coprocessor source/dest. Register  (C0-C15)
    const u32 Rdd = (opcode >> 12) & 15; // Rd     - ARM source/destination Register    (R0-R15)
    const u32 Pnd = (opcode >> 8) & 15; // Coprocessor number                 (P0-P15)
    const u32 CP = (opcode >> 5) & 7; // CP     - Coprocessor information            (0-7)
    const u32 Cmd = opcode & 15; //  Coprocessor operand Register       (C0-C15)
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;

    if constexpr (!copro_to_arm) { // ARM->CoPro
        const u32 val = CP_read(CP_ptr, Pnd, cp_opc, Cnd, Cmd, CP);
        if (Rdd == 15) {
            // When using MRC with R15: Bit 31-28 of data are copied to Bit 31-28 of CPSR (ie. N,Z,C,V flags), other data bits are ignored, CPSR Bit 27-0 are not affected, R15 (PC) is not affected.     */
            schedule_IRQ_check<true>();
            regs.CPSR.u = (regs.CPSR.u & 0x0FFFFFFF) | (val & 0xF0000000);
        }
        else {
            *regmap[Rdd] = val;
        }
    }
    else {
        // When using MCR with R15: Coprocessor will receive a data value of PC+12.
        u32 v = *regmap[Rdd];
        if (Rdd == 15) v += 4;
        bool halted = CP_write(CP_ptr, Pnd, cp_opc, Cnd, Cmd, CP, v);
        if (halted) set_current_cached_ins_ends_block();
    }
    idle(1);
    return false;
}

ARMip
template<bool do_debug>
ARMIs(BKPT) {
    regs.R_abt[1] = regs.PC - 4;
    regs.SPSR_abt = regs.CPSR.u;
    regs.CPSR.mode = M_abort;
    fill_regmap();
    regs.CPSR.I = 1;
    regs.PC = regs.EBR | 0x0000000C;
    reload_pipeline<do_debug, true>();
    printf("\nARM9 BKPT!?");
    return true;
}

ARMip
template<bool do_debug>
ARMIs(SWI) {
    regs.R_svc[1] = regs.PC - 4;
    regs.SPSR_svc = regs.CPSR.u;
    regs.CPSR.mode = M_supervisor;
    fill_regmap();
    regs.CPSR.I = 1;
    regs.PC = regs.EBR | 0x00000008;
    reload_pipeline<do_debug, true>();
    return true;
}

ARMip
template<bool do_debug>
ARMIs(INVALID) {
    printf("\nWARNING INVALID!");
    NOGOHERE;
    return false;
}

ARMip
template<bool do_debug>
ARMIs(PLD) {
    printf("\nPLD!");
    regs.PC += 4;
    return false;
}


ARMip
template<bool do_debug, bool x, bool y>
ARMIs(SMLAxy) {
    i16 value1, value2;

    if constexpr (x) value1 = static_cast<i16>(*regmap[ins.Rmd] >> 16);
    else value1 = static_cast<i16>(*regmap[ins.Rmd] & 0xFFFF);

    if constexpr (y) value2 = static_cast<i16>(*regmap[ins.Rsd] >> 16);
    else value2 = static_cast<i16>(*regmap[ins.Rsd] & 0xFFFF);

    const u32 first_result = static_cast<u32>(static_cast<i32>(value1) * static_cast<i32>(value2));
    const u32 mop2 = *regmap[ins.Rnd];
    const u32 final_result = first_result + mop2;

    if((~(first_result ^ mop2) & (mop2 ^ final_result)) >> 31)
        regs.CPSR.Q = 1;

    *regmap[ins.Rdd] = final_result;

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;
    return false;
}

ARMip
template<bool do_debug, bool y>
ARMIs(SMLAWy) {
    i32 value1 = static_cast<i32>(*regmap[ins.Rmd]);
    i16 value2;

    if constexpr (y) value2 = static_cast<i16>(*regmap[ins.Rsd] >> 16);
    else value2 = static_cast<i16>(*regmap[ins.Rsd] & 0xFFFF);

    const u32 first_result = static_cast<u32>((static_cast<i32>(value1) * static_cast<i32>(value2)) >> 16);
    const u32 mop2 = *regmap[ins.Rnd];
    const u32 final_result = first_result + mop2;

    if((~(first_result ^ mop2) & (mop2 ^ final_result)) >> 31) {
        regs.CPSR.Q = 1;
    }

    *regmap[ins.Rdd] = final_result;

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;
    return false;
}

ARMip
template<bool do_debug, bool y, bool Rdd_is_15>
ARMIs(SMULWy) {
    // 1001b: SMULWy{cond}   Rd,Rm,Rs        ;Rd=(Rm*HalfRs)/10000h

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;

    u32 *Rd = regmap[ins.Rdd];
    u32 Rs = *regmap[ins.Rsd];
    const u32 Rm = *regmap[ins.Rmd];

    if constexpr (y) Rs >>= 16;
    else Rs &= 0xFFFF;

    const u64 result = (static_cast<i64>(static_cast<i32>(Rm))) * static_cast<i64>(static_cast<i16>(Rs)) >> 16;

    *Rd = result;

    if constexpr(Rdd_is_15) {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug, bool x, bool y, bool Rdd_is_15>
ARMIs(SMLALxy) {
    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_nonsequential; // this one becomes nonsequential

    u32 *Rd = regmap[ins.Rdd];
    u32 Rs = *regmap[ins.Rsd];
    u32 Rm = *regmap[ins.Rmd];
    u32 *Rn = regmap[ins.Rnd];

    if constexpr (y) Rs >>= 16;
    else Rs &= 0xFFFF;
    if constexpr (x) Rm >>= 16;
    else Rm &= 0xFFFF;

    i64 result = static_cast<i64>(static_cast<i16>(Rm)) * static_cast<i64>(static_cast<i16>(Rs));
    result += static_cast<i64>(static_cast<u64>(*Rn) | (static_cast<u64>(*Rd) << 32));

    *Rn = static_cast<u64>(result) & 0xFFFFFFFF;
    *Rd = static_cast<u64>(result) >> 32;
    if constexpr(Rdd_is_15)  {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug, bool x, bool y, bool Rdd_is_15>
ARMIs(SMULxy) {
    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_nonsequential; // this one becomes nonsequential

    u32 *Rd = regmap[ins.Rdd];
    u32 Rs = *regmap[ins.Rsd];
    u32 Rm = *regmap[ins.Rmd];

    if constexpr (y) Rs >>= 16;
    else Rs &= 0xFFFF;
    if constexpr (x) Rm >>= 16;
    else Rm &= 0xFFFF;

    *Rd = (static_cast<i32>(static_cast<i16>(Rm)) * static_cast<i32>(static_cast<i16>(Rs)));
    if constexpr (Rdd_is_15) {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug, bool P, bool U, bool I, bool L, bool W>
ARMIs(LDRD_STRD) {
    u32 imm_off = ins.immediate_offset;
    u32 *Rn = regmap[ins.Rnd];
    u32 *Rd = regmap[ins.Rdd];
    u32 *Rdp1 = regmap[ins.Rdd | 1];
    u32 Rm;
    if constexpr(I) Rm = imm_off;
    else Rm = *regmap[ins.Rmd];

    u32 addr = *Rn;
    if constexpr (P) {
        if constexpr (U) addr += Rm;
        else addr -= Rm;
    }
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (L) {
        const u32 val = read<4, do_debug>(addr, ARM32P_nonsequential);
        const u32 val_hi = read<4, do_debug>(addr+4, ARM32P_sequential);

        if constexpr (!P) {
            if constexpr(U) addr += Rm;
            else addr -= Rm;
        }
        bool flush = false;
        if constexpr (W) {
            if (!((ins.Rnd == 15) && (ins.Rdd == 14))) {
                if (ins.Rnd == 15) {
                    // writeback fails. technically invalid here
                    flush = true;
                    *Rn = addr + 4;
                }
                else
                    *Rn = addr;
            }
        }
        *Rd = val;
        *Rdp1 = val_hi;
        flush |= ins.Rdd >= 14;
        if (flush) {
            reload_pipeline<do_debug, true>();
            return true;
        }
    }
    else {
        write<4, do_debug, true>(addr, ARM32P_nonsequential, *Rd);
        write<4, do_debug, true>(addr+4, ARM32P_sequential, *Rdp1);
        if constexpr (!P) {
            if constexpr(U) addr += Rm;
            else addr -= Rm;
        }
        if constexpr (W) {
            if (ins.Rnd == 15) {
                // writeback fails. technically invalid here
                *Rn = addr + 4;
                reload_pipeline<do_debug, true>();
                return true;
            }
            else
                *Rn = addr;
        }
    }
    return false;
}

ARMip
template<bool do_debug, bool Rdd_is_15>
ARMIs(CLZ) {
    const u32 v = *regmap[ins.Rmd];

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;

    *regmap[ins.Rdd] = (v == 0) ? 32 : __builtin_clz(v);
    if constexpr (Rdd_is_15) {
        reload_pipeline<do_debug, true>();
        return true;
    }
    return false;
}

ARMip
template<bool do_debug>
ARMIs(BLX_reg) {
    const u32 link = regs.PC - 4;
    regs.PC = (*regmap[ins.Rsd]);
    *regmap[14] = link;
    regs.CPSR.T = regs.PC & 1;
    if (regs.CPSR.T) regs.PC &= 0xFFFFFFFE;
    else regs.PC &= 0xFFFFFFFC;

    reload_pipeline<do_debug, true>();
    return true;
}

ARMip
template<bool do_debug, bool subtract, bool double_op2>
ARMIs(QADD_QSUB_QDADD_QDSUB) {
    u32 op2  = *regmap[ins.src2];

    if constexpr(double_op2) {
        u32 result = op2 + op2;

        if((op2 ^ result) >> 31) {
            regs.CPSR.Q = 1;
            result = 0x80000000 - (result >> 31);
        }

        op2 = result;
    }

    if constexpr (subtract) {
        const u32 op1 = *regmap[ins.src1];
        u32 result = op1 - op2;

        if(((op1 ^ op2) & (op1 ^ result)) >> 31) {
            regs.CPSR.Q = 1;
            result = 0x80000000 - (result >> 31);
        }

        *regmap[ins.dst] = result;
    } else {
        const u32 op1 = *regmap[ins.src1];
        u32 result = op1 + op2;

        if((~(op1 ^ op2) & (op2 ^ result)) >> 31) {
            regs.CPSR.Q = 1;
            result = 0x80000000 - (result >> 31);
        }
        *regmap[ins.dst] = result;
    }

    regs.PC += 4;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    return false;
}

ARMip
template<bool do_debug, bool link>
ARMIs(B_BL) {
    if constexpr (link) {
        *regmap[14] = regs.PC - 4;
    }
    regs.PC += ins.immediate_offset;
    reload_pipeline<do_debug, true>();
    return true;
}

ARMip
template<bool do_debug>
ARMIs(BLX) {
    *regmap[14] = regs.PC - 4;
    regs.PC += static_cast<u32>(ins.immediate_offset);
    regs.CPSR.T = 1;

    reload_pipeline<do_debug, true>();
    return true;
}

#undef PC
#undef ARMip
#undef ARMIs
#undef ARM_OP_TAG
#undef ARMdecode
#undef is_ARM7x
