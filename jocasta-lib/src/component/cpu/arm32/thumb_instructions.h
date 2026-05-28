#pragma once

#define PC R[15]
#define is_ARM7x (cpukind <= AT_ARM7TDMI)

#define THUMBip template <armtype cpukind, typename scheduler_kind>
#define THUMBIs(name)  bool core<cpukind, scheduler_kind>::THUMB_ins_##name(const arm32_cached_ins<cpukind, scheduler_kind> &ins)

THUMBip
template<bool do_debug, bool cached>
THUMBIs(INVALID)
{
    printf("\nUNIMPLEMENTED INSTRUCTION!");
    NOGOHERE;
}

template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::tLSL(u32 v, u32 amount, bool set_flags) {
    temp_carry = regs.CPSR.C;
    if (amount == 0) return v;
    temp_carry = amount > 32 ? 0 : !!(v & 1 << (32 - amount));
    v = (amount > 31) ? 0 : (v << amount);
    return v;
}

// Logical shift right
template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::tLSR(u32 v, u32 amount, bool set_flags)
{
    if (set_flags) temp_carry = regs.CPSR.C;
    if (amount == 0) return v;
    if (set_flags) temp_carry = (amount > 32) ? 0 : !!(v & 1 << (amount - 1));
    v = (amount > 31) ? 0 : (v >> amount);
    return v;
}

// Arithemtic (sign-extend) shift right
template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::tASR(u32 v, u32 amount, bool set_flags)
{
    //   carry = cpsr().c;
    temp_carry = regs.CPSR.C;

    //   if(shift == 0) return source;
    if (amount == 0) return v;

    //   carry = shift > 32 ? source & 1 << 31 : source & 1 << shift - 1;
    temp_carry = (amount >= 32) ? (!!(v & 0x80000000)) : !!(v & (1 << (amount - 1)));

    //   source = shift > 31 ? (i32)source >> 31 : (i32)source >> shift;
    v = (amount > 31) ? static_cast<i32>(v) >> 31 : static_cast<i32>(v) >> amount;

    //    return source;
    return v;
}

template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::tADD(const u32 op1, const u32 op2)
{
    const u32 result = op1 + op2;
    regs.CPSR.N = (result >> 31) & 1;
    regs.CPSR.Z = result == 0;
    regs.CPSR.C = result < op1;
    temp_carry = regs.CPSR.C;
    regs.CPSR.V = (~(op1 ^ op2) & (op2 ^ result)) >> 31;
    return result;
}

template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::tSUB(const u32 op1, const u32 op2, bool set_flags)
{
    const u32 result = op1 - op2;
    regs.CPSR.N = (result >> 31) & 1;
    regs.CPSR.Z = result == 0;
    temp_carry = op1 >= op2;
    regs.CPSR.C = temp_carry;
    regs.CPSR.V = ((op1 ^ op2) & (op1 ^ result)) >> 31;
    return result;
}

THUMBip
template<bool do_debug, bool cached, bool I, bool sub_opcode>
THUMBIs(ADD_SUB)
{
    u64 val;
    if constexpr (I) {
        val = ins.imm;
    }
    else {
        val = *regmap[ins.Rn];
    }
    regs.PC += 2;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    u32 *Rd = regmap[ins.Rd];
    u32 op1 = *regmap[ins.Rs];
    if constexpr (sub_opcode) *Rd = tSUB(op1, val, true);
    else *Rd = tADD(op1, val);
    return false;
}

