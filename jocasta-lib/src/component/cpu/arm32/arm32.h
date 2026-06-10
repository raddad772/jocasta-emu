/********
 * jocasta-lib ARM32 (ARM7DI, ARM7TDMI, ARM946ES and soon ARM11MPCore) cached/interpreter.
 *
 */

#pragma once

#include <cassert>

#include "helpers/int.h"
#include "helpers/setbits.h"
#include "helpers/block_cache_better.h"
#include "helpers/debug.h"
#include "helpers/debugger/debuggerdefs.h"
#include "helpers/serialize/serialize.h"
#include "helpers/scheduler.h"

#include "disassemblers/disassemblers.h"
#include "nds_cp15.h"
namespace ARM32 {
#include "helpers/multisize_memaccess.cpp"

#define ARM32P_nonsequential 0
#define ARM32P_sequential 1
#define ARM32P_code 2
#define ARM32P_dma 4
#define ARM32P_lock 8

enum modes {
    M_old_user = 0,
    M_old_fiq = 1,
    M_old_irq = 2,
    M_old_supervisor = 3,
    M_user = 16,
    M_fiq = 17,
    M_irq = 18,
    M_supervisor = 19,
    M_abort = 23,
    M_undefined = 27,
    M_system = 31
};

enum condition_codes {
    CC_EQ = 0, // Z=1
    CC_NE = 1, // Z=0
    CC_CS_HS = 2, // C=1
    CC_CC_LO = 3, // C=0
    CC_MI = 4, // N=1
    CC_PL = 5, // N=0
    CC_VS = 6, // V=1
    CC_VC = 7, // V=0
    CC_HI = 8, // C=1 and Z=0
    CC_LS = 9, // C=0 or Z=1
    CC_GE = 10, // N=V
    CC_LT = 11, // N!=V
    CC_GT = 12, // Z=0 and N=V
    CC_LE = 13, // Z=1 or N!=V
    CC_AL = 14, // "always"
    CC_NV = 15 // never. don't execute opcode
};


enum armtype : u32 {
    AT_ARM7DI = 0,
    AT_ARM7TDMI = 1,
    AT_ARM946ES = 2,
    AT_ARM11MPCore = 3
};
template<armtype cpukind, typename scheduler_kind> struct core;

    typedef u32 (*CP_read_func)(void *, u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP);
    typedef bool (*CP_write_func)(void *, u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP, u32 val);

struct REGS {
    u32 R[16]{};
    u32 R_fiq[7]{};
    u32 R_svc[2]{};
    u32 R_abt[2]{};
    u32 R_irq[2]{};
    u32 R_und[2]{};

    u32 R_invalid[16]{};

    u32 SPSR_fiq{};
    u32 SPSR_svc{};
    u32 SPSR_abt{};
    u32 SPSR_irq{};
    u32 SPSR_und{};
    u32 SPSR_invalid{};
    union ARM946ES_CPSR {
        struct {
            u32 mode : 5;
            u32 T: 1; // T - state bit. 0 = ARM{}, 1 = THUMB
            u32 F: 1; // F - FIQ disable
            u32 I: 1; // I - IRQ disable
            //u32 A: 1; // A - abort disable. _2 is : 14 (or 15) if uncommented
            //u32 E: 1; // E - endian. _2 is : 15 if uncommented
            u32 _2 : 16;
            u32 J: 1; // J - Jazelle (Java) mode
            u32 _3 : 2; // this is 2 because Q. it makes sense.
            u32 Q: 1; // Sticky overflow{}, ARmv5TE and up only.
            u32 V: 1; // 0 = no overflow{}, 1 = overflow
            u32 C: 1; // 0 = borrow/no carry{}, 1 = carry/no borrow
            u32 Z: 1; // 0 = not zero{}, 1 = zero
            u32 N: 1; // 0 = not signed{}, 1 = signed
        };
        u32 u{};
    } CPSR{};

    union {
        u32 b;
        u32 u{};
    } SPSR{};

