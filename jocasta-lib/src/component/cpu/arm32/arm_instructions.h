#pragma once
#define AREAD(addr, sz) read<sz, do_debug>((addr), ARM32P_nonsequential)
#define AWRITE(addr, sz, val) write<sz, do_debug, false>((addr), ARM32P_nonsequential, (val) & mask)
#define is_ARM7x (cpukind <= AT_ARM7TDMI)

#define OBIT(x) ((opcode >> (x)) & 1)

#define PC R[15]

#define ARMi(name) template <armtype cpukind, typename scheduler_kind> template <bool do_debug> void core<cpukind, scheduler_kind>::ARM_ins_##name(u32 opcode)

template<armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::MUL(u32 product, u32 multiplicand, u32 multiplier, u32 S) {
    u32 n = 1;
    if constexpr (cpukind == AT_ARM7DI) {
        n = __builtin_clz(multiplier) >> 1;
        if (n > 16) n = 16;
    }
    else {
        if((multiplier >> 8) && (multiplier >>  8 != 0xFFFFFF)) n++;
        if((multiplier >> 16) && (multiplier >> 16 != 0xFFFF)) n++;
        if((multiplier >> 24) && (multiplier >> 24 != 0xFF)) n++;
    }

    idle(n);
    product += multiplicand * multiplier;
    if(regs.CPSR.T || S) {
        regs.CPSR.Z = product == 0;
        regs.CPSR.N = (product >> 31) & 1;
    }
    return product;
}

ARMi(MUL_MLA)
{
    const u32 accumulate = OBIT(21);
    if (accumulate) idle(1);
    const u32 S = OBIT(20);
    const u32 Rdd = (opcode >> 16) & 15;
    const u32 Rnd = (opcode >> 12) & 15;
    const u32 Rsd = (opcode >> 8) & 15;
    const u32 Rmd = opcode & 15;
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    const u32 Rn = *regmap[Rnd];
    const u32 Rm = *regmap[Rmd];
    const u32 Rs = *regmap[Rsd];
    u32 *Rd = regmap[Rdd];
    *Rd = MUL(accumulate ? Rn : 0, Rm, Rs, S);
    if (Rdd == 15) reload_pipeline<do_debug, false>();
}

