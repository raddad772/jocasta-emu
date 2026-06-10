//
// Created by . on 2/11/25.
//

#pragma once

#include "helpers/debugger/debuggerdefs.h"
#include "helpers/debugger/debugger.h"
#include "helpers/better_irq_multiplexer.h"
#include "helpers/int.h"
#include "helpers/debug.h"
#include "r3000_multiplier.h"
#include "helpers/scheduler.h"
#include "gte.h"
namespace R3000 {
static constexpr bool DIVMUL_INSTANT = true;
struct REGS {
    u32 R[32]{};
    u32 COP0[32]{};
    u32 PC{};
    u32 PC_next{};
    u32 IR{};
};

    union CAUSE {
        struct {
            u32 _ : 2;
            u32 exception_code : 5;
            u32 __ : 1;
            u32 IP : 8;
            u32 ___ : 12;
            u32 CE : 2;
            u32 BT : 1;
            u32 BD : 1;
        };
        u32 u{};
    };

struct OPCODE;
struct core;
typedef void (core::*insfunc)(u32 opcode, OPCODE *op);

struct OPCODE {
    u32 opcode{};
    insfunc func{};
    insfunc func_debug{};
    i32 arg{};
};

struct ICACHE {
    struct LINE {
        u32 tag{};
        u8 valid{};
        u32 data[4]{};
    } line[256]{};
    bool enabled{};
    u32 refill_words{4};