    u32 IRQ_line{};
    bool FIQ_line{};
    u32 EBR{};
};

template<armtype cpukind, typename scheduler_kind>
struct arm32_ins {
    void (core<cpukind, scheduler_kind>::*exec)(u32 opcode){};
    bool valid{};
};

template<armtype cpukind, typename scheduler_kind>
struct arm32_cached_ins {
    union {
        u8 Rdd{};
        u8 fsxc;
        u8 src1;
        u8 Rd;
    };
    union {
        u8 Rnd{};
        u8 Rn;
        u8 Rb;
        u8 dst;
    };
    union {
        u8 Rmd{};
        u8 Ro;
    };

    union {
        u8 Rsd{};
        u8 Rs;
        u8 alu_opcode;
        u8 src2;
    };
    union {
        u16 L{};
        u16 I;
        u16 B;
        u16 SP;
        u16 S;
        u16 PC_LR;
    };

    condition_codes cc{}; // condition code
    u8 sub_opcode{};
    u32 opcode{};
    union {
        u32 immediate_offset{};
        u32 imm;
        u16 rlist;
        u16 comment;
    };
    bool (core<cpukind, scheduler_kind>::*exec)(const arm32_cached_ins<cpukind, scheduler_kind> &ins){};
    bool (core<cpukind, scheduler_kind>::*exec_debug)(const arm32_cached_ins<cpukind, scheduler_kind> &ins){};

    u8 kind{};
    bool ends_block{};
};

template<armtype cpukind, typename scheduler_kind>
struct cached_block {
    u32 cached_addr{}, sz{}, exec_count{};
    u32 page_first{}, page_span{};
    u32 version_snap[5]{};
    u8 mem_region{}; // To avoid doubling up on address decodes

    std::vector<arm32_cached_ins<cpukind, scheduler_kind>> instructions{};
};

namespace AIK {
    enum ins_kind {
        MUL_MLA,
        MULL_MLAL,
        SWP,
        LDRH_STRH,
        LDRSB_LDRSH,
        MRS,
        MSR_reg,
        MSR_imm,
        BX,
        data_proc_immediate_shift,
        data_proc_register_shift,
        data_proc_undefined,
        undefined_instruction,
        data_proc_immediate,
        LDR_STR_immediate_offset,
        LDR_STR_register_offset,
        LDM_STM,
        STC_LDC,
        CDP,
        MCR_MRC,
        SWI,
        INVALID,
        PLD,
        SMLAxy,
        SMLAWy,
        SMULWy,
        SMLALxy,
        SMULxy,
        LDRD_STRD,
        CLZ,
        BLX_reg,
        QADD_QSUB_QDADD_QDSUB,
        BKPT,
        B_BL,
        BLX
    };
}

namespace TIK {
    enum ins_kind {
        ADD_SUB,
        LSL_LSR_ASR,
        MOV_CMP_ADD_SUB,
        data_proc,
        BX_BLX,
        ADD_CMP_MOV_hi,
        LDR_PC_relative,
        LDRH_STRH_reg_offset,
        LDRSH_LDRSB_reg_offset,
        LDR_STR_reg_offset,
        LDRB_STRB_reg_offset,
        LDR_STR_imm_offset,
        LDRB_STRB_imm_offset,
        LDRH_STRH_imm_offset,
        LDR_STR_SP_relative,
        ADD_SP_or_PC,
        ADD_SUB_SP,
        PUSH_POP,
        LDM_STM,
        SWI,
        UNDEFINED_BCC,
        BCC,
        B,
        BL_BLX_prefix,
        BL_suffix,
        BKPT,
        BLX_suffix,
        INVALID
    };
};


template<armtype cpukind, typename scheduler_kind>
struct core {
    explicit core(scheduler_kind *scheduler_in, u64 *master_clock_in, u64 *waitstates_in);

