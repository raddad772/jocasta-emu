//
// Created by . on 11/18/25.
//

#include <cassert>

#include "cdp1802.h"
#include "cdp1802_instructions.h"
#include "cdp1802_disassembler.h"

namespace CDP1802 {

void core::reset() {
    regs.D = 0;
    regs.I = regs.N = pins.Q = 0;
    regs.IE = 1;
    pins.D = 0;
    regs.X = 0;
    regs.P = 0;
    regs.R[0].u = 0;
    prepare_fetch();
}

void core::dma_in() {
    pins.MRD = 0;
    pins.MWR = 1;
    pins.Addr = regs.R[0].u;
    regs.R[0].u++;
    pins.SC = PINS::S2_dma;
    trace_format();
}

void core::dma_out() {
    pins.MRD = 1;
    pins.MWR = 0;
    pins.Addr = regs.R[0].u;
    regs.R[0].u++;
    pins.SC = PINS::S2_dma;
    trace_format();
}

void core::dma_end() {
    prepare_fetch();
}

void core::interrupt_end() {
    prepare_fetch();
}

void core::interrupt() {
    regs.T.hi = regs.X;
    regs.T.lo = regs.P;
    regs.IE = 0;
    regs.P = 1;
    regs.X = 2;
    pins.SC = PINS::S3_interrupt;
    trace_format();
}

bool core::perform_interrupts() {
    // Sample DMA-IN, DMA-OUT, and IRQ vs. IE to determine fi any should be done, and do them, and return true.
    // False if no interrupt
    // priority DMA-IN, DMA-OUT, IRQ
    if (pins.DMA_IN) {
        dma_in();
        return true;
    }
    else if (pins.DMA_OUT) {
        dma_out();
        return true;
    }
    else if (pins.INTERRUPT && regs.IE) {
        //dbg_break("IRQ", 0);
        interrupt();
        return true;
    }
    return false;
}

void core::prepare_fetch() {
    if (!perform_interrupts()) { // Only proceed if no interrupt pending
        pins.SC = PINS::S0_fetch;
        pins.Addr = regs.R[regs.P].u++;
        most_recent_fetch = pins.Addr;
        pins.MWR = 0;
        pins.MRD = 1;
    }
}


void core::do_load(u8 addr_ptr) {
    pins.MWR = 0;
    pins.MRD = 1;
    pins.Addr = regs.R[addr_ptr].u;
}

// prepare_fetch: prepare the fetch
// fetch: got it, so decode and prepare for execute

void core::do_out() {
    // M(R(X)) -> BUS
    // R(X) + 1 -> R(X)
    do_load(regs.X);
}

void core::do_store(u8 addr_ptr, u8 val) {
    pins.MWR = 1;
    pins.Addr = regs.R[addr_ptr].u;
    pins.D = val;
}

void core::prepare_execute() {
    pins.SC = PINS::S1_execute;
    pins.MRD = pins.MWR = 0;
    execs_left = 1;
    switch (regs.I) {
        case 0x0: // LDN
            do_load(regs.N);
            ins = regs.N == 0 ? &ins_IDLE : &ins_LDN;
            break;
        case 0x1: // INC n
            ins = &ins_INC;
            break;
        case 0x2: // DEC n
            ins = &ins_DEC;
            break;
        case 0x3: // short branch
            ins = &ins_SHORT_BRANCH;
            do_immediate();
            break;
        case 0x4: // LDA
            ins = &ins_LDA;
            do_load(regs.N);
            break;
        case 0x5: // STR
            ins = &ins_none;
            do_store(regs.N, regs.D);
            break;
        case 0x6:
            if (regs.N == 0) {
                ins = &ins_IRX;
            }
            else {
                if (regs.N < 8) {
                    pins.N = regs.N;
                    do_out();
                    ins = &ins_OUT;
                }
                else if (regs.N == 8) {
                    ins = &ins_IRX;
                    printf("\n WARN 0x68;");
                }
                else {
                    pins.D = 0xFF;
                    pins.N = regs.N & 7;
                    pins.MWR = 1;
                    pins.Addr = regs.R[regs.X].u;
                    ins = &ins_INP;
                }
            }
            break;
        case 0x7:
            prepare_execute_70();
            break;
        case 0x8: // GLO
            ins = &ins_GLO;
            break;
        case 0x9:
            ins = &ins_GHI;
            break;
        case 0xA:
            ins = &ins_PLO;
            break;
        case 0xB:
            ins = &ins_PHI;
            break;
        case 0xC: // long branch/skip
            ins = &ins_LONG_BRANCH;
            execs_left = 2;
            do_immediate();
            break;
        case 0xD: // N->P
            ins = &ins_SEP;
            break;
        case 0xE: // N->X
            ins = &ins_SEX;
            break;
        case 0xF:
            prepare_execute_F0();
            break;
    }
}

void core::prepare_execute_70() {
    switch (0x70 | regs.N) {
        case 0x70:
            ins = &ins_RET;
            do_load(regs.X);
            break;
        case 0x71:
            ins = &ins_DIS;
            do_load(regs.X);
            break;
        case 0x72: // LDXA
            ins = &ins_LDXA;
            do_load(regs.X);
            break;
        case 0x73: // STXD
            do_store(regs.X, regs.D);
            ins = &ins_STXD;
            break;
        case 0x74:
            ins = &ins_ADC;
            do_load(regs.X);
            break;
        case 0x75:
            ins = &ins_SDB;
            do_load(regs.X);
            break;
        case 0x76: // SHRC, RSHR
            ins = &ins_SHRC_RSHR;
            break;
        case 0x77: // SMB
            ins = &ins_SMB;
            do_load(regs.X);
            break;
        case 0x78: // SAV
            ins = &ins_none;
            do_store(regs.X, regs.T.u);
            break;
        case 0x79: // MARK
            regs.T.hi = regs.X;
            regs.T.lo = regs.P;
            do_store(2, regs.T.u);
            ins = &ins_MARK;
            break;
        case 0x7A:
            ins = &ins_REQ;
            break;
        case 0x7B:
            ins = &ins_SEQ;
            break;
        case 0x7C:
            ins = &ins_ADC;
            do_immediate();
            break;
        case 0x7D:
            ins = &ins_SDB;
            do_immediate();
            break;
        case 0x7E: // SHLC, RSHL
            ins = &ins_SHLC_RSHL;
            break;
        case 0x7F: // SMBI
            ins = &ins_SMB;
            do_immediate();
            break;
    }
}

void core::do_immediate() {
    do_load(regs.P);
    regs.R[regs.P].u++;
}

void core::prepare_execute_F0() {
    switch (0xF0 | regs.N) {
        case 0xF0:
            ins = &ins_LDN;
            do_load(regs.X);
            break;
        case 0xF1:
            ins = &ins_OR;
            do_load(regs.X);
            break;
        case 0xF2:
            ins = &ins_AND;
            do_load(regs.X);
            break;
        case 0xF3:
            ins = &ins_XOR;
            do_load(regs.X);
            break;
        case 0xF4:
            ins = &ins_ADD;
            do_load(regs.X);
            break;
        case 0xF5:
            ins = &ins_SD;
            do_load(regs.X);
            break;
        case 0xF6:
            ins = &ins_SHR;
            break;
        case 0xF7:
            ins = &ins_SM;
            do_load(regs.X);
            break;
        case 0xF8:
            ins = &ins_LDN;
            do_immediate();
            break;
        case 0xF9:
            ins = &ins_OR;
            do_immediate();
            break;
        case 0xFA:
            ins = &ins_AND;
            do_immediate();
            break;
        case 0xFB:
            ins = &ins_XOR;
            do_immediate();
            break;
        case 0xFC:
            ins = &ins_ADD;
            do_immediate();
            break;
        case 0xFD:
            ins = &ins_SD;
            do_immediate();
            break;
        case 0xFE:
            ins = &ins_SHL;
            break;
        case 0xFF:
            ins = &ins_SM;
            do_immediate();
            break;
    }
}

void core::pprint_context(jsm_string &out) {
        out.sprintf("X:%02d  P:%02d  N:%02d  D:%02x  DF:%d  Q:%d  R[X]:%04x  R[P]:%04x  R[N]:%04x ",
            regs.X, regs.P, regs.N,
            regs.D, regs.DF, pins.Q,
            regs.R[regs.X].u, regs.R[regs.P], regs.R[regs.N]);
}


void core::trace_format() {
    if (trace.dbglog.view && trace.dbglog.view->ids_enabled[trace.dbglog.id]) {
        // addr, regs, e, m, x, rt, out
        trace.str.quickempty();
        trace.str2.quickempty();
        u64 tc = *master_clock;
        dbglog_view *dv = trace.dbglog.view;


        if  (pins.SC != PINS::S0_fetch) {
            switch(pins.SC) {
                case PINS::S2_dma:
                    if (pins.DMA_IN) trace.str.sprintf("DMA IN %d", pins.N);
                    else trace.str.sprintf("DMA OUT %d", pins.N);
                    break;
                case PINS::S3_interrupt:
                    trace.str.sprintf("INTERRUPT");
                    break;
                default:
                    assert(1==2);
                    break;
            }
            dv->add_printf(trace.dbglog.id, tc, DBGLS_TRACE, "%s", trace.str.ptr);
            dv->extra_printf("");
            return;
        }
        trace.ins_PC = most_recent_fetch;
        u32 opc = (regs.I << 4) | regs.N;
        ctxt ctx;
        disassemble(trace.ins_PC, trace.strct, trace.str, ctx);
        pprint_context(trace.str2);

        dv->add_printf(trace.dbglog.id, tc, DBGLS_TRACE, "%04x  (%02x) %s", most_recent_fetch, opc, trace.str.ptr);
        dv->extra_printf("%s", trace.str2.ptr);
    }
}

void core::fetch() {
    regs.IR = pins.D;
    regs.I = (regs.IR >> 4) & 15;
    regs.N = regs.IR & 15;
    trace_format();
    prepare_execute();
}

void core::execute() {
    execs_left--;
    ins(this);
}

void core::cycle() {
    if (pins.clear_wait != PINS::RUN) return;
    switch (pins.SC) {
        case PINS::S0_fetch:
            fetch();
            break;
        case PINS::S1_execute:
            execute();
            break;
        case PINS::S2_dma:
            dma_end();
            break;
        case PINS::S3_interrupt:
            interrupt_end();
            break;
    }
}

void core::setup_tracing(jsm_debug_read_trace *strct, u64 *trace_cycle_pointer)
{
    jsm_copy_read_trace(&trace.strct, strct);
    trace.ok = 1;
    trace.cycles = trace_cycle_pointer;
}

// ---- Serialize / Deserialize ----
// ins is a function pointer; encode it as an integer ID for portability.
static const ins_func ins_table[] = {
    nullptr,          // 0 = not set / S0_fetch (irrelevant)
    &ins_LDN,         // 1
    &ins_LDA,         // 2
    &ins_INC,         // 3
    &ins_DEC,         // 4
    &ins_GLO,         // 5
    &ins_GHI,         // 6
    &ins_PLO,         // 7
    &ins_PHI,         // 8
    &ins_SEP,         // 9
    &ins_SEX,         // 10
    &ins_IRX,         // 11
    &ins_OUT,         // 12
    &ins_INP,         // 13
    &ins_LDXA,        // 14
    &ins_SHRC_RSHR,   // 15
    &ins_SHLC_RSHL,   // 16
    &ins_ADC,         // 17
    &ins_SDB,         // 18
    &ins_STXD,        // 19
    &ins_SMB,         // 20
    &ins_MARK,        // 21
    &ins_REQ,         // 22
    &ins_SEQ,         // 23
    &ins_RET,         // 24
    &ins_DIS,         // 25
    &ins_OR,          // 26
    &ins_AND,         // 27
    &ins_XOR,         // 28
    &ins_ADD,         // 29
    &ins_SD,          // 30
    &ins_SHR,         // 31
    &ins_SM,          // 32
    &ins_SHL,         // 33
    &ins_SHORT_BRANCH,// 34
    &ins_LONG_BRANCH, // 35
    &ins_IDLE,        // 36
    &ins_none,        // 37
};
static constexpr u32 ins_table_size = sizeof(ins_table) / sizeof(ins_table[0]);

static u8 ins_to_id(ins_func f) {
    for (u8 i = 0; i < ins_table_size; i++) {
        if (ins_table[i] == f) return i;
    }
    return 0; // fallback to nullptr
}
static ins_func id_to_ins(u8 id) {
    if (id >= ins_table_size) return nullptr;
    return ins_table[id];
}

void core::serialize(serialized_state &state) {
#define S(x) Sadd(state, &(x), sizeof(x))
    // PINS — save enum fields as u8
    u8 sc = static_cast<u8>(pins.SC);
    u8 cw = static_cast<u8>(pins.clear_wait);
    S(sc); S(cw);
    S(pins.EF); S(pins.INTERRUPT); S(pins.DMA_IN); S(pins.DMA_OUT);
    S(pins.MRD); S(pins.MWR); S(pins.Q); S(pins.Addr); S(pins.D); S(pins.N);
    // REGS
    for (u32 i = 0; i < 16; i++) S(regs.R[i].u);
    S(regs.N); S(regs.I); S(regs.T.u); S(regs.IE); S(regs.P); S(regs.X);
    S(regs.D); S(regs.IR); S(regs.B); S(regs.DF);
    // execution state
    S(execs_left);
    u8 ins_id = ins_to_id(ins);
    S(ins_id);
    S(most_recent_fetch);
#undef S
}

void core::deserialize(serialized_state &state) {
#define L(x) Sload(state, &(x), sizeof(x))
    // PINS — restore enum fields via memcpy to avoid type issues
    u8 sc, cw;
    L(sc); L(cw);
    memcpy(&pins.SC,         &sc, 1);
    memcpy(&pins.clear_wait, &cw, 1);
    L(pins.EF); L(pins.INTERRUPT); L(pins.DMA_IN); L(pins.DMA_OUT);
    L(pins.MRD); L(pins.MWR); L(pins.Q); L(pins.Addr); L(pins.D); L(pins.N);
    // REGS
    for (u32 i = 0; i < 16; i++) L(regs.R[i].u);
    L(regs.N); L(regs.I); L(regs.T.u); L(regs.IE); L(regs.P); L(regs.X);
    L(regs.D); L(regs.IR); L(regs.B); L(regs.DF);
    // execution state
    L(execs_left);
    u8 ins_id;
    L(ins_id);
    ins = id_to_ins(ins_id);
    L(most_recent_fetch);
#undef L
}

}