    void reset() {
        for (LINE &l : line) { l.tag = 0; l.valid = 0; }
        enabled = false;
        refill_words = 4;
    }
};

struct BIU {
    u32 com_delay{0x0003'1125};
    u32 bios_delay{0x0013'243F};
    u32 spu_delay{0x2009'31E1};
    u32 cdrom_delay{0x0002'0843};
    u32 exp1_delay{0x0013'243F};
    u32 exp2_delay{0x0007'0777};
    u32 exp3_delay{0x0000'3022};

    void reset() { *this = BIU{}; }
};


    struct ctxt;
struct core {
    core(u64 *master_clock_in, u64 *waitstates_in, scheduler_t *scheduler_in, IRQ_multiplexer_b *IRQ_multiplexer_in);
    void setup_tracing(jsm_debug_read_trace &strct, u64 *trace_cycle_pointer, i32 source_id);
    void add_to_console(u32 ch);
    void exception(u32 code, u32 cop0);
    template<bool do_debug> void after_ins();
    void decode(u32 IR);
    template<bool do_debug> bool fetch_and_decode();
    void print_context(ctxt &ct, jsm_string &out);
    void trace_format_console(jsm_string &out);
    void trace_format();
    void dbglog_exception(u32 code, u32 vector, u32 raddr);
    template<bool do_debug> void check_IRQ();
    template<bool do_debug> void cycle(i32 howmany);
    template<bool do_debug> void instruction();
    void reset();
    void update_I_STAT();
    void write_reg(u32 addr, u8 sz, u32 val);
    u32 read_reg(u32 addr, u8 sz);
    [[nodiscard]] u32 peek_next_instruction() const;
    void idle(u32 howlong);
    void set_cache_control(u32 val);
    void icache_isolated_write(u32 addr);
    void set_mem_delay(u32 addr, u32 val);
    [[nodiscard]] u32 biu_access_cycles(u32 delay_reg, u8 sz, bool is_write) const;
    void recompute_biu_cycles();
    [[nodiscard]] u32 uncached_fetch_cycles(u32 addr) const;
    [[nodiscard]] u32 data_access_cycles(u32 addr, u8 sz, bool is_write) const;

    REGS regs{};
    MULTIPLIER multiplier{};
    jsm_string console{4096};

    struct {
        struct {
            i32 target{-1};
            u32 value{0};
        } load[2]{};

        struct {
            bool slot{false};
            bool taken{false};
            u32 target{};
        } branch[2]{};
    } delay{};

    u64 *clock;
    scheduler_t *scheduler{};
    u64 *waitstates{};

    struct {
        u32 IRQ{};
    } pins{};

    struct {
        jsm_debug_read_trace strct{};
        jsm_string str{100};
        jsm_string str2{100};
        bool ok{};
        u32 ins_PC{};
        i32 source_id{};
        u32 irq_id{};
        u32 rfe_id{};
        u32 exception_id{};
        u32 I_STAT_write{};
        u32 I_MASK_write{};
        u32 console_log_id{};
    } trace{};

    struct {
        IRQ_multiplexer_b *I_STAT{};
        u32 I_MASK{};
    } io{};

    GTE::core gte{};

    ICACHE icache{};
    BIU biu{};

    enum biu_region { BIU_R_BIOS, BIU_R_SPU, BIU_R_CDROM, BIU_R_EXP1, BIU_R_EXP2, BIU_R_EXP3, BIU_R_COUNT };
    u32 biu_cycles_cache[BIU_R_COUNT][2][3]{};

    OPCODE *cur_ins{};

    OPCODE decode_table[0x7F]{};

    void *fetch_ins_ptr{}, *read_ptr{}, *write_ptr{}, *peek_ins_ptr{}, *update_sr_ptr{}, *khook_ptr{};
    u32 (*fetch_ins)(void *ptr, u32 addr){};
    u32 (*fetch_ins_debug)(void *ptr, u32 addr){};
    u32 (*peek_ins)(void *ptr, u32 addr);
    u32 (*read)(void *ptr, u32 addr, u8 sz){};
    u32 (*read_debug)(void *ptr, u32 addr, u8 sz){};
    void (*khook)(void *ptr);
    void (*write)(void *ptr, u32 addr, u8 sz, u32 val){};
    void (*write_debug)(void *ptr, u32 addr, u8 sz, u32 val){};
    u32 (*read24_unaligned)(void *ptr, u32 addr){};
    u32 (*read24_unaligned_debug)(void *ptr, u32 addr){};
    void (*write24_unaligned)(void *ptr, u32 addr, u32 val){};
    void (*write24_unaligned_debug)(void *ptr, u32 addr, u32 val){};
    void (*update_sr)(void *ptr, core *mcore, u32 val){};
    DBG_START
    console_view *console{};
    DBG_LOG_VIEW_SIMPLE
    DBG_END

private:
    void wait_for(u64 timecode);
    void do_decode_table();
    template<bool do_debug> void fNA(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSLL(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSRL(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSRA(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSLLV(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSRLV(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSRAV(u32 opcode, OPCODE *op);
    template<bool do_debug> void fJR(u32 opcode, OPCODE *op);
    template<bool do_debug> void fJALR(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSYSCALL(u32 opcode, OPCODE *op);
    template<bool do_debug> void fBREAK(u32 opcode, OPCODE *op);
    template<bool do_debug> void fMFHI(u32 opcode, OPCODE *op);
    template<bool do_debug> void fMFLO(u32 opcode, OPCODE *op);
    template<bool do_debug> void fMTHI(u32 opcode, OPCODE *op);
    template<bool do_debug> void fMTLO(u32 opcode, OPCODE *op);
    template<bool do_debug> void fMULT(u32 opcode, OPCODE *op);
    template<bool do_debug> void fMULTU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fDIV(u32 opcode, OPCODE *op);
    template<bool do_debug> void fDIVU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fADD(u32 opcode, OPCODE *op);
    template<bool do_debug> void fADDU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSUB(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSUBU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fAND(u32 opcode, OPCODE *op);
    template<bool do_debug> void fOR(u32 opcode, OPCODE *op);
    template<bool do_debug> void fXOR(u32 opcode, OPCODE *op);
    template<bool do_debug> void fNOR(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSLT(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSLTU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fBcondZ(u32 opcode, OPCODE *op);
    template<bool do_debug> void fJ(u32 opcode, OPCODE *op);
    template<bool do_debug> void fJAL(u32 opcode, OPCODE *op);
    template<bool do_debug> void fBEQ(u32 opcode, OPCODE *op);
    template<bool do_debug> void fBNE(u32 opcode, OPCODE *op);
    template<bool do_debug> void fBLEZ(u32 opcode, OPCODE *op);
    template<bool do_debug> void fBGTZ(u32 opcode, OPCODE *op);
    template<bool do_debug> void fADDI(u32 opcode, OPCODE *op);
    template<bool do_debug> void fADDIU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSLTI(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSLTIU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fANDI(u32 opcode, OPCODE *op);
    template<bool do_debug> void fORI(u32 opcode, OPCODE *op);
    template<bool do_debug> void fXORI(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLUI(u32 opcode, OPCODE *op);
    template<bool do_debug> void fCOP(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLB(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLH(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLWL(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLW(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLBU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLHU(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLWR(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSB(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSH(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSWL(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSW(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSWR(u32 opcode, OPCODE *op);
    template<bool do_debug> void fLWC(u32 opcode, OPCODE *op);
    template<bool do_debug> void fSWC(u32 opcode, OPCODE *op);
    void fMFC(u32 opcode, OPCODE *op, u32 copnum);
    void fMTC(u32 opcode, OPCODE *op, u32 copnum);
    template<bool do_debug> void fCOP0_RFE(u32 opcode, OPCODE *op);

    void COP_write_reg(u32 COP, u32 num, u32 val);
    u32 COP_read_reg(u32 COP, u32 num);
    void branch(i64 rel, bool doit, bool link, u32 link_reg);
    void jump(u32 new_addr, bool link, u32 link_reg);
    u32 fs_reg_delay_read(i32 target);
    u64 current_clock() { return *clock + *waitstates; }

    template<bool do_debug> u32 bus_read(u32 addr, u8 sz) {
        *waitstates += data_access_cycles(addr, sz, false);
        if constexpr (do_debug) return read_debug(read_ptr, addr, sz);
        else return read(read_ptr, addr, sz);
    }

    template<bool do_debug> void bus_write(u32 addr, u8 sz, u32 val) {
        *waitstates += data_access_cycles(addr, sz, true);
        if constexpr (do_debug) write_debug(write_ptr, addr, sz, val);
        else write(write_ptr, addr, sz, val);
    }

    template<bool do_debug> u32 bus_read24_unaligned(u32 addr) {
        if (addr & 1) *waitstates += data_access_cycles(addr, 1, false) + data_access_cycles(addr + 1, 2, false);
        else *waitstates += data_access_cycles(addr, 2, false) + data_access_cycles(addr + 2, 1, false);
        if constexpr (do_debug) return read24_unaligned_debug(read_ptr, addr);
        else return read24_unaligned(read_ptr, addr);
    }

    template<bool do_debug> void bus_write24_unaligned(u32 addr, u32 val) {
        if (addr & 1) *waitstates += data_access_cycles(addr, 1, true) + data_access_cycles(addr + 1, 2, true);
        else *waitstates += data_access_cycles(addr, 2, true) + data_access_cycles(addr + 2, 1, true);
        if constexpr (do_debug) write24_unaligned_debug(write_ptr, addr, val);
        else write24_unaligned(write_ptr, addr, val);
    }

    template<bool do_debug> u32 raw_fetch(u32 addr) {
        if constexpr (do_debug) return fetch_ins_debug(fetch_ins_ptr, addr);
        else return fetch_ins(fetch_ins_ptr, addr);
    }

    template<bool do_debug> u32 fetch(u32 addr);

    void fs_reg_delay(const i32 target, const u32 value)
    {
        delay.load[1].target = target;
        delay.load[1].value = value;

        if (delay.load[0].target == target) { delay.load[0] = {}; }
    }

    void fs_reg_write(i32 target, u32 value)
    {
        // cancel in-pipeline write to register
        if (delay.load[0].target == target) delay.load[0] = {};

        regs.R[target] = value;
    }

};
}