    scheduler_kind *scheduler;
    u32 sch_irq_sch{};
    template<bool cached> void reset();
    void direct_boot();
    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);
    void setup_tracing(jsm_debug_read_trace *strct, u64 *trace_cycle_pointer, i32 source_id);
    template<bool do_debug> void run_THUMB();
    template<bool do_debug> void run_ARM();
    template<bool do_debug, bool do_IRQ_check=false, bool IRQ_check_param=false, bool check_halt_bool=false> void cached_run(i32 num_cycles);
    void set_current_cached_ins_ends_block();
    typedef cached_block<cpukind, scheduler_kind> cached_block_t;
    template<bool do_debug> cached_block_t *get_next_block();
    template<bool do_debug, bool is_THUMB> void compile_cached_block_into(u32 fetch_addr, cached_block_t *bl);
    template<bool do_debug, bool is_THUMB, bool do_IRQ_check, bool IRQ_check_param, bool check_halt_bool> void execute_cached_block(cached_block_t *bl);
    bool halted{};
    bool cached_mode{true};
    void enter_cached_mode();
    void exit_cached_mode();
    u32 cached_max_block_size{};
    i64 cached_cycles_left{};

    template<bool do_debug>
    void run_instruction() {
        if constexpr (cpukind == AT_ARM7DI) {
            // ARM7DI no THUMB
            run_ARM<do_debug>();
        }
        else {
            if (regs.CPSR.T)
                run_THUMB<do_debug>();
            else
                run_ARM<do_debug>();
        }
    }

    void arm_trace_format(u32 opcode, u32 addr, bool T, bool taken);