ARMi(MULL_MLAL)
{
    u32 S = OBIT(20);
    const u32 Rdd = (opcode >> 16) & 15;
    const u32 Rnd = (opcode >> 12) & 15;
    const u32 Rsd = (opcode >> 8) & 15;
    const u32 Rmd = opcode & 15;
    const u32 sign = OBIT(22); // signed if =1
    const u32 accumulate = OBIT(21); // acumulate if =1
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    const u64 Rm = *regmap[Rmd];
    const u64 Rs = *regmap[Rsd];

    u32 n = 2 + accumulate;
    u64 result = 0;
    if (sign) {
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

    u32 *Rd = regmap[Rdd];
    u32 *Rn = regmap[Rnd];
    if (accumulate) result += (static_cast<u64>(*Rd) << 32) | static_cast<u64>(*Rn);
    *Rn = result & 0xFFFFFFFF;
    *Rd = result >> 32;
    if ((Rnd == 15) || (Rdd == 15)) reload_pipeline<do_debug, false>();
    if (S) {
        regs.CPSR.N = (result >> 63) & 1;
        regs.CPSR.Z = result == 0;
    }
}

ARMi(SWP)
{
    const u32 B = OBIT(22);
    const u32 Rnd = (opcode >> 16) & 15;
    const u32 Rdd = (opcode >> 12) & 15;
    const u32 Rmd = opcode & 15;
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    // Rd=[Rn], [Rn]=Rm
    const u32 *Rn = regmap[Rnd];
    u32 *Rd = regmap[Rdd];
    const u32 *Rm = regmap[Rmd];
    u32 mask = 0xFFFFFFFF;
    u32 tmp;
    u8 sz = 4;
    if (B) {
        mask = 0xFF;
        sz = 1;
        tmp = read<1, do_debug>(*Rn, ARM32P_nonsequential) & mask;
        write<1, do_debug, false>(*Rn, ARM32P_nonsequential | ARM32P_lock, (*Rm) & mask); // Rm = [Rn]
    }
    else {
        tmp = read<4, do_debug>(*Rn, ARM32P_nonsequential) & mask;
        write<4, do_debug, false>(*Rn, ARM32P_nonsequential | ARM32P_lock, (*Rm) & mask); // Rm = [Rn]
    }
    idle(1);
    if (!B) tmp = align_val(*Rn, tmp);
    *Rd = tmp; // Rd = [Rn]
    if (Rdd == 15) reload_pipeline<do_debug, false>();
}

ARMi(LDRH_STRH)
{
    const u32 P = OBIT(24); // pre or post. 0=post
    const u32 U = OBIT(23); // up/down, 0=down
    const u32 I = OBIT(22); // 0 = register, 1 = immediate offset
    const u32 L = OBIT(20);
    u32 W = 1;
    if (P) W = OBIT(21);
    const u32 Rnd = (opcode >> 16) & 15;
    const u32 Rdd = (opcode >> 12) & 15;
    u32 imm_off = 0;
    imm_off = ((opcode >> 8) & 15) << 4;
    imm_off |= (opcode & 15);
    const u32 Rmd = opcode & 15; // Offset register
    u32 *Rn = regmap[Rnd];
    u32 *Rd = regmap[Rdd];
    const u32 Rm = I ? imm_off : *regmap[Rmd];
    u32 addr = *Rn;
    if (P) addr = U ? (addr + Rm) : (addr - Rm);
    // L = 0 is store
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if (L) {
        u32 val = read<2, do_debug>(addr, ARM32P_nonsequential);
        if (addr &  1) { // read of a halfword to a unaligned address produces a weird ROR
            val = ((val >> 8) & 0xFF) | (val << 24);
        }
        if (!P) addr = U ? (addr + Rm) : (addr - Rm);
        bool flush = false;
        if (W && !((Rnd == 15) && (Rdd == 15))) {
            if (Rnd == 15) {
                // writeback fails. technically invalid here
                *Rn = addr + 4;
                flush = true;
            }
            else
                *Rn = addr;
        }
        if constexpr (is_ARM7x) idle(1);
        *Rd = val;
        flush |= (Rdd == 15);
        if (flush) reload_pipeline<do_debug, false>();
    }
    else {
        u32 val = *Rd;
        /*if (addr & 3) {
            val = (val << 16) | (val >> 16);
        }*/
        write<2, do_debug, false>(addr, ARM32P_nonsequential, val & 0xFFFF);
        if (!P) addr = U ? (addr + Rm) : (addr - Rm);
        if (W) {
            if (Rnd == 15) {
                // writeback fails. technically invalid here
                *Rn = addr + 4;
                reload_pipeline<do_debug, false>();
            }
            else
                *Rn = addr;
        }
    }

}

ARMi(LDRSB_LDRSH)
{
    const u32 P = OBIT(24); // pre or post. 0=post
    const u32 U = OBIT(23); // up/down, 0=down
    const u32 I = OBIT(22); //
    u32 W = 1;
    if (P) W = OBIT(21);
    const u32 Rnd = (opcode >> 16) & 15;
    const u32 Rdd = (opcode >> 12) & 15;
    u32 imm_off = ((opcode >> 8) & 15) << 4;
    imm_off |= (opcode & 15);
    const u32 Rmd = opcode & 15; // Offset register
    u32 *Rn = regmap[Rnd];
    u32 *Rd = regmap[Rdd];
    u32 Rm = I ? imm_off : *regmap[Rmd];
    u32 addr = *Rn;
    if (P) addr = U ? (addr + Rm) : (addr - Rm);

    u32 H = OBIT(5);

    u8 sz = H ? 2 : 1;

    u32 val;
    if (H) val = read<2, do_debug>(addr, ARM32P_nonsequential);
    else val = read<1, do_debug>(addr, ARM32P_nonsequential);

    if (H && !(addr & 1)) { // read of a halfword to a unaligned address produces a byte-extend
        val = sign_extend<16>(val);
    }
    else {
        if (H) val = (val >> 8); // what we said above...
        val = sign_extend<8>(val);
    }
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if (!P) addr = U ? (addr + Rm) : (addr - Rm);
    bool flush = false;
    if (W) {
        if (Rnd == 15) {// writeback fails. technically invalid here
            if (Rdd != 15) {
                *Rn = addr + 4;
                flush = true;
            }
        }
        else
            *Rn = addr;
    }
    if constexpr (is_ARM7x) idle(1);
    *Rd = val;
    flush |= (Rdd == 15);
    if (flush) reload_pipeline<do_debug, false>();
}

ARMi(MRS)
{
    const u32 PSR = OBIT(22); // 0 = CPSR, 1 = SPSR(current)
    const u32 Rdd = (opcode >> 12) & 15;
    u32 *Rd = regmap[Rdd];
    if (PSR) {
        if (regs.CPSR.mode == M_system) *Rd = regs.CPSR.u;
        else *Rd = *get_SPSR_by_mode();
    }
    else {
        *Rd = regs.CPSR.u;
    }

    pipeline.access = ARM32P_sequential | ARM32P_code;
    regs.PC += 4;
}

ARMi(MSR_reg)
{
   const  u32 PSR = OBIT(22); // 0 = CPSR, 1 = SPSR(current)
    u32 f = OBIT(19);
    u32 s = OBIT(18);
    u32 x = OBIT(17);
    u32 c = OBIT(16);
    const u32 Rmd = opcode & 15;
    u32 mask = 0;
    if (f) mask |= 0xFF000000;
    u32 cycles = 0;
    if (s) { cycles = 2; mask |= 0xFF0000; };
    if (x) { cycles = 2; mask |= 0xFF00; };
    if (c) { cycles = 2; mask |= 0xFF; };
    u32 imm = *regmap[Rmd];
    if (!PSR) { // CPSR
        if (regs.CPSR.mode == M_user)
            mask &= 0xFF000000;
        if (mask & 0xFF)
            imm |= 0x10; // force this bit always
        //u32 old_mode = regs.CPSR.mode;
        schedule_IRQ_check<false>();
        regs.CPSR.u = (~mask & regs.CPSR.u) | (imm & mask);
        if (mask & 0x0F) {
            fill_regmap();
        }
    }
    else {
        if ((regs.CPSR.mode != M_user) && (regs.CPSR.mode != M_system)) {
            u32 *v = get_SPSR_by_mode();
            *v = (~mask & *v) | (imm & mask);
        }
        cycles = 2;
    }
    if constexpr (!is_ARM7x) { if (cycles) idle(cycles); }
    pipeline.access = ARM32P_sequential | ARM32P_code;
    regs.PC += 4;
}

ARMi(MSR_imm)
{
    // something -> MSR or CPSR
   const  u32 PSR = OBIT(22); // 0 = CPSR, 1 = SPSR(current)
    u32 f = OBIT(19);
    u32 s = OBIT(18);
    u32 x = OBIT(17);
    u32 c = OBIT(16);
    u32 Is = ((opcode >> 8) & 15) << 1;
    u32 imm = opcode & 255;
    if (Is) imm = (imm << (32 - Is)) | (imm >> Is);
    u32 mask = 0;
    u32 cycles = 0;
    if (f) mask |= 0xFF000000;
    if (s) { mask |= 0xFF0000; cycles = 2; }
    if (x) { mask |= 0xFF00; cycles = 2; }
    if (c) { mask |= 0xFF; cycles = 2; }
    if (!PSR) { // CPSR
        if (regs.CPSR.mode == M_user)
            mask &= 0xFF000000;
        if (mask & 0xFF)
            imm |= 0x10; // force this bit always
        schedule_IRQ_check<false>();
        regs.CPSR.u = (~mask & regs.CPSR.u) | (imm & mask);
        if (mask & 0x0F) {
            fill_regmap();
        }
    }
    else {
        if ((regs.CPSR.mode != M_user) && (regs.CPSR.mode != M_system)) {
            u32 *v = get_SPSR_by_mode();
            *v = (~mask & *v) | (imm & mask);
        }
        cycles = 2;
    }
    if constexpr (!is_ARM7x) { if (cycles) idle(cycles); }
    regs.PC += 4;
    pipeline.access = ARM32P_sequential | ARM32P_code;
}

ARMi(BX)
{
    const u32 Rnd = opcode & 15;
    u32 addr = *regmap[Rnd];
    regs.CPSR.T = addr & 1;
    addr &= 0xFFFFFFFE;
    regs.PC = addr;
    reload_pipeline<do_debug, false>();
}

template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::TEST(const u32 v, const u32 S)
{
    if (regs.CPSR.T || S) {
        regs.CPSR.N = (v >> 31) & 1;
        regs.CPSR.Z = v == 0;
        regs.CPSR.C = temp_carry;
    }
    return v;
}

template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::ADD(const u32 Rnd, const u32 Rmd, const u32 carry, const u32 S)
{
    const u32 result = Rnd + Rmd + carry;
    if (regs.CPSR.T || S) {
        const u32 overflow = ~(Rnd ^ Rmd) & (Rnd ^ result);
        regs.CPSR.V = (overflow >> 31) & 1;
        regs.CPSR.C = ((overflow ^ Rnd ^ Rmd ^ result) >> 31) & 1;
        regs.CPSR.Z = result == 0;
        regs.CPSR.N = (result >> 31) & 1;
    }
    return result;
}


// case 2: v = SUB(Rn, Rm, 1); break;
template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::SUB(const u32 Rn, const u32 Rm, const u32 carry, const u32 S)
{
    const u32 iRm = Rm ^ 0xFFFFFFFF;
    return ADD(Rn, iRm, carry, S);
}

template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::ALU(const u32 Rn, const u32 Rm, const u32 alu_opcode, const u32 S, u32 *out) {
    switch(alu_opcode) {
        case 0: *out = TEST(Rn & Rm, S); break;
        case 1: *out = TEST(Rn ^ Rm, S); break;
        case 2: *out = SUB(Rn, Rm, 1, S); break;
        case 3: *out = SUB(Rm, Rn, 1, S); break;
        case 4: *out = ADD(Rn, Rm, 0, S); break;
        case 5: *out = ADD(Rn, Rm, regs.CPSR.C, S); break; // TODO: xx HUH?
        case 6: *out = SUB(Rn, Rm, regs.CPSR.C, S); break;
        case 7: *out = SUB(Rm, Rn, regs.CPSR.C, S); break;
        case 8: TEST(Rn & Rm, S); break;
        case 9: TEST(Rn ^ Rm, S); break;
        case 10: SUB(Rn, Rm, 1, S); break;
        case 11: ADD(Rn, Rm, 0, S); break;
        case 12: *out = TEST(Rn | Rm, S); break;
        case 13: *out = TEST(Rm, S); break;
        case 14: *out = TEST(Rn & ~Rm, S); break;
        case 15: *out = TEST(Rm ^ 0xFFFFFFFF, S); break;
        default:
            NOGOHERE;
    }
    return *out;
}

// Logical shift left
template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::LSL(const u32 v, const u32 amount) {
    temp_carry = regs.CPSR.C;
    if (amount == 0) return v;
    temp_carry = amount > 32 ? 0 : !!(v & 1 << (32 - amount));
    return (amount > 31) ? 0 : (v << amount);
}

// Logical shift right
template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::LSR(const u32 v, const u32 amount)
{
    temp_carry = regs.CPSR.C;
    if (amount == 0) return v;
    temp_carry = (amount > 32) ? 0 : !!(v & 1 << (amount - 1));
    return (amount > 31) ? 0 : (v >> amount);
}

// Arithemtic (sign-extend) shift right
template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::ASR(const u32 v, const u32 amount)
{
    //   carry = cpsr().c;
    temp_carry = regs.CPSR.C;

    //   if(shift == 0) return source;
    if (amount == 0) return v;

    //   carry = shift > 32 ? source & 1 << 31 : source & 1 << shift - 1;
    temp_carry = (amount >= 32) ? (!!(v & 0x80000000)) : !!(v & (1 << (amount - 1)));

    //   source = shift > 31 ? (i32)source >> 31 : (i32)source >> shift;
    return (amount > 31) ? static_cast<i32>(v) >> 31 : static_cast<i32>(v) >> amount;
}

template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::ROR(u32 v, u32 amount)
{
    temp_carry = regs.CPSR.C;
    if (amount == 0) return v;
    amount &= 31;
    if (amount) v = (v << (32 - amount)) | (v >> amount); // ?correct
    temp_carry = !!(v & 1 << 31);
    return v;
}

// Rotate right thru carry
template <armtype cpukind, typename scheduler_kind>
u32 core<cpukind, scheduler_kind>::RRX(const u32 v)
{
    temp_carry = v & 1;
    return (v >> 1) | (regs.CPSR.C << 31);
}

ARMi(data_proc_immediate_shift)
{
    const u32 alu_opcode = (opcode >> 21) & 15;
    const u32 S = (opcode >> 20) & 1; // set condition codes. 0=no, 1=yes. must be 1 for 8-B
    const u32 Rnd = (opcode >> 16) & 15; // first operand
    const u32 Rdd = (opcode >> 12) & 15; // dest reg.
    const u32 Is = (opcode >> 7) & 31; // shift amount
    const u32 shift_type = (opcode >> 5) & 3; // 0=LSL, 1=LSR, 2=ASR, 3=ROR
    // R(bit4) = 0 for this
    const u32 Rmd = opcode & 15;
    pipeline.access = ARM32P_code | ARM32P_sequential;

    const u32 Rn = *regmap[Rnd];
    u32 Rm = *regmap[Rmd];
    u32 *Rd = regmap[Rdd];
    temp_carry = regs.CPSR.C;
    regs.PC += 4;
    switch(shift_type) {
        case 0: //
            Rm = LSL(Rm, Is);
            break;
        case 1:
            Rm = LSR(Rm, Is ? Is : 32);
            break;
        case 2:
            Rm = ASR(Rm, Is ? Is : 32);
            break;
        case 3:
            Rm = Is ? ROR(Rm, Is) : RRX(Rm);
            break;
        default:
            NOGOHERE;
    }

    ALU(Rn, Rm, alu_opcode, S, Rd);
    if ((S==1) && (Rdd == 15)) {
        if (regs.CPSR.mode != M_system) {
            schedule_IRQ_check<false>();
            regs.CPSR.u = *get_SPSR_by_mode();
        }
        fill_regmap();
    }
    if ((Rdd == 15) && ((alu_opcode < 8) || (alu_opcode > 11))) {
       reload_pipeline<do_debug, false>();
    }
}

ARMi(data_proc_register_shift)
{
    const u32 alu_opcode = (opcode >> 21) & 15;
    const u32 S = (opcode >> 20) & 1; // set condition codes. 0=no, 1=yes. must be 1 for 8-B
    const u32 Rnd = (opcode >> 16) & 15; // first operand
    const u32 Rdd = (opcode >> 12) & 15; // dest reg.
    const u32 Isd = (opcode >> 8) & 15;
    const u32 shift_type = (opcode >> 5) & 3; // 0=LSL, 1=LSR, 2=ASR, 3=ROR
    // R(bit4) = 0 for this
    const u32 Rmd = opcode & 15;

    const u32 Is = (*regmap[Isd]) & 0xFF; // shift amount
    idle(1);
    pipeline.access = ARM32P_code | ARM32P_nonsequential; // weird quirk of ARM
    regs.PC += 4;
    const u32 Rn = *regmap[Rnd];
    u32 Rm = *regmap[Rmd];
    u32 *Rd = regmap[Rdd];
    temp_carry = regs.CPSR.C;
    switch(shift_type) {
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

    ALU(Rn, Rm, alu_opcode, S, Rd);

    if ((S==1) && (Rdd == 15)) {
        if (regs.CPSR.mode != M_system) {
            schedule_IRQ_check<false>();
            regs.CPSR.u = *get_SPSR_by_mode();
        }
        fill_regmap();
    }
    if ((Rdd == 15) && ((alu_opcode < 8) || (alu_opcode > 11)))
        reload_pipeline<do_debug, false>();
}

ARMi(undefined_instruction)
{
    printf("\nARM9 UNDEFINED INS!");
    NOGOHERE;
    regs.R_und[1] = regs.PC - 4;
    regs.SPSR_und = regs.CPSR.u;
    regs.CPSR.mode = M_undefined;
    fill_regmap();
    regs.CPSR.I = 1;
    regs.PC = regs.EBR | 0x00000004;
    reload_pipeline<do_debug, false>();
}

ARMi(data_proc_immediate)
{
    const u32 alu_opcode = (opcode >> 21) & 15;
    const u32 S = (opcode >> 20) & 1; // set condition codes. 0=no, 1=yes. must be 1 for 8-B
    const u32 Rnd = (opcode >> 16) & 15; // first operand
    const u32 Rdd = (opcode >> 12) & 15; // dest reg.

    const u32 Rn = *regmap[Rnd];
    regs.PC += 4;
    u32 *Rd = regmap[Rdd];
    pipeline.access = ARM32P_code | ARM32P_sequential;

    u32 Rm = opcode & 0xFF;
    u32 imm_ROR_amount = (opcode >> 7) & 30;
    temp_carry = regs.CPSR.C;
    if (imm_ROR_amount) Rm = ROR(Rm, imm_ROR_amount);

    ALU(Rn, Rm, alu_opcode, S, Rd);
    if ((S==1) && (Rdd == 15)) {
        if (regs.CPSR.mode != M_system) {
            schedule_IRQ_check<false>();
            regs.CPSR.u = *get_SPSR_by_mode();
        }
        fill_regmap();
    }
    if ((Rdd == 15) && ((alu_opcode < 8) || (alu_opcode > 11)))
        reload_pipeline<do_debug, false>();
}

ARMi(LDR_STR_immediate_offset)
{
    const u32 P = OBIT(24); // Pre/post. 0 = after-transfer, post
    const u32 U = OBIT(23); // 0 = down, 1 = up
    const u32 B = OBIT(22); // byte/word, 0 = 32bit, 1 = 8bit
    // when P=0, bit 21 is T, 0= normal, 1= force nonpriviledged, and W=1
    // when P=1, bit 21 is write-back bit, 0 = normal, 1 = write into base
    u32 T = 0; // 0 = normal, 1 = force unprivileged
    u32 W = 1; // writeback. 0 = no, 1 = write into base
    if (P == 0) { T = OBIT(21); }
    else W = OBIT(21);

    const u32 L = OBIT(20); // store = 0, load = 1
    const u32 Rnd = (opcode >> 16) & 15; // base register. PC=+8
    const u32 Rdd = (opcode >> 12) & 15; // source/dest register. PC=+12

    u32 *Rn = regmap[Rnd];
    u32 *Rd = regmap[Rdd];

    const u32 offset = (opcode & 4095);
    u32 addr = *Rn;
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    //if (Rnd == 15) addr += 4;
    if (P) addr = U ? (addr + offset) : (addr - offset);
    const u8 sz = B ? 1 : 4;
    const u32 mask = B ? 0xFF : 0xFFFFFFFF;
    if (L == 1) {// store to RAM
        // B ? 1, or 4
        u32 v;
        if (B) v = AREAD(addr, 1);
        else v = AREAD(addr, 4);
        if ((addr & 3) && !B) v = align_val(addr, v);
        if (!P) addr = U ? (addr + offset) : (addr - offset);
        bool flush = false;
        if (W) {
            if (Rnd == 15) {
                *Rn = addr + 4;
                flush = true;
            }
            else
                *Rn = addr;
        }
        if constexpr (is_ARM7x) {
            idle(1);
            *Rd = v;
            flush |= Rdd == 15;
            if (flush) reload_pipeline<do_debug, false>();
        }
        else {
            *Rd = v;
            if (Rdd == 15) {
                regs.CPSR.T = regs.PC & 1;
                reload_pipeline<do_debug, false>();
            }
        }
    }
    else {
        if (B) AWRITE(addr, 1, *Rd);
        else AWRITE(addr, 4, *Rd);
        if (!P) addr = U ? (addr + offset) : (addr - offset);
        if (W) {
            if (Rnd == 15) {
                *Rn = addr + 4;
                reload_pipeline<do_debug, false>();
            }
            else
                *Rn = addr;
        }
    }
}

ARMi(LDR_STR_register_offset)
{
    const u32 P = OBIT(24);
    const u32 U = OBIT(23);
    const u32 B = OBIT(22);
    u32 T = 0; // 0 = normal, 1 = force unprivileged
    u32 W = 1; // writeback. 0 = no, 1 = write into base
    if (P == 0) { T = OBIT(21); }
    else W = OBIT(21);
    const u32 L = OBIT(20); // 0 store, 1 load
    const u32 Rnd = (opcode >> 16) & 15; // base reg
    const u32 Rdd = (opcode >> 12) & 15; // source/dest reg
    const u32 Is = (opcode >> 7) & 31;
    const u32 shift_type = (opcode >> 5) & 3;
    const u32 Rmd = opcode & 15;

    u32 *Rn = regmap[Rnd];
    u32 Rm = *regmap[Rmd];
    u32 *Rd = regmap[Rdd];
    temp_carry = regs.CPSR.C;
    switch(shift_type) {
        case 0: //
            Rm = LSL(Rm, Is);
            break;
        case 1:
            Rm = LSR(Rm, Is ? Is : 32);
            break;
        case 2:
            Rm = ASR(Rm, Is ? Is : 32);
            break;
        case 3:
            Rm = Is ? ROR(Rm, Is) : RRX(Rm);
            break;
    }

    u32 addr = *Rn;
    if (P) addr = U ? (addr + Rm) : (addr - Rm);
    const u8 sz = B ? 1 : 4;
    const u32 mask = B ? 0xFF : 0xFFFFFFFF;
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if (L == 1) { // LDR from RAM
        u32 v;
        if (B) v = AREAD(addr, 1);
        else v = AREAD(addr, 4);
        if ((!B) && (addr & 3)) v = align_val(addr, v);
        if (!P) addr = U ? (addr + Rm) : (addr - Rm);
        bool flush = false;
        if (W)  {
            if (Rnd == 15) {
                flush = true;
                *Rn = addr+4;
            }
            else
                *Rn = addr;
        }
        if constexpr (is_ARM7x) {
            idle(1);
            *Rd = v;
            flush |= Rdd == 15;
            if (flush) reload_pipeline<do_debug, false>();
        }
        else {
            *Rd = v;
            if (Rdd == 15) {
                regs.CPSR.T = regs.PC & 1;
                reload_pipeline<do_debug, false>();
            }
        }
    }
    else { // STR to RAM
        if (B) AWRITE(addr, 1, *Rd);
        else AWRITE(addr, 4, *Rd);
        if (!P) addr = U ? (addr + Rm) : (addr - Rm);
        if (W) {
            if (Rnd == 15) {
                *Rn = addr + 4;
                reload_pipeline<do_debug, false>();
            }
            else
                *Rn = addr;
        }
    }
}

ARMi(LDM_STM_ARM7TDMI) {
    u32 P = OBIT(24); // P=0 add offset after. P=1 add offset first
    u32 U = OBIT(23); // 0=subtract offset, 1 =add
    u32 S = OBIT(22); // 0=no, 1=load PSR or force user bit
    u32 W = OBIT(21); // 0=no writeback, 1= writeback
    u32 L = OBIT(20); // 0=store, 1=load
    u32 Rnd = (opcode >> 16) & 15;

    u32 rlist = (opcode & 0xFFFF);
    u32 rcount = 0;
    //u32 *Rd = regmap[Rnd);
    int first = -1;
    u32 bit = 0;
    u32 move_pc = 0;
    // Get first register and register count
    for (u32 i = 0; i < 16; i++) {
        u32 mbit = (rlist >> (bit++)) & 1;
        rcount += mbit;
        if (mbit && (first==-1)) first = static_cast<int>(i);
    }
    u32 byte_sz = rcount << 2; // 4 byte per register

    if (rlist == 0) {
    /* according to NBA, "If the register list is empty, only r15 will be loaded/stored but
     * the base will be incremented/decremented as if each register was transferred."
     */
        rlist = 0x8000;
        first = 15;
        byte_sz = 64;
    }
    move_pc = (rlist >> 15) & 1;

    u32 cur_addr = *regmap[Rnd];
    u32 base_addr = cur_addr;

    u32 do_mode_switch = S && (!L || !move_pc);
    u32 old_mode = regs.CPSR.mode;
    if (do_mode_switch) {
        regs.CPSR.mode = M_user;
        fill_regmap();
    }

    if (!U) {
        P = !P;
        cur_addr -= byte_sz;
        base_addr -= byte_sz;
    }
    else {
        base_addr += byte_sz;
    }

    pipeline.access = ARM32P_code | ARM32P_nonsequential;
    regs.PC += 4;
    int access_type = ARM32P_nonsequential;
    u32 vr = *regmap[Rnd];
    bool flush = false;
    for (int i = first; i < 16; i++) {
        if (~rlist & (1 << i)) {
            continue;
        }
        if (P) {
            cur_addr += 4;
        }

        if (L) {
            u32 v = read<4, do_debug>(cur_addr, access_type);
            if (W && (i == first)) {
                *regmap[Rnd] = base_addr;
                flush |= Rnd == 15;
            }
            *regmap[i] = v;
            flush |= i == 15;
        }
        else {
            write<4, do_debug, false>(cur_addr, access_type, *regmap[i]);
            if (W && (i == first)) {
                *regmap[Rnd] = base_addr;
                flush |= Rnd == 15;
            }
        }

        if (!P) cur_addr += 4;
        access_type = ARM32P_sequential;
    }
    if (L) {
        idle(1);
        if (do_mode_switch) {
            // According to MBA,
            /*"     During the following two cycles of a usermode LDM,\n"
                   register accesses will go to both the user bank and original bank.
                   */
            // TODO: th
        }

        if (move_pc) {
            if (S) { // If force usermode...
                regs.CPSR.u |= 0x10;
                switch(old_mode) {
                    case M_system:
                    case M_user:
                        break;
                    case M_fiq:
                        schedule_IRQ_check<false>();
                        regs.CPSR.u = regs.SPSR_fiq; break;
                    case M_irq:
                        schedule_IRQ_check<false>();
                        regs.CPSR.u = regs.SPSR_irq; break;
                    case M_supervisor:
                        schedule_IRQ_check<false>();
                        regs.CPSR.u = regs.SPSR_svc; break;
                    case M_abort:
                        schedule_IRQ_check<false>();
                        regs.CPSR.u = regs.SPSR_abt; break;
                    case M_undefined:
                        schedule_IRQ_check<false>();
                        regs.CPSR.u = regs.SPSR_und; break;
                    default:
                        break;
                }
                flush = true;
            }
            fill_regmap();
        }
    }
    if (do_mode_switch) {
        regs.CPSR.mode = old_mode;
        fill_regmap();
    }
    if (flush) reload_pipeline<do_debug, false>();
}

ARMi(LDM_STM_ARM946ES) {
    const u32 P = OBIT(24); // P=0 add offset after. P=1 add offset first
    const u32 U = OBIT(23); // 0=subtract offset, 1 =add
    const u32 S = OBIT(22); // 0=no, 1=load PSR or force user bit
    const u32 W = OBIT(21); // 0=no writeback, 1= writeback
    const u32 L = OBIT(20); // 0=store, 1=load
    const u32 Rnd = (opcode >> 16) & 15;

    const i32 rlist = (opcode & 0xFFFF);
    bool move_pc = (rlist >> 15) & 1;;

    u32 bytes = 0;
    u32 base_new;
    u32 addr = *regmap[Rnd];
    bool Rnd_is_last = false;

    if (rlist != 0) {
        // Get size in bytes
        for (u32 i = 0; i <= 15; i++) {
            if ((rlist >> i) & 1)
                bytes += sizeof(u32);
        }

        Rnd_is_last = (rlist >> Rnd) == 1; // Get if Rnd_is_last
    } else {
        bytes = 64;
    }

    if (!U) {
        addr -= bytes;
        base_new = addr;
    } else {
        base_new = addr + bytes;
    }
    if (rlist == 0) {
        *regmap[Rnd] = base_new;
        regs.PC += 4;
        return; // ARM9 adjust base register and returns
    }

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_nonsequential;

    const u32 mode = regs.CPSR.mode;

    if (S && (!L || !move_pc)) {
        regs.CPSR.mode = M_user;
        fill_regmap();
    }

    u32 i = 0;
    u32 remaining = rlist;
    u32 access = ARM32P_nonsequential;
    while (remaining != 0) {
        while (((remaining >> i) &  1) == 0) i++;

        if (P == U)
            addr += 4;

        if (L) *regmap[i] = read<4, do_debug>(addr, access);
        else write<4, do_debug, false>(addr, access, *regmap[i]);

        access = ARM32P_sequential;

        if (P != U)
            addr += 4;

        remaining &= ~(1 << i);
    }

    if (S) {
        if (L && move_pc) {
            if (regs.CPSR.mode != M_system) {
                schedule_IRQ_check<false>();
                regs.CPSR.u = *get_SPSR_by_mode();
            }
        } else {
            regs.CPSR.mode = mode;
        }
        fill_regmap();
    }

    if (W) {
        if (L) {
            // writeback if base is the only register or *NOT* the "last" register
            if (!Rnd_is_last || rlist == (1 << Rnd)) {
                *regmap[Rnd] = base_new;
            }
        } else {
            *regmap[Rnd] = base_new;
        }
    }

    if (L && move_pc) {
        if ((regs.PC & 1) && !S) {
            regs.CPSR.T = 1;
            regs.PC &= 0xFFFFFFFE;
        }
        reload_pipeline<do_debug, false>();
    }
}

ARMi(STC_LDC)
{
    printf("\nWARNING STC/LDC");
    regs.PC += 4;
}

ARMi(CDP)
{
    //UNIMPLEMENTED;
}

ARMi(MCR_MRC)
{
    if (regs.CPSR.mode == M_user) {
        undefined_exception<do_debug, false>();
        return;
    }

    u32 v2 = ((opcode >> 28) & 15) == 15;
    const u32 cp_opc = (opcode >> 21) & 7; // CP Opc - Coprocessor operation code
    const u32 copro_to_arm = !OBIT(20);
    const u32 Cnd = (opcode >> 16) & 15; // Cn     - Coprocessor source/dest. Register  (C0-C15)
    const u32 Rdd = (opcode >> 12) & 15; // Rd     - ARM source/destination Register    (R0-R15)
    const u32 Pnd = (opcode >> 8) & 15; // Coprocessor number                 (P0-P15)
    const u32 CP = (opcode >> 5) & 7; // CP     - Coprocessor information            (0-7)
    const u32 Cmd = opcode & 15; //  Coprocessor operand Register       (C0-C15)
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;

    if (!copro_to_arm) { // ARM->CoPro
         const u32 val = CP_read(CP_ptr, Pnd, cp_opc, Cnd, Cmd, CP);
         if (Rdd == 15) {
             // When using MRC with R15: Bit 31-28 of data are copied to Bit 31-28 of CPSR (ie. N,Z,C,V flags), other data bits are ignored, CPSR Bit 27-0 are not affected, R15 (PC) is not affected.     */
             schedule_IRQ_check<false>();
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
        CP_write(CP_ptr, Pnd, cp_opc, Cnd, Cmd, CP, v);
    }
    idle(1);
}

ARMi(BKPT)
{
    if ((opcode >> 28) == 14) { // BKPT
        regs.R_abt[1] = regs.PC - 4;
        regs.SPSR_abt = regs.CPSR.u;
        regs.CPSR.mode = M_abort;
        fill_regmap();
        regs.CPSR.I = 1;
        regs.PC = regs.EBR | 0x0000000C;
        reload_pipeline<do_debug, false>();
        printf("\nARM9 BKPT!?");
    }
    else {
        ARM_ins_undefined_instruction<do_debug>(opcode);
    }
}

ARMi(SWI)
{
    regs.R_svc[1] = regs.PC - 4;
    regs.SPSR_svc = regs.CPSR.u;
    regs.CPSR.mode = M_supervisor;
    fill_regmap();
    regs.CPSR.I = 1;
    regs.PC = regs.EBR | 0x00000008;
    reload_pipeline<do_debug, false>();
    //printf("\nWARNING SWI %d", opcode & 0xFF);
}

ARMi(INVALID)
{
    //UNIMPLEMENTED;
}

ARMi(PLD)
{
    //UNIMPLEMENTED;
    printf("\nPLD!");
}

ARMi(SMLAxy)
{
    // Passes armwrestler.nds tests!
    const u32 Rdd = (opcode >> 16) & 15;
    const u32 Rnd = (opcode >> 12) & 15;
    const u32 Rsd = (opcode >> 8) & 15;
    const u32 y = OBIT(6); // 1 = RS top, 0 bottom
    const u32 x = OBIT(5); // 1 = Rm top, 0 bottom
    const u32 Rmd = opcode & 15;

    i16 value1, value2;

    if (x) value1 = static_cast<i16>(*regmap[Rmd] >> 16);
    else value1 = static_cast<i16>(*regmap[Rmd] & 0xFFFF);

    if (y) value2 = static_cast<i16>(*regmap[Rsd] >> 16);
    else value2 = static_cast<i16>(*regmap[Rsd] & 0xFFFF);

    const u32 first_result = static_cast<u32>(static_cast<i32>(value1) * static_cast<i32>(value2));
    const u32 mop2 = *regmap[Rnd];
    const u32 final_result = first_result + mop2;

    if((~(first_result ^ mop2) & (mop2 ^ final_result)) >> 31)
        regs.CPSR.Q = 1;

    *regmap[Rdd] = final_result;

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;
}

ARMi(SMLAWy)
{
    const u32 Rdd = (opcode >> 16) & 15;
    const u32 Rnd = (opcode >> 12) & 15;
    const u32 Rsd = (opcode >> 8) & 15;
    const u32 y = OBIT(6); // 1 = RS top, 0 bottom
    const u32 Rmd = opcode & 15;

    i32 value1 = static_cast<i32>(*regmap[Rmd]);
    i16 value2;

    if (y) value2 = static_cast<i16>(*regmap[Rsd] >> 16);
    else value2 = static_cast<i16>(*regmap[Rsd] & 0xFFFF);

    const u32 first_result = static_cast<u32>((static_cast<i32>(value1) * static_cast<i32>(value2)) >> 16);
    const u32 mop2 = *regmap[Rnd];
    const u32 final_result = first_result + mop2;

    if((~(first_result ^ mop2) & (mop2 ^ final_result)) >> 31) {
        regs.CPSR.Q = 1;
    }

    *regmap[Rdd] = final_result;

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;
}

ARMi(SMULWy)
{
    // 1001b: SMULWy{cond}   Rd,Rm,Rs        ;Rd=(Rm*HalfRs)/10000h
    const u32 Rdd = (opcode >> 16) & 15;
    //const u32 Rnd = (opcode >> 12) & 15;
    const u32 Rsd = (opcode >> 8) & 15;
    const u32 y = OBIT(6); // 1 = RS top, 0 bottom
    const u32 Rmd = opcode & 15;

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;

    u32 *Rd = regmap[Rdd];
    u32 Rs = *regmap[Rsd];
    const u32 Rm = *regmap[Rmd];

    if (y) Rs >>= 16;
    else Rs &= 0xFFFF;

    const u64 result = (static_cast<i64>(static_cast<i32>(Rm))) * static_cast<i64>(static_cast<i16>(Rs)) >> 16;

    *Rd = result;

    if (Rdd == 15) {
       reload_pipeline<do_debug, false>();
    }
}

ARMi(SMLALxy)
{
    const u32 Rdd = (opcode >> 16) & 15;
    const u32 Rnd = (opcode >> 12) & 15;
    const u32 Rsd = (opcode >> 8) & 15;
    const u32 y = OBIT(6); // 1 = RS top, 0 bottom
    const u32 x = OBIT(5); // 1 = Rm top, 0 bottom
    const u32 Rmd = opcode & 15;

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_nonsequential; // this one becomes nonsequential

    u32 *Rd = regmap[Rdd];
    u32 Rs = *regmap[Rsd];
    u32 Rm = *regmap[Rmd];
    u32 *Rn = regmap[Rnd];

    if (y) Rs >>= 16;
    else Rs &= 0xFFFF;
    if (x) Rm >>= 16;
    else Rm &= 0xFFFF;

    i64 result = static_cast<i64>(static_cast<i16>(Rm)) * static_cast<i64>(static_cast<i16>(Rs));
    result += static_cast<i64>(static_cast<u64>(*Rn) | (static_cast<u64>(*Rd) << 32));

    *Rn = static_cast<u64>(result) & 0xFFFFFFFF;
    *Rd = static_cast<u64>(result) >> 32;
    if (Rdd == 15) {
       reload_pipeline<do_debug, false>();
    }
}

ARMi(SMULxy)
{
    const u32 Rdd = (opcode >> 16) & 15;
    const u32 Rsd = (opcode >> 8) & 15;
    const u32 y = OBIT(6); // 1 = RS top, 0 bottom
    const u32 x = OBIT(5); // 1 = Rm top, 0 bottom
    const u32 Rmd = opcode & 15;

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_nonsequential; // this one becomes nonsequential

    u32 *Rd = regmap[Rdd];
    u32 Rs = *regmap[Rsd];
    u32 Rm = *regmap[Rmd];

    if (y) Rs >>= 16;
    else Rs &= 0xFFFF;
    if (x) Rm >>= 16;
    else Rm &= 0xFFFF;

    *Rd = (static_cast<i32>(static_cast<i16>(Rm)) * static_cast<i32>(static_cast<i16>(Rs)));
    if (Rdd == 15) {
       reload_pipeline<do_debug, false>();
    }
}

ARMi(LDRD_STRD)
{
/*
2: LDR{cond}D  Rd,<Address>  ;Load Doubleword  R(d)=[a], R(d+1)=[a+4]
3: STR{cond}D  Rd,<Address>  ;Store Doubleword [a]=R(d), [a+4]=R(d+1)
STRD/LDRD: Rd must be an even numbered register (R0,R2,R4,R6,R8,R10,R12).
STRD/LDRD: Address must be double-word aligned (multiple of eight).
          */
    const u32 P = OBIT(24); // pre or post. 0=post
    const u32 U = OBIT(23); // up/down, 0=down
    const u32 I = OBIT(22); // 0 = register, 1 = immediate offset
    const u32 L = !OBIT(5); // bits 5-6 = opcode. 10 = LDR, 11 = STR. so bit 5 == 1 = STR
    u32 W = 1;
    if (P) W = OBIT(21);

    const u32 Rnd = (opcode >> 16) & 15;
    const u32 Rdd = (opcode >> 12) & 14; // Only use top 3 bits
    u32 imm_off = 0;
    imm_off = ((opcode >> 8) & 15) << 4;
    imm_off |= (opcode & 15);
    const u32 Rmd = opcode & 15; // Offset register
    u32 *Rn = regmap[Rnd];
    u32 *Rd = regmap[Rdd];
    u32 *Rdp1 = regmap[Rdd | 1];
    const u32 Rm = I ? imm_off : *regmap[Rmd];
    u32 addr = *Rn;
    if (P) addr = U ? (addr + Rm) : (addr - Rm);
    regs.PC += 4;
    pipeline.access = ARM32P_nonsequential | ARM32P_code;
    if (L) {
        const u32 val = read<4, do_debug>(addr, ARM32P_nonsequential);
        const u32 val_hi = read<4, do_debug>(addr+4, ARM32P_sequential);

        if (!P) addr = U ? (addr + Rm) : (addr - Rm);
        bool flush = false;
        if (W && !((Rnd == 15) && (Rdd == 14))) {
            if (Rnd == 15) {
                // writeback fails. technically invalid here
                flush = true;
                *Rn = addr + 4;
            }
            else
                *Rn = addr;
        }
        *Rd = val;
        *Rdp1 = val_hi;
        flush |= Rdd >= 14;
        if (flush) reload_pipeline<do_debug, false>();
    }
    else {
        write<4, do_debug, false>(addr, ARM32P_nonsequential, *Rd);
        write<4, do_debug, false>(addr+4, ARM32P_sequential, *Rdp1);
        if (!P) addr = U ? (addr + Rm) : (addr - Rm);
        if (W) {
            if (Rnd == 15) {
                // writeback fails. technically invalid here
                *Rn = addr + 4;
                reload_pipeline<do_debug, false>();
            }
            else
                *Rn = addr;
        }
    }
}

ARMi(CLZ)
{
    const u32 Rdd = (opcode >> 12) & 15;
    const u32 Rmd = opcode & 15;

    const u32 v = *regmap[Rmd];

    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;

    *regmap[Rdd] = (v == 0) ? 32 : __builtin_clz(v);
    if (Rdd == 15) reload_pipeline<do_debug, false>();
}

ARMi(BLX_reg)
{
    const u32 link = regs.PC - 4;
    regs.PC = (*regmap[opcode & 15]);
    *regmap[14] = link;
    regs.CPSR.T = regs.PC & 1;
    if (regs.CPSR.T) regs.PC &= 0xFFFFFFFE;
    else regs.PC &= 0xFFFFFFFC;

   reload_pipeline<do_debug, false>();
}

ARMi(QADD_QSUB_QDADD_QDSUB) {
    const u32 src1 =  opcode & 15;
    const u32 src2 = (opcode >> 16) & 15;
    const u32 dst  = (opcode >> 12) & 15;
    u32 op2  = *regmap[src2];

    const u32 subtract = OBIT(21);
    const u32 double_op2 = OBIT(22);

    if(double_op2) {
        u32 result = op2 + op2;

        if((op2 ^ result) >> 31) {
            regs.CPSR.Q = 1;
            result = 0x80000000 - (result >> 31);
        }

        op2 = result;
    }

    if (subtract) {
        const u32 op1 = *regmap[src1];
        u32 result = op1 - op2;

        if(((op1 ^ op2) & (op1 ^ result)) >> 31) {
            regs.CPSR.Q = 1;
            result = 0x80000000 - (result >> 31);
        }

        *regmap[dst] = result;
    } else {
        const u32 op1 = *regmap[src1];
        u32 result = op1 + op2;

        if((~(op1 ^ op2) & (op2 ^ result)) >> 31) {
            regs.CPSR.Q = 1;
            result = 0x80000000 - (result >> 31);
        }
        *regmap[dst] = result;
    }

    regs.PC += 4;
    pipeline.access = ARM32P_sequential | ARM32P_code;
}

ARMi(B_BL)
{
    const u32 link = OBIT(24);
    i32 offset = sign_extend<24>(opcode & 0xFFFFFF);
    offset <<= 2;
    if (link) {
        *regmap[14] = regs.PC - 4;
    }
    regs.PC += static_cast<u32>(offset);
    reload_pipeline<do_debug, false>();
}

ARMi(BLX)
{
    i32 offset = sign_extend<24>(opcode & 0xFFFFFF);
    offset <<= 2;
    const i32 H = OBIT(24) << 1;
    offset += H;

    *regmap[14] = regs.PC - 4;
    regs.PC += static_cast<u32>(offset);
    regs.CPSR.T = 1;

    reload_pipeline<do_debug, false>();
}

#undef AREAD
#undef AWRITE
#undef OBIT
#undef getR
#undef PC
#undef is_ARM7x
#undef ARMi