THUMBip
template<bool do_debug,  bool cached, u8 sub_opcode>
THUMBIs(LSL_LSR_ASR)
{
    //UPDATE!
    u32 *Rd = regmap[ins.Rd];
    const u32 Rs = *regmap[ins.Rs];
    regs.PC += 2;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    temp_carry = regs.CPSR.C;
    if constexpr(sub_opcode == 0) *Rd = tLSL(Rs, ins.imm, true);
    if constexpr(sub_opcode == 1) *Rd = tLSR(Rs, ins.imm ? ins.imm : 32, true);
    if constexpr(sub_opcode == 2) *Rd = tASR(Rs, ins.imm ? ins.imm : 32, true);
    regs.CPSR.C = temp_carry;
    regs.CPSR.Z = *Rd == 0;
    regs.CPSR.N = ((*Rd) >> 31) & 1;
    return false;
}

THUMBip
template<bool do_debug, bool cached, u8 sub_opcode>
THUMBIs(MOV_CMP_ADD_SUB)
{
    u32 *Rd = regmap[ins.Rd];
    if constexpr(sub_opcode == 0) { // MOV
        *Rd = ins.imm;
        regs.CPSR.N = (*Rd >> 31) & 1;
        regs.CPSR.Z = (*Rd) == 0;
    }
    if constexpr(sub_opcode == 1)  // CMP
        tSUB(*Rd, ins.imm, true);
    if constexpr(sub_opcode == 2) // ADD
        *Rd = tADD(*Rd, ins.imm);
    if constexpr(sub_opcode == 3) // SUB
            *Rd = tSUB(*Rd, ins.imm, true);

    pipeline.access = ARM32P_sequential | ARM32P_code;
    regs.PC += 2;
    return false;
}

template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::tROR(u32 v, u32 amount)
{
    temp_carry = regs.CPSR.C;
    if (amount == 0) return v;
    amount &= 31;
    if (amount) v = (v << (32 - amount)) | (v >> amount); // ?correct
    temp_carry = !!(v & 1 << 31);
    return v;
}

// Thanks Ares for this trick
static u32 thumb_mul_ticks(u32 multiplier, u32 is_signed)
{
    u32 n = 1;
    if(multiplier >>  8 && multiplier >>  8 != 0xffffff) n++;
    if(multiplier >> 16 && multiplier >> 16 !=   0xffff) n++;
    if(multiplier >> 24 && multiplier >> 24 !=     0xff) n++;
    return n;
}

THUMBip
template<bool do_debug, bool cached, u8 sub_opcode>
THUMBIs(data_proc) {
#define setnz(x) regs.CPSR.N = ((x) >> 31) & 1; \
              regs.CPSR.Z = (x) == 0;
    u32 Rs = *regmap[ins.Rs];
    u32 *Rd = regmap[ins.Rd];
    regs.PC += 2;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    temp_carry = regs.CPSR.C;
    if constexpr(sub_opcode == 0) {
        // AND (N,Z)
        *Rd &= Rs;
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 1) {
        // XOR (N,Z)
        *Rd ^= Rs;
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 2) {
        // LSL (N,Z,C)
        idle(1);
        pipeline.access = ARM32P_nonsequential | ARM32P_code;
        *Rd = tLSL(*Rd, Rs & 0xFF, true);
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 3) {
        // LSR
        idle(1);
        pipeline.access = ARM32P_nonsequential | ARM32P_code;
        *Rd = tLSR(*Rd, Rs & 0xFF, true);
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 4) {
        // ASR
        idle(1);
        *Rd = tASR(*Rd, Rs & 0xFF, true);
        pipeline.access = ARM32P_nonsequential | ARM32P_code;
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 5) {
        // ADC
        *Rd = tADD(*Rd, Rs + temp_carry);
    }
    if constexpr(sub_opcode == 6) {
        // SBC
        *Rd = tSUB((*Rd) - (temp_carry ^ 1), Rs, true);
    }
    if constexpr(sub_opcode == 7) {
        // tROR
        *Rd = tROR(*Rd, Rs & 0xFF);
        idle(1);
        pipeline.access = ARM32P_nonsequential | ARM32P_code;
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 8) {
        // TST (N, Z)
        const u32 v = (*Rd) & Rs;
        setnz(v);
    }
    if constexpr(sub_opcode == 9) {
        // NEG
        *Rd = tSUB(0, Rs, true);
    }
    if constexpr(sub_opcode == 10) {
        // CMP Rd, Rs
        tSUB(*Rd, Rs, true);
    }
    if constexpr(sub_opcode == 11) {
        // CMN
        tADD(*Rd, Rs);
    }
    if constexpr(sub_opcode == 12) {
        // ORR
        *Rd |= Rs;
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 13) {
        // MUL
        idle(thumb_mul_ticks(*Rd, 0));
        *Rd *= Rs;
        pipeline.access = ARM32P_nonsequential | ARM32P_code;
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 14) {
        // BIC
        *Rd &= (Rs ^ 0xFFFFFFFF);
        setnz(*Rd);
    }
    if constexpr(sub_opcode == 15) {
        // MVN
        *Rd = Rs ^ 0xFFFFFFFF;
        setnz(*Rd);
    }
    regs.CPSR.C = temp_carry;
    return false;
}