    REGS regs{};
    NDS_CP15 nds_cp15;
    void *idle_ptr{};
    void (*idle_func)(void *, u32 num);
    void idle(u32 num) { *waitstates += num; if (idle_func) idle_func(idle_ptr, num); }
    template<u8 sz, bool do_debug> u32 read(u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool cached> void write(u32 addr, u8 access, u32 val);

    template<u8 sz, bool do_debug>
    u32 fetch_ins();
    u32 *get_SPSR_by_mode();

    u32 *old_getR(u32 num);

    template<bool do_debug, bool cached, bool is_FIQ> void do_interrupt();
    [[nodiscard]] bool condition_passes(condition_codes which) const;

    template<bool do_debug, bool cached> void reload_pipeline();
    template<bool do_debug, bool cached> void reload_pipeline_ARM();
    template<bool do_debug, bool cached> void reload_pipeline_THUMB();
    void print_context(ARMctxt *ct, jsm_string *out, bool taken) const;

    void *fetch_ptr{}, *read_ptr{}, *write_ptr{}, *ins_timing_ptr{};

    void cached_block_destruct(void *ptr);
    void *cached_block_ptr{};
    template<bool do_debug> void *fetch_cached_block(u32 addr);
    void *(*get_cached_block)(void *ptr, u32 addr);
    void *(*get_cached_block_debug)(void *ptr, u32 addr);
    template<bool do_debug> void *get_cached_block_itcm(u32 addr);
    void (*register_cached_block)(void *ptr, void *block_ptr);
    void (*register_cached_block_debug)(void *ptr, void *block_ptr);
    u32 (*fetch_ins_func16_peek)(void *ptr, u32 addr, u8 access){};
    u32 (*fetch_ins_func32_peek)(void *ptr, u32 addr, u8 access){};
    u32 (*fetch_ins_func16_peek_debug)(void *ptr, u32 addr, u8 access){};
    u32 (*fetch_ins_func32_peek_debug)(void *ptr, u32 addr, u8 access){};

    u32 (*ins_timing16)(void *ptr, u32 addr, u8 access){};
    u32 (*ins_timing32)(void *ptr, u32 addr, u8 access){};
    u32 (*fetch_ins_func16)(void *ptr, u32 addr, u8 access){};
    u32 (*fetch_ins_func32)(void *ptr, u32 addr, u8 access){};
    u32 (*read_func8)(void *ptr, u32 addxr, u8 access){};
    u32 (*read_func16)(void *ptr, u32 addxr, u8 access){};
    u32 (*read_func32)(void *ptr, u32 addxr, u8 access){};
    void (*write_func8)(void *ptr, u32 addr, u8 access, u32 val){};
    void (*write_func16)(void *ptr, u32 addr, u8 access, u32 val){};
    void (*write_func32)(void *ptr, u32 addr, u8 access, u32 val){};
    u32 (*fetch_ins_func16_debug)(void *ptr, u32 addr, u8 access){};
    u32 (*fetch_ins_func32_debug)(void *ptr, u32 addr, u8 access){};
    u32 (*read_func8_debug)(void *ptr, u32 addxr, u8 access){};
    u32 (*read_func16_debug)(void *ptr, u32 addxr, u8 access){};
    u32 (*read_func32_debug)(void *ptr, u32 addxr, u8 access){};
    void (*write_func8_debug)(void *ptr, u32 addr, u8 access, u32 val){};
    void (*write_func16_debug)(void *ptr, u32 addr, u8 access, u32 val){};
    void (*write_func32_debug)(void *ptr, u32 addr, u8 access, u32 val){};
    CP_read_func CP_read{};
    CP_write_func CP_write{};
    void *CP_ptr{};

    template<bool do_debug, bool cached> static void sch_check_irq(void *ptr, u64 key, u64 timecode, u32 jitter);
    template<bool cached> void schedule_IRQ_check();
    template<bool do_debug, bool cached, bool do_sched> bool IRQcheck();
    arm32_cached_ins<cpukind, scheduler_t> *current_ins{};

    u64 *waitstates{};
    u64 *master_clock{};
    arm32_ins<cpukind, scheduler_kind> *arm_ins{};
    u32 *regmap[16]{};
    u32 temp_carry{}; // temp for instructions

    arm32_ins<cpukind, scheduler_kind> opcode_table_arm[4096]{};
    arm32_ins<cpukind, scheduler_kind> opcode_table_arm_debug[4096]{};
    arm32_ins<cpukind, scheduler_kind> opcode_table_arm_never[4096]{};
    arm32_ins<cpukind, scheduler_kind> opcode_table_arm_never_debug[4096]{};
    arm32_cached_ins<cpukind, scheduler_kind> opcode_table_thumb[65536]{};
    struct {
        u32 opcode[2]{};
        u32 addr[2]{};
        u32 access{};
        //bool flushed{};
    } pipeline{};

    struct {
        jsm_debug_read_trace strct{};
        jsm_string str{100};
        jsm_string str2{100};
        bool ok{};
        u64 *cycles{};
        u32 ins_PC{};
        i32 source_id{};
        u32 exception_id{};
    } trace{};
    void fill_regmap();

    block_cache_better<32 * 1024, 256, 2, 320,  cached_block_t> arm9_block_cache{};
    template<u8 sz, bool do_debug> u32 cached_compile_get_ins(u32 addr);

    [[nodiscard]] bool addr_in_itcm(u32 addr)
    {
        return ((addr >= nds_cp15.itcm.base_addr) && (addr < nds_cp15.itcm.end_addr));
    }

    template<u8 sz, bool peek> [[nodiscard]] u32 read_itcm(u32 addr)
    {
        if constexpr (!peek) (*waitstates)++;
        u32 tcm_addr = (addr - nds_cp15.itcm.base_addr) & nds_cp15.itcm.mask;
        if constexpr(sz == 1) return nds_cp15.itcm.data[tcm_addr];
        if constexpr(sz == 2) return reinterpret_cast<u16 *>(nds_cp15.itcm.data)[tcm_addr >> 1];
        if constexpr(sz == 4) return reinterpret_cast<u32 *>(nds_cp15.itcm.data)[tcm_addr >> 2];
        NOGOHERE;
    }

    [[nodiscard]] bool addr_in_dtcm(const u32 addr) const
    {
        return ((addr >= nds_cp15.dtcm.base_addr) && ((addr < nds_cp15.dtcm.end_addr)));
    }

    template<u8 sz>
    void write_dtcm(const u32 addr, const u32 v)
    {
        (*waitstates)++;
        const u32 tcm_addr = (addr - nds_cp15.dtcm.base_addr) & (nds_cp15.dtcm.size - 1);
        if constexpr(sz == 1) nds_cp15.dtcm.data[tcm_addr] = v;
        else if constexpr(sz == 2) reinterpret_cast<u16 *>(nds_cp15.dtcm.data)[tcm_addr >> 1] = v;
        else if constexpr(sz == 4) reinterpret_cast<u32 *>(nds_cp15.dtcm.data)[tcm_addr >> 2] = v;
    }

    template<u8 sz, bool cached> void write_itcm(const u32 addr, const u32 v)
    {
        (*waitstates)++;
        u32 tcm_addr = (addr - nds_cp15.itcm.base_addr) & nds_cp15.itcm.mask;
        if constexpr(cached) arm9_block_cache.mark_dirty(tcm_addr);
        if constexpr(sz == 1) nds_cp15.itcm.data[tcm_addr] = v;
        else if constexpr(sz == 2) reinterpret_cast<u16 *>(nds_cp15.itcm.data)[tcm_addr >> 1] = v;
        else if constexpr(sz == 4) reinterpret_cast<u32 *>(nds_cp15.itcm.data)[tcm_addr >> 2] = v;
        else NOGOHERE;
    }

    template<u8 sz> u32 read_dtcm(const u32 addr)
    {
        (*waitstates)++;
        const u32 tcm_addr = (addr - nds_cp15.dtcm.base_addr) & (NDS_DTCM_SIZE - 1);
        if constexpr(sz == 1) return nds_cp15.dtcm.data[tcm_addr];
        if constexpr(sz == 2) return reinterpret_cast<u16 *>(nds_cp15.dtcm.data)[tcm_addr >> 1];
        if constexpr(sz == 4) return reinterpret_cast<u32 *>(nds_cp15.dtcm.data)[tcm_addr >> 2];
        NOGOHERE;
    }

    template<bool do_block_end, bool for_cached> void cached_decode_thumb32(u16 opc, arm32_cached_ins<cpukind, scheduler_kind> &ins);
    template<bool do_block_end> void cached_decode_arm32(u32 opc, arm32_cached_ins<cpukind, scheduler_kind> &ins);

    u32 TEST(u32 v, u32 S);
    u32 ADD(u32 Rnd, u32 Rmd, u32 carry, u32 S);
    u32 SUB(u32 Rn, u32 Rm, u32 carry, u32 S);
    u32 ALU(u32 Rn, u32 Rm, u32 alu_opcode, u32 S, u32 *out);

    u32 LSL(u32 v, u32 amount);
    u32 LSR(u32 v, u32 amount);
    u32 ASR(u32 v, u32 amount);
    u32 ROR(u32 v, u32 amount);
    u32 RRX(u32 v);
    u32 tLSL(u32 v, u32 amount, bool set_flags);
    u32 tLSR(u32 v, u32 amount, bool set_flags);
    u32 tASR(u32 v, u32 amount, bool set_flags);
    u32 tADD(u32 op1, u32 op2);
    u32 tSUB(u32 op1, u32 op2, bool set_flags);
    u32 tROR(u32 v, u32 amount);
    u32 MUL(u32 product, u32 multiplicand, u32 multiplier, u32 S);
#define ARMi(name) template<bool do_debug> void ARM_ins_##name(u32 opcode)
    ARMi(MUL_MLA);
    ARMi(MULL_MLAL);
    ARMi(SWP);
    ARMi(LDRH_STRH);
    ARMi(LDRSB_LDRSH);
    ARMi(MRS);
    ARMi(MSR_reg);
    ARMi(MSR_imm);
    ARMi(BX);
    ARMi(data_proc_immediate_shift);
    ARMi(data_proc_register_shift);
    ARMi(undefined_instruction);
    ARMi(data_proc_immediate);
    ARMi(LDR_STR_immediate_offset);
    ARMi(LDR_STR_register_offset);
    ARMi(LDM_STM_ARM7TDMI);
    ARMi(LDM_STM_ARM946ES);
    ARMi(STC_LDC);
    ARMi(CDP);
    ARMi(MCR_MRC);
    ARMi(SWI);
    ARMi(INVALID);
    ARMi(PLD);
    ARMi(SMLAxy);
    ARMi(SMLAWy);
    ARMi(SMULWy);
    ARMi(SMLALxy);
    ARMi(SMULxy);
    ARMi(LDRD_STRD);
    ARMi(CLZ);
    ARMi(BLX_reg);
    ARMi(QADD_QSUB_QDADD_QDSUB);
    ARMi(BKPT);
    ARMi(B_BL);
    ARMi(BLX);
#undef ARMi
#define THUMBi(name) bool THUMB_ins_##name(const arm32_cached_ins<cpukind, scheduler_kind> &ins)
    template<bool do_debug, bool cached> THUMBi(INVALID);
    template<bool do_debug, bool cached, bool I, bool sub_opcode> THUMBi(ADD_SUB);
    template<bool do_debug, bool cached, u8 sub_opcode> THUMBi(LSL_LSR_ASR);
    template<bool do_debug, bool cached, u8 sub_opcode> THUMBi(MOV_CMP_ADD_SUB);
    template<bool do_debug, bool cached, u8 sub_opcode> THUMBi(data_proc);
    template<bool do_debug, bool cached, bool sub_opcode> THUMBi(BX_BLX);
    template<bool do_debug, bool cached, u8 sub_opcode> THUMBi(ADD_CMP_MOV_hi);
    template<bool do_debug, bool cached> THUMBi(LDR_PC_relative);
    template<bool do_debug, bool cached, bool L> THUMBi(LDRH_STRH_reg_offset);
    template<bool do_debug, bool cached, bool B> THUMBi(LDRSH_LDRSB_reg_offset);
    template<bool do_debug, bool cached, bool L> THUMBi(LDR_STR_reg_offset);
    template<bool do_debug, bool cached, bool L> THUMBi(LDRB_STRB_reg_offset);
    template<bool do_debug, bool cached, bool L> THUMBi(LDR_STR_imm_offset);
    template<bool do_debug, bool cached, bool L> THUMBi(LDRB_STRB_imm_offset);
    template<bool do_debug, bool cached, bool L> THUMBi(LDRH_STRH_imm_offset);
    template<bool do_debug, bool cached, bool L> THUMBi(LDR_STR_SP_relative);
    template<bool do_debug, bool cached, bool SP> THUMBi(ADD_SP_or_PC);
    template<bool do_debug, bool cached, bool sub_opcode> THUMBi(ADD_SUB_SP);
    template<bool do_debug, bool cached, bool PC_LR, bool pop, bool rlist_empty> THUMBi(PUSH_POP);
    template<bool do_debug, bool cached, bool L, bool rlist_empty> THUMBi(LDM_STM);
    template<bool do_debug, bool cached> THUMBi(SWI);
    template<bool do_debug, bool cached> THUMBi(UNDEFINED_BCC);
    template<bool do_debug, bool cached> THUMBi(BCC);
    template<bool do_debug, bool cached> THUMBi(B);
    template<bool do_debug, bool cached> THUMBi(BL_BLX_prefix);
    template<bool do_debug, bool cached> THUMBi(BL_suffix);
    template<bool do_debug, bool cached> THUMBi(BKPT);
    template<bool do_debug, bool cached> THUMBi(BLX_suffix);
#undef THUMBi

#define ARMi(name) bool ARM_cached_ins_##name(const arm32_cached_ins<cpukind, scheduler_kind> &ins)
    template<bool do_debug, bool accumulate, bool S, bool Rd_is_15> ARMi(MUL_MLA);
    template<bool do_debug, bool accumulate, bool S, bool Rn_or_Rd_is_15, bool sign> ARMi(MULL_MLAL);
    template<bool do_debug, bool B, bool Rdd_is_15> ARMi(SWP);
    template<bool do_debug, bool P, bool U, bool I, bool L, bool W> ARMi(LDRH_STRH);
    template<bool do_debug, bool P, bool U, bool I, bool W, bool H> ARMi(LDRSB_LDRSH);
    template<bool do_debug, bool PSR> ARMi(MRS);
    template <bool do_debug, bool PSR, bool do_idle> ARMi(MSR_reg);
    template<bool do_debug, bool PSR, bool do_idle> ARMi(MSR_imm);
    template<bool do_debug> ARMi(BX);
    template<bool do_debug, bool S, bool Rdd_is_15> ARMi(data_proc_immediate_shift);
    template<bool do_debug, bool S, bool Rdd_is_15> ARMi(data_proc_register_shift);
    template<bool do_debug> ARMi(undefined_instruction);
    template<bool do_debug, bool S, bool Rdd_is_15, bool carryout> ARMi(data_proc_immediate);
    template<bool do_debug, bool P, bool U, bool B, bool W, bool L> ARMi(LDR_STR_immediate_offset);
    template<bool do_debug, bool P, bool U, bool B, bool W, bool L> ARMi(LDR_STR_register_offset);
    template<bool do_debug, bool P, bool U, bool S, bool W, bool L, bool move_pc, bool do_mode_switch> ARMi(LDM_STM_ARM7TDMI);
    template<bool do_debug, bool U> ARMi(LDM_STM_ARM946ES_ABORT);
    template<bool do_debug, bool P, bool U, bool S, bool W, bool L, bool move_pc, bool do_mode_switch, bool Rnd_is_last> ARMi(LDM_STM_ARM946ES);
    template<bool do_debug> ARMi(STC_LDC);
    template<bool do_debug> ARMi(CDP);
    template<bool do_debug, bool copro_to_arm> ARMi(MCR_MRC);
    template<bool do_debug> ARMi(BKPT);
    template<bool do_debug> ARMi(SWI);
    template<bool do_debug> ARMi(INVALID);
    template<bool do_debug> ARMi(PLD);
    template<bool do_debug, bool x, bool y> ARMi(SMLAxy);
    template<bool do_debug, bool y> ARMi(SMLAWy);
    template<bool do_debug, bool y, bool Rdd_is_15> ARMi(SMULWy);
    template<bool do_debug, bool x, bool y, bool Rdd_is_15> ARMi(SMLALxy);
    template<bool do_debug, bool x, bool y, bool Rdd_is_15>ARMi(SMULxy);
    template<bool do_debug, bool P, bool U, bool I, bool L, bool W> ARMi(LDRD_STRD);
    template<bool do_debug, bool Rdd_is_15> ARMi(CLZ);
    template<bool do_debug> ARMi(BLX_reg);
    template<bool do_debug, bool subtract, bool double_op2> ARMi(QADD_QSUB_QDADD_QDSUB);
    template<bool do_debug, bool link> ARMi(B_BL);
    template<bool do_debug> ARMi(BLX);
#undef ARMi
#define ARMi(name) template<bool do_block_end> void decode_cached_ARM_##name(u32 opcode, arm32_cached_ins<cpukind, scheduler_kind> &out)
    ARMi(MUL_MLA);
    ARMi(MULL_MLAL);
    ARMi(SWP);
    ARMi(LDRH_STRH);
    ARMi(LDRSB_LDRSH);
    ARMi(MRS);
    ARMi(MSR_reg);
    ARMi(MSR_imm);
    ARMi(BX);
    ARMi(data_proc_immediate_shift);
    ARMi(data_proc_register_shift);
    ARMi(undefined_instruction);
    ARMi(data_proc_immediate);
    ARMi(LDR_STR_immediate_offset);
    ARMi(LDR_STR_register_offset);
    ARMi(LDM_STM_ARM7TDMI);
    ARMi(LDM_STM_ARM946ES);
    ARMi(STC_LDC);
    ARMi(CDP);
    ARMi(MCR_MRC);
    ARMi(SWI);
    ARMi(INVALID);
    ARMi(PLD);
    ARMi(SMLAxy);
    ARMi(SMLAWy);
    ARMi(SMULWy);
    ARMi(SMLALxy);
    ARMi(SMULxy);
    ARMi(LDRD_STRD);
    ARMi(CLZ);
    ARMi(BLX_reg);
    ARMi(QADD_QSUB_QDADD_QDSUB);
    ARMi(BKPT);
    ARMi(B_BL);
    ARMi(BLX);
#undef ARMi

    template<bool do_debug, bool cached> void undefined_exception();
    void fill_ARM_table();

    template<bool do_debug>
    void decode_and_exec_ARM(u32 opcode, u32 opcode_addr);

    template<bool do_debug>
    void decode_and_exec_THUMB(u32 opcode, u32 opcode_addr);

    DBG_START
        DBG_EVENT_VIEW
        DBG_TRACE_VIEW
        DBG_LOG_VIEW_SIMPLE
    DBG_END

};


template<armtype cpukind, typename scheduler_kind>
void core<cpukind, scheduler_kind>::serialize(serialized_state &state)
{
#define S(x) Sadd(state, &( x), sizeof( x))
    S(halted);
    S(cached_mode);
    S(cached_max_block_size);
    S(cached_cycles_left);

    S(regs.R);
    S(regs.R_fiq);
    S(regs.R_svc);
    S(regs.R_abt);
    S(regs.R_irq);
    S(regs.R_und);
    S(regs.R_invalid);
    S(regs.SPSR_fiq);
    S(regs.SPSR_svc);
    S(regs.SPSR_abt);
    S(regs.SPSR_irq);
    S(regs.SPSR_und);
    S(regs.SPSR_invalid);
    S(regs.CPSR.u);
    S(regs.SPSR.u);
    S(regs.IRQ_line);
    S(regs.FIQ_line);
    S(regs.EBR);

    if constexpr(cpukind >= AT_ARM946ES) {
        nds_cp15.serialize(state);
    }

    S(pipeline.opcode);
    S(pipeline.addr);
    S(pipeline.access);
    S(temp_carry);
    S(trace.ins_PC);
#undef S
}

template<armtype cpukind, typename scheduler_kind>
void core<cpukind, scheduler_kind>::deserialize(serialized_state &state)
{
#define L(x) Sload(state, &( x), sizeof( x))
    L(halted);
    L(cached_mode);
    L(cached_max_block_size);
    L(cached_cycles_left);

    L(regs.R);
    L(regs.R_fiq);
    L(regs.R_svc);
    L(regs.R_abt);
    L(regs.R_irq);
    L(regs.R_und);
    L(regs.R_invalid);
    L(regs.SPSR_fiq);
    L(regs.SPSR_svc);
    L(regs.SPSR_abt);
    L(regs.SPSR_irq);
    L(regs.SPSR_und);
    L(regs.SPSR_invalid);
    L(regs.CPSR.u);
    L(regs.SPSR.u);
    L(regs.IRQ_line);
    L(regs.FIQ_line);
    L(regs.EBR);

    if constexpr(cpukind >= AT_ARM946ES) {
        nds_cp15.deserialize(state);
        arm9_block_cache.reset();
    }

    L(pipeline.opcode);
    L(pipeline.addr);
    L(pipeline.access);
    L(temp_carry);
    L(trace.ins_PC);
#undef L

    sch_irq_sch = 0;
    arm_ins = nullptr;
    current_ins = nullptr;
    fill_regmap();
}


static constexpr u32 masksz[5] = { 0, 0xFF, 0xFFFF, 0, 0xFFFFFFFF };
static constexpr u32 maskalign[5] = {0, 0xFFFFFFFF, 0xFFFFFFFE, 0, 0xFFFFFFFC};

#ifndef ARM32_NOIMPL
#include "decode.h"
#include "exec.h"
#endif
#undef OBIT
#define OBIT(x) ((opcode >> (x)) & 1)
static u32 align_val(const u32 addr, u32 tmp)
{
    if (addr & 3) {
        const u32 misalignment = addr & 3;
        if (misalignment == 1) tmp = ((tmp >> 8) & 0xFFFFFF) | (tmp << 24);
        else if (misalignment == 2) tmp = (tmp >> 16) | (tmp << 16);
        else tmp = ((tmp << 8) & 0xFFFFFF00) | (tmp >> 24);
    }
    return tmp;
}

TIK::ins_kind decode_thumb32(u16 opc);
template<armtype cpukind> AIK::ins_kind decode_arm32(u32 opc);
AIK::ins_kind decode_arm32_never(u32 opc);

#ifndef ARM32_NOIMPL
#include "arm_instructions.h"
#include "thumb_instructions.h"
#include "arm_cached_instructions.h"
#include "arm_cached_decode.h"
#include "arm_cached_instructions.h"
#include "cached.h"
#endif

#undef OBIT
#undef PC
#undef getR

}
