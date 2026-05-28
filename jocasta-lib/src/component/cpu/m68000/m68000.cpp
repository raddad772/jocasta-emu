//
// Created by RadDad772 on 4/14/24.
//

#include <cassert>
#include <cstring>
#include <cstdio>

#include "helpers/debug.h"
#include "helpers/debugger/debugger.h"
#include "helpers/serialize/serialize.h"
#include "m68000.h"
#include "m68000_instructions.h"
#include "m68000_internal.h"
#include "m68000_disassembler.h"

#define M68K_TRACE_FORMAT_COMPACT 0
#define M68K_TRACE_FORMAT_FULL_REGS 1

#define M68K_TRACE_FORMAT M68K_TRACE_FORMAT_FULL_REGS

namespace M68k {
void disasm_RESET_POWER(ins_t &ins, u32 &PC, jsm_debug_read_trace &rt, jsm_string &out, struct ctxt &ct);
void do_decode();

core::core(bool megadrive_bug_in) : megadrive_bug(megadrive_bug_in) {
    do_decode();
    SPEC_RESET.sz = 4;
    SPEC_RESET.operand_mode = OM_none;
    SPEC_RESET.disasm = &disasm_RESET_POWER;
    SPEC_RESET.exec = &core::ins_RESET_POWER;
    //regs.D[0] = regs.A[0] = 0xFFFFFFFF;
    iack_handlers.reserve(2);
}

void core::setup_tracing(const jsm_debug_read_trace *strct, u64 *trace_cycle_pointer)
{
    trace.strct.read_trace_m68k = strct->read_trace_m68k;
    trace.strct.ptr = strct->ptr;
    trace.strct.read_trace = strct->read_trace;
    trace.ok = 1;
    trace.cycles = trace_cycle_pointer;
}

void core::set_SR(u32 val, u32 immediate_t)
{
    u32 new_s = (val & 0x2000) >> 13;
    // CCR
    //regs.SR.u = val & 0xA71F; //(regs.SR.u & 0xFFE0) | (val & 0x1F);
    regs.SR.u = (regs.SR.u & 0xFFE0) | (val & 0x1F);

    if (new_s != regs.SR.S) {
        swap_ASP();
    }
    regs.SR.S = new_s;

    regs.SR.I = (val >> 8) & 7;
    regs.next_SR_T = (val >> 15) & 1;
    if (immediate_t || (regs.next_SR_T == 0)) regs.SR.T = regs.next_SR_T;
}

void core::pprint_ea(ins_t &m_ins, u32 opnum, jsm_string *outstr) const {
    EA *ea = &m_ins.ea[opnum];
    u32 kind = 0; // 0 = NONE, 1 = addr reg, 2 = data reg, 3 = EA
    if (opnum == 0) {
        switch(m_ins.operand_mode) {
            case OM_none:
            case OM_qimm:
                break;
            case OM_ea_r:
            case OM_ea:
            case OM_ea_ea:
                if (ea->kind == AM_data_register_direct) kind = 2;
                else if (ea->kind == AM_address_register_indirect) kind = 1;
                else kind = 3;
                break;
            case OM_r:
            case OM_r_ea:
            case OM_r_r:
                if (ea->kind == AM_data_register_direct)
                    kind = 2;
                else
                    kind = 1;
                break;
            case OM_qimm_qimm:
            case OM_qimm_r:
            case OM_qimm_ea:
                break;
            default:
                printf("\nFORGOT DISASM1 FOR OM %d", m_ins.operand_mode);
                break;
        }
    }
    else {
        switch(m_ins.operand_mode) {
            case OM_r:
            case OM_ea:
            case OM_none:
                break;
            case OM_r_r:
            case OM_ea_r:
            case OM_qimm_r:
                if (ea->kind == AM_data_register_direct)
                    kind = 2;
                else if (ea->kind == AM_address_register_direct)
                    kind = 1;
                else
                    assert(1==2);
                break;
            case OM_r_ea:
            case OM_qimm_ea:
            case OM_ea_ea:
                if (ea->kind == AM_data_register_direct) kind = 2;
                else if (ea->kind == AM_address_register_indirect) kind = 1;
                else kind = 3;
                break;
            case OM_qimm:
            case OM_qimm_qimm:
                break;
            default:
                printf("\nFORGOT DISASM2 FOR OM %d", m_ins.operand_mode);
                break;
        }
    }
    if (kind == 3) {
        switch(ea->kind) {
            case AM_address_register_indirect_with_predecrement:
            case AM_address_register_indirect:
            case AM_address_register_indirect_with_postincrement:
            case AM_address_register_indirect_with_displacement:
            case AM_address_register_indirect_with_index:
            case AM_address_register_direct:
                kind = 1; // addr reg
                break;
            default:
                kind = 0;
                break;
        }
    }
    switch(kind) {
        case 0:
            break;
        case 1: // addr reg
            if (outstr) outstr->sprintf("A%d:%08x", ea->reg, regs.A[ea->reg]);
            else dbg_printf("A%d:%08x", ea->reg, regs.A[ea->reg]);
            break;
        case 2: // data reg
            if (outstr) outstr->sprintf("D%d:%08x", ea->reg, regs.D[ea->reg]);
            else dbg_printf("D%d:%08x", ea->reg, regs.D[ea->reg]);
            break;
        nodefault
    }
    if ((kind != 0) && (opnum == 0)) {
        if (outstr) outstr->sprintf("  ");
        else dbg_printf("  ");
    }
}

void core::trace_format()
{
    if (dbg.dvptr && dbg.dvptr->ids_enabled[dbg.dv_id]) {
        trace.str.quickempty();
        trace.str2.quickempty();
        dbglog_view *dv = dbg.dvptr;
        u64 tc;
        if (!trace.cycles) tc = 0;
        else tc = *trace.cycles;

        const u32 pc = debug.ins_PC & 0xFFFFFF;
        disassemble(pc, regs.IR, trace.strct, trace.str, &trace.str2, this);
#if M68K_TRACE_FORMAT == M68K_TRACE_FORMAT_FULL_REGS
        char flags[9]{};
        flags[0] = regs.SR.T ? 'T' : 't';
        flags[1] = regs.SR.S ? 'S' : 's';
        flags[2] = static_cast<char>('0' + regs.SR.I);
        flags[3] = regs.SR.C ? 'C' : 'c';
        flags[4] = regs.SR.V ? 'V' : 'v';
        flags[5] = regs.SR.Z ? 'Z' : 'z';
        flags[6] = regs.SR.N ? 'N' : 'n';
        flags[7] = regs.SR.X ? 'X' : 'x';
        flags[8] = 0;
        dv->add_printf(dbg.dv_id, tc, DBGLS_TRACE,
                       "CPU  %06x  %04x  %-48s d0:%08x d1:%08x d2:%08x d3:%08x d4:%08x d5:%08x d6:%08x d7:%08x a0:%08x a1:%08x a2:%08x a3:%08x a4:%08x a5:%08x a6:%08x a7:%08x sp:%08x %s cyc:%llu",
                       pc, regs.IR & 0xFFFF, trace.str.ptr,
                       regs.D[0], regs.D[1], regs.D[2], regs.D[3],
                       regs.D[4], regs.D[5], regs.D[6], regs.D[7],
                       regs.A[0], regs.A[1], regs.A[2], regs.A[3],
                       regs.A[4], regs.A[5], regs.A[6], regs.A[7],
                       regs.ASP, flags, tc);
#elif M68K_TRACE_FORMAT == M68K_TRACE_FORMAT_COMPACT
        dv->add_printf(dbg.dv_id, tc, DBGLS_TRACE, "%06x  %s", pc, trace.str.ptr);
        dv->extra_printf("%s", trace.str2.ptr);
#else
#error Unknown M68K_TRACE_FORMAT
#endif
    }

    /*trace.str.quickempty();
    disassemble(regs.PC-2, regs.IR, trace.strct, trace.str);
#ifdef  M68K_TESTING
    printf(DBGC_M68K "\nM68k  (%04llu)   %06x  %s" DBGC_RST, tc, regs.PC-4, trace.str.ptr);
#else
    dbg_printf(DBGC_M68K "\nM68k  (%04llu)   %06x  %s", tc, regs.PC-4, trace.str.ptr);
    dbg_seek_in_line(TRACE_BRK_POS);
    dbg_printf("SR:%04x  ", regs.SR.u);
    pprint_ea(*ins, 0, nullptr);
    pprint_ea(*ins, 1, nullptr);
    dbg_printf(DBGC_RST);
#endif*/
}

void core::register_iack_handler(void *ptr, iack_handler_f debug_off, iack_handler_f debug_on)
{
    iack_handler &nh = iack_handlers.emplace_back();
    nh.ptr = ptr;
    nh.handler[0] = debug_off;
    nh.handler[1] = debug_on;
}

void core::set_interrupt_level(u32 val)
{
    if ((pins.IPL < 7) && (val == 7)) {
        state.nmi = true;
    }
    pins.IPL = val;
}

void core::lycoder_pprint2() const {
    // d:FFFFFFFF FFFFFFFF B6DB6DB6 DB6DB6DB 6DB6DB6D 00000000 00000000 00000000
    dbg_printf(" d:%08X %08X %08X %08X %08X %08X %08X %08X",
               regs.D[0], regs.D[1], regs.D[2], regs.D[3],
               regs.D[4], regs.D[5], regs.D[6], regs.D[7]);
    // a:00600100 004001BE 004001C0 00000000 00000004 004006F8 004000F0 0000FC00
    dbg_printf(" a:%08X %08X %08X %08X %08X %08X %08X %08X",
               regs.A[0], regs.A[1], regs.A[2], regs.A[3],
               regs.A[4], regs.A[5], regs.A[6], regs.A[7]);
    // pc:4002D0 sr:2700 asp:000000
    dbg_printf(" pc:%08X sr:%04X asp:%08X\n", opc, regs.SR.u, regs.ASP);
}

void core::lycoder_pprint1()
{
    // cyc:14742674 a:004002D0 opc:21FC
    dbg_printf("cyc:%lld a:%08X opc:%04X", *trace.cycles, regs.PC-4, regs.IRD);
    opc = regs.PC - 4;
}

void core::unstop()
{


    if (state.current == S_stopped)
        state.current = state.stopped.next_state;
}

u32 core::get_SR() const {
    return regs.SR.u;
}

void core::reset()
{
    state.current = S_exec;
    state.instruction.done = 0;
    state.e_clock_count = 0;
    set_SR((get_SR() & 0x1F) | 0x2700, 1);
    last_decode = 0x50000;
    ins = &SPEC_RESET;
    state.instruction.TCU = 0;
    state.instruction.prefetch = 0;
    opc = 0xFFFFFFFF;
}

void core::disassemble_entry(disassembly_entry& entry)
{
    const u16 IR = trace.strct.read_trace_m68k(trace.strct.ptr, entry.addr, 1, 1) & 0xFFFF;
    entry.dasm.quickempty();
    entry.context.quickempty();
    ins_t &m_ins = decoded[IR];
    u32 mPC = entry.addr+2;
    ctxt ct{};
    m_ins.disasm(m_ins, mPC, trace.strct, entry.dasm, ct);
    entry.ins_size_bytes = mPC - entry.addr;
    pprint_ea(m_ins, 0, &entry.context);
    pprint_ea(m_ins, 1, &entry.context);
}

#define S(x) Sadd(state, & x, sizeof( x))
void REGS::serialize(serialized_state &state) {
    S(D);
    S(A);
    S(IPC);
    S(PC);
    S(ASP);
    S(SR.u);
    S(IRC);
    S(IR);
    S(IRD);
    S(next_SR_T);
}

void PINS::serialize(serialized_state &state) {
    S(FC);
    S(Addr);
    S(D);
    S(DTACK);
    S(VPA);
    S(VMA);
    S(AS);
    S(RW);
    S(IPL);
    S(UDS);
    S(LDS);
    S(RESET);
}
#undef S
#define S(x) Sadd(m_state, & x, sizeof( x))
void core::serialize(serialized_state &m_state)
{
    regs.serialize(m_state);
    pins.serialize(m_state);

    S(ins_decoded);
    S(testing);
    S(opc);
    S(megadrive_bug);
    S(debug);
    S(state);
    S(last_decode);

    u32 ea = 0;
    if (state.operands.ea == &ins->ea[0])
        ea = 1;
    else if (state.operands.ea == &ins->ea[1])
        ea = 2;
    Sadd(m_state, &ea, sizeof(ea));

    u32 func = serialize_func();
    Sadd(m_state, &func, sizeof(func));
}
#undef S


#define L(x) Sload(state, & x, sizeof( x))
void REGS::deserialize(serialized_state &state)
{
    L(D);
    L(A);
    L(IPC);
    L(PC);
    L(ASP);
    L(SR.u);
    L(IRC);
    L(IR);
    L(IRD);
    L(next_SR_T);
}

void PINS::deserialize(serialized_state &state)
{
    L(FC);
    L(Addr);
    L(D);
    L(DTACK);
    L(VPA);
    L(VMA);
    L(AS);
    L(RW);
    L(IPL);
    L(UDS);
    L(LDS);
    L(RESET);
}

#undef L
#define L(x) Sload(m_state, & x, sizeof( x))

void core::deserialize(serialized_state &m_state)
{
    regs.deserialize(m_state);
    pins.deserialize(m_state);

    L(ins_decoded);
    L(testing);
    L(opc);
    L(megadrive_bug);
    L(debug);
    L(state);
    L(last_decode);
    if (last_decode == 0x50000)
        ins = &SPEC_RESET;
    else
        ins = &decoded[last_decode];

    // state.bus_cycle.func
    // state.operands.ea,
    // ins
    u32 ea = 0;
    Sload(m_state, &ea, sizeof(ea));
    if (ea == 1)
        state.operands.ea = &ins->ea[0];
    else if (ea == 2)
        state.operands.ea = &ins->ea[1];
    else
        state.operands.ea = nullptr;

    u32 func = 0;
    Sload(m_state, &func, sizeof(func));
    deserialize_func(func);
}
}