THUMBip
template<bool do_debug, bool cached, bool sub_opcode>
THUMBIs(BX_BLX)
{
    // Update the BLX!
    // for BX, MSBd must be zero
    // for BLX, MSBd must be one
    // MSBd = bit 7
    const u32 addr = *regmap[ins.Rs];
    regs.CPSR.T = addr & 1;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (!is_ARM7x) {
        if constexpr (sub_opcode) { // BLX
            const u32 lnk = regs.PC - 1;
            *regmap[14] = lnk;
        }
    }
    regs.PC = addr & 0xFFFFFFFE;
    reload_pipeline<do_debug, cached>();
    return true;
}

THUMBip
template<bool do_debug, bool cached, u8 sub_opcode>
THUMBIs(ADD_CMP_MOV_hi)
{
    //assert(ins.sub_opcode != 3);
    const u32 op1 = *regmap[ins.Rs];
    const u32 op2 = *regmap[ins.Rd];
    regs.PC += 2;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    bool flush = false;

    if constexpr (sub_opcode == 0) {
        // ADD
        *regmap[ins.Rd] = op1 + op2;
        if (ins.Rd == 15) {
            regs.PC &= 0xFFFFFFFE;
            reload_pipeline<do_debug, cached>();
            flush = true;
        }
    }
    if constexpr (sub_opcode == 1) {
        // CMP
        tSUB(op2, op1, true);
    }
    if constexpr (sub_opcode == 2) {
        // MOV
        *regmap[ins.Rd] = op1;
        if (ins.Rd == 15) {
            regs.PC &= 0xFFFFFFFE;
            reload_pipeline<do_debug, cached>();
            flush = true;
        }
    }
    return flush;
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(LDR_PC_relative)
{
    const u32 addr = (regs.PC & (~3)) + ins.imm;
    regs.PC += 2;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    u32 *Rd = regmap[ins.Rd];
    *Rd = read<4, do_debug>(addr, ARM32P_nonsequential);
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool L>
THUMBIs(LDRH_STRH_reg_offset)
{
    u32 addr = *regmap[ins.Rb] + *regmap[ins.Ro];
    u32 *Rd = regmap[ins.Rd];
    regs.PC += 2;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (L) { // load
        u32 v = read<2, do_debug>(addr, ARM32P_nonsequential);
        if (addr & 1) v = (v >> 8) | (v << 24);
        *Rd = v;
    }
    else { // store
        write<2, do_debug, cached>(addr, ARM32P_nonsequential, (*Rd) & 0xFFFF);
    }
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool B>
THUMBIs(LDRSH_LDRSB_reg_offset)
{
    const u32 addr = *regmap[ins.Rb] + *regmap[ins.Ro];
    regs.PC += 2;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    u32 *Rd = regmap[ins.Rd];
    const u8 sz = ins.B ? 1 : 2;

    // ins.B ? 1 : 2
    u32 v;
    if constexpr (B) {
        v = read<1, do_debug>(addr, ARM32P_nonsequential) & 0xFF;
        v = sign_extend<8>(v);
    }
    else {
        v = read<2, do_debug>(addr, ARM32P_nonsequential) & 0xFFFF;
        if constexpr (is_ARM7x) {
            if (addr & 1) { v = (v >> 8); v = sign_extend<8>(v); }
            else v = sign_extend<16>(v);
        }
        else v = sign_extend<16>(v);
    }
    *Rd = v;
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool L>
THUMBIs(LDR_STR_reg_offset)
{
    const u32 addr = *regmap[ins.Rb] + *regmap[ins.Ro];
    regs.PC += 2;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    u32 *Rd = regmap[ins.Rd];
    if constexpr (L) { // Load
        u32 v = read<4, do_debug>(addr, ARM32P_nonsequential);
        if (addr & 3) v = align_val(addr, v);
        *Rd = v;
    }
    else { // Store
        write<4, do_debug, cached>(addr, ARM32P_nonsequential, *Rd);
    }
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool L>
THUMBIs(LDRB_STRB_reg_offset)
{
    const u32 addr = *regmap[ins.Rb] + *regmap[ins.Ro];
    regs.PC += 2;
    u32 *Rd = regmap[ins.Rd];
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (L) { // Load
        *Rd = read<1, do_debug>(addr, ARM32P_nonsequential);
    }
    else { // Store
        write<1, do_debug, cached>(addr, ARM32P_nonsequential, (*Rd) & 0xFF);
    }
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool L>
THUMBIs(LDR_STR_imm_offset)
{
    const u32 addr = *regmap[ins.Rb] + ins.imm;
    regs.PC += 2;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    u32 *Rd = regmap[ins.Rd];
    if constexpr (L) { // Load
        u32 v = read<4, do_debug>(addr, ARM32P_nonsequential);
        if (addr & 3) v = align_val(addr, v);
        *Rd = v;
    }
    else { // Store
        write<4, do_debug, cached>(addr, ARM32P_nonsequential, *Rd);
    }
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool L>
THUMBIs(LDRB_STRB_imm_offset)
{
    const u32 addr = *regmap[ins.Rb] + ins.imm;
    u32 *Rd = regmap[ins.Rd];
    regs.PC += 2;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (L) { // load
        const u32 v = read<1, do_debug>(addr, ARM32P_nonsequential);
        *Rd = v;
    }
    else { // store
        write<1, do_debug, cached>(addr, ARM32P_nonsequential, (*Rd) & 0xFF);
    }
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool L>
THUMBIs(LDRH_STRH_imm_offset)
{
    const u32 addr = *regmap[ins.Rb] + ins.imm;
    u32 *Rd = regmap[ins.Rd];
    regs.PC += 2;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (L) { // load
        u32 v = read<2, do_debug>(addr, ARM32P_nonsequential);
        if (addr & 1) v = (v >> 8) | (v << 24);
        *Rd = v;
    }
    else { // store
        write<2, do_debug, cached>(addr, ARM32P_nonsequential, (*Rd) & 0xFFFF);
    }
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool L>
THUMBIs(LDR_STR_SP_relative)
{
    const u32 addr = *regmap[13] + ins.imm;
    regs.PC += 2;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if constexpr (L) { // if Load
        u32 v = read<4, do_debug>(addr, ARM32P_nonsequential);
        if (addr & 3) v = align_val(addr, v);
        *regmap[ins.Rd] = v;
    }
    else { // Store
        write<4, do_debug, cached>(addr, ARM32P_nonsequential, *regmap[ins.Rd]);
    }
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool SP>
THUMBIs(ADD_SP_or_PC)
{
    u32 *Rd = regmap[ins.Rd];
    if constexpr (SP) *Rd = *regmap[13] + ins.imm;
    else *Rd = (regs.PC & 0xFFFFFFFD) + ins.imm;
    regs.PC += 2;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool sub_opcode>
THUMBIs(ADD_SUB_SP)
{
    u32 *sp = regmap[13];
    if constexpr (sub_opcode) *sp -= ins.imm;
    else *sp += ins.imm;
    regs.PC += 2;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool PC_LR, bool pop, bool rlist_empty>
THUMBIs(PUSH_POP)
{
    u32 access = ARM32P_nonsequential;
    const u32 rlist = ins.rlist;
    u32 address = *regmap[13];
    bool flush = false;
    if constexpr (is_ARM7x) {
        regs.PC += 2;
        pipeline.access = ARM32P_nonsequential | ARM32P_code;
        if constexpr ((!PC_LR) && rlist_empty) {
            u32 *r13 = regmap[13];
            if constexpr (pop) {
                regs.PC = read<4, do_debug>(*r13, ARM32P_nonsequential);
                *r13 += 64;
                reload_pipeline<do_debug, cached>();
                return true;
            }
            else {
                *r13 -= 64;
                write<4, do_debug, cached>(*r13, ARM32P_nonsequential, regs.PC);
                return false;
            }
        }
    }

    if constexpr (pop) {
        for (u32 r=0; r<8; r++) {
            if ((rlist  >> r) & 1) {
                *regmap[r] = read<4, do_debug>(address, access);
                address += 4;
                access = ARM32P_sequential;
            }
        }

        if constexpr (PC_LR) {
            regs.PC = read<4, do_debug>(address, access);
            *regmap[13] = address + 4;
            if constexpr (is_ARM7x) {
                idle(1);
            }
            else {
                regs.CPSR.T = regs.PC & 1;
            }
            regs.PC &= 0xFFFFFFFE;
            reload_pipeline<do_debug, cached>();
            return true;
        }
        if constexpr (is_ARM7x) {
            idle(1);
        }
        *regmap[13] = address;
    } else {
        for (u32 r = 0; r < 8; r++) {
            if ((rlist >> r) & 1)
                address -= 4;
        }

        if constexpr(PC_LR) {
            address -= 4;
        }

        *regmap[13] = address;

        for (u32 r = 0; r < 8; r++) {
            if ((rlist >> r) & 1) {
                write<4, do_debug, cached>(address, access, *regmap[r]);
                address += 4;
                access = ARM32P_sequential;
            }
        }

        if constexpr(PC_LR) {
            write<4, do_debug, cached>(address, access, *regmap[14]);
        }
    }

    if constexpr (!is_ARM7x) {
        regs.PC += 2;
    }
    pipeline.access = ARM32P_code | ARM32P_nonsequential;
    return false;
}

THUMBip
template<bool do_debug, bool cached, bool L, bool rlist_empty>
THUMBIs(LDM_STM) {
    const u32 rlist = ins.rlist;
    u32 address = *regmap[ins.Rb];
    u32 access = ARM32P_nonsequential;
    if constexpr (is_ARM7x) {
        pipeline.access = ARM32P_nonsequential | ARM32P_code;
        regs.PC += 2;
        if constexpr (rlist_empty)  {
            u32 *r13 = regmap[ins.Rb];
            if constexpr (L) {
                regs.PC = read<4, do_debug>(*r13, ARM32P_nonsequential);
                *r13 += 0x40;
                reload_pipeline<do_debug, cached>();
                return true;
            }
            else {
                write<4, do_debug, cached>(*r13, ARM32P_nonsequential, regs.PC);
                *r13 += 0x40;
                return false;
            }
        }
    }
    if constexpr (L) {
        for (u32 i = 0; i <= 7; i++) {
            if ((rlist >> i) & 1) {
                u32 v = read<4, do_debug>(address, access);
                *regmap[i] = v;
                address += 4;
                access = ARM32P_sequential;
            }
        }
        if constexpr (is_ARM7x) idle(1);
        if (~rlist & (1 << ins.Rb)) {
            // Do not trigger pipeline flush on r15
            *regmap[ins.Rb] = address;
        }
    } else {
        u32 first = ins.Rmd;
        u32 count = ins.Rdd;

        u32 addr_new = address + (count << 2);
        for (u32 reg = first; reg < 8; reg++) {
            if ((rlist >> reg) & 1) {
                write<4, do_debug, cached>(address, access, *regmap[reg]);
                if constexpr (is_ARM7x) {
                    if (reg == first) *regmap[ins.Rb] = addr_new;
                }
                address += 4;
                access = ARM32P_sequential;
            }
        }

        if constexpr (!is_ARM7x) *regmap[ins.Rb] = address;
    }
    if constexpr(!is_ARM7x)
        regs.PC += 2;
    return false;
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(SWI)
{
    /*
Execution SWI/BKPT:
  R14_svc=PC+2     R14_abt=PC+4   ;save return address
  SPSR_svc=CPSR    SPSR_abt=CPSR  ;save CPSR flags
  CPSR=<changed>   CPSR=<changed> ;Enter svc/abt, ARM state, IRQs disabled
  PC=VVVV0008h     PC=VVVV000Ch   ;jump to SWI/PrefetchAbort vector address
     */
    regs.R_svc[1] = regs.PC - 2;
    regs.SPSR_svc = regs.CPSR.u;
    regs.CPSR.mode = M_supervisor;
    regs.CPSR.T = 0; // exit THUMB
    regs.CPSR.I = 1; // mask IRQ
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    fill_regmap();
    regs.PC = regs.EBR | 0x00000008;
    reload_pipeline_ARM<do_debug, cached>();
    return true;
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(BKPT)
{
    regs.R_abt[1] = regs.PC - 2;
    regs.SPSR_abt = regs.CPSR.u;
    regs.CPSR.mode = M_abort;
    regs.CPSR.T = 0; // exit THUMB
    regs.CPSR.I = 1; // mask IRQ
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    fill_regmap();
    regs.PC = regs.EBR | 0x0000000C;
    reload_pipeline<do_debug, cached>();
    return true;
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(UNDEFINED_BCC)
{
    return THUMB_ins_BCC<do_debug, cached>(ins);
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(BCC)
{
    regs.PC += 2;
    if (condition_passes(static_cast<condition_codes>(ins.sub_opcode))) {
        pipeline.access = ARM32P_nonsequential | ARM32P_code;
        regs.PC += ins.imm;
        reload_pipeline<do_debug, cached>();
        return true;
    }

    pipeline.access = ARM32P_sequential | ARM32P_code;
    return false;
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(B)
{
    regs.PC += ins.imm;
    reload_pipeline<do_debug, cached>();
    return true;
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(BL_BLX_prefix)
{
    u32 *lr = regmap[14];
    regs.PC += 2;
    *lr = regs.PC + ins.imm - 2;
    pipeline.access = ARM32P_sequential | ARM32P_code;
    return false;
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(BL_suffix)
{
    const u32 v = (regs.PC - 2) | 1;
    u32 *LR = regmap[14];
    regs.PC = ((*LR) + ins.imm) & 0xFFFFFFFE;
    *LR = v;
    reload_pipeline<do_debug, cached>();
    return true;
}

THUMBip
template<bool do_debug, bool cached>
THUMBIs(BLX_suffix)
{
    const u32 v = (regs.PC - 2) | 1;
    u32 *LR = regmap[14];
    const u32 addr = (*LR) + ins.imm;
    *LR = v;
    regs.PC = addr & 0xFFFFFFFC;
    regs.CPSR.T = 0;
    reload_pipeline<do_debug, cached>();
    return true;
}

#undef PC
#undef is_ARM7x
#undef THUMBip
#undef THUMBIs
#undef setnz