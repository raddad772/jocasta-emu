#pragma once

#define TBOOL template <armtype cpukind, typename scheduler_kind>bool core<cpukind, scheduler_kind>::
#define TVOID template <armtype cpukind, typename scheduler_kind>void core<cpukind, scheduler_kind>::
#define TVOIDC template <armtype cpukind, typename scheduler_kind>  template<bool cached> void core<cpukind, scheduler_kind>::
#define TVOIDD template <armtype cpukind, typename scheduler_kind> template<bool do_debug> void core<cpukind, scheduler_kind>::
#define TVOIDDC template <armtype cpukind, typename scheduler_kind> template<bool do_debug, bool cached> void core<cpukind, scheduler_kind>::
#define TU32 template <armtype cpukind, typename scheduler_kind>, typename scheduler_kindu32 core<cpukind, scheduler_kind>::
#define TU32P template <armtype cpukind, typename scheduler_kind>u32 *core<cpukind, scheduler_kind>::

#define PC R[15]

TVOID setup_tracing(jsm_debug_read_trace *strct, u64 *trace_cycle_pointer, i32 source_id)
{
    trace.strct.read_trace_m68k = strct->read_trace_m68k;
    trace.strct.ptr = strct->ptr;
    trace.strct.read_trace = strct->read_trace;
    trace.ok = true;
    trace.cycles = trace_cycle_pointer;
    trace.source_id = source_id;
}

TVOID print_context(ARMctxt *ct, jsm_string *out, bool taken) const {
    out->quickempty();
    bool needs_commaspace = false;
    if (!taken) out->sprintf("NT.");
    for (u32 i = 0; i < 16; i++) {
        if (ct->regs & (1 << i)) {
            if (needs_commaspace) {
                out->sprintf(", ");
            }
            needs_commaspace = true;
            out->sprintf("R%d:%08x", i, *regmap[i]);
        }
    }
}

TVOIDDC undefined_exception()
{
    regs.R_und[1] = regs.PC - 4;
    printf("\nWARN: PC MAY BE WRONG");
    regs.SPSR_und = regs.CPSR.u;
    regs.CPSR.mode = M_undefined;
    fill_regmap();

    regs.CPSR.I = 1;
    regs.PC = regs.EBR | 0x00000004;
    reload_pipeline<do_debug, cached>();
}


TVOID fill_regmap() {
    for (u32 i = 8; i < 15; i++) {
        regmap[i] = old_getR(i);
    }
}

template <armtype cpukind, typename scheduler_kind>
template<bool do_debug, bool cached, bool is_FIQ> void core<cpukind, scheduler_kind>::do_interrupt()
{
    halted = false;
    if constexpr(do_debug) {
        if (dbg.dvptr && trace.exception_id && dbg.dvptr->ids_enabled[trace.exception_id]) {
            dbg.dvptr->add_printf(trace.exception_id, *master_clock, DBGLS_TRACE, "ARM IRQ!");
        }
    }
    if constexpr(!cached) {
        if (regs.CPSR.T) {
            fetch_ins<2, do_debug>();
        }
        else {
            fetch_ins<4, do_debug>();
        };
    }
    else {
        if (regs.CPSR.T) {
            (*waitstates) += ins_timing16(ins_timing_ptr, regs.PC, pipeline.access);
        }
        else {
            (*waitstates) += ins_timing16(ins_timing_ptr, regs.PC, pipeline.access);
        }
    }

    if constexpr(is_FIQ) {
        regs.SPSR_fiq = regs.CPSR.u;
        regs.CPSR.mode = M_fiq;
        regs.CPSR.F = 1;
    }
    else {
        regs.SPSR_irq = regs.CPSR.u;
        regs.CPSR.mode = M_irq;
    }

    fill_regmap();
    regs.CPSR.I = 1;

    u32 *r14 = regmap[14];
    if (regs.CPSR.T) {
        regs.CPSR.T = 0;
        *r14 = regs.PC;
    }
    else {
        *r14 = regs.PC - 4;
    }

    if constexpr(is_FIQ) {
        if constexpr (cpukind < AT_ARM7TDMI)
            regs.PC = 0x0000'001C;
        else
            regs.PC = regs.EBR | 0x0000001C;
    }
    else {
        if constexpr (cpukind < AT_ARM7TDMI)
            regs.PC = 0x0000'0018;
        else
            regs.PC = regs.EBR | 0x0000'0018;
    }
    reload_pipeline<do_debug, cached>();
}

template <armtype cpukind, typename scheduler_kind>
template<u8 sz, bool do_debug>
u32 core<cpukind, scheduler_kind>::read(u32 addr, u8 access) {
    if constexpr (cpukind >= AT_ARM946ES) {
        u32 v;
        addr &= maskalign[sz];

        if (addr_in_itcm(addr) && nds_cp15.regs.control.itcm_enable && !nds_cp15.regs.control.itcm_load_mode) {
            return read_itcm<sz, false>(addr);
        }
        if (!(access & ARM32P_code) && addr_in_dtcm(addr) && nds_cp15.regs.control.dtcm_enable && !nds_cp15.regs.control.dtcm_load_mode) {
            return read_dtcm<sz>(addr);
        }
        if constexpr (do_debug) {
            if constexpr(sz == 1) return read_func8_debug(read_ptr, addr, access) & 0xFF;
            else if constexpr(sz == 2) return read_func16_debug(read_ptr, addr, access) & 0xFFFF;
            else if constexpr(sz == 4) return read_func32_debug(read_ptr, addr, access);
            else NOGOHERE;
        }
        else {
            if constexpr(sz == 1) return read_func8(read_ptr, addr, access) & 0xFF;
            else if constexpr(sz == 2) return read_func16(read_ptr, addr, access) & 0xFFFF;
            else if constexpr(sz == 4) return read_func32(read_ptr, addr, access);
            else NOGOHERE;
        }
    }
    else {
        if constexpr (do_debug) {
            if constexpr(sz == 1) return read_func8_debug(read_ptr, addr, access) & 0xFF;
            else if constexpr(sz == 2) return read_func16_debug(read_ptr, addr, access) & 0xFFFF;
            else if constexpr(sz == 4) return read_func32_debug(read_ptr, addr, access);
            else NOGOHERE;
        }
        else {
            if constexpr(sz == 1) return read_func8(read_ptr, addr, access) & 0xFF;
            else if constexpr(sz == 2) return read_func16(read_ptr, addr, access) & 0xFFFF;
            else if constexpr(sz == 4) return read_func32(read_ptr, addr, access);
            else NOGOHERE;
        }
    }
}

template <armtype cpukind, typename scheduler_kind>
template<u8 sz, bool do_debug, bool cached>
void core<cpukind, scheduler_kind>::write(u32 addr, u8 access, u32 val) {
    if constexpr (cpukind >= AT_ARM946ES) {
        addr &= maskalign[sz];
        if (addr_in_itcm(addr) && nds_cp15.regs.control.itcm_enable) {
            write_itcm<sz, cached>(addr, val);
            return;
        }
        if (!(access & ARM32P_code) && addr_in_dtcm(addr) && nds_cp15.regs.control.dtcm_enable) {
            write_dtcm<sz>(addr, val);
            return;
        }
    }
    if constexpr(do_debug) {
        if constexpr(sz == 1) write_func8_debug(write_ptr, addr, access, val);
        if constexpr(sz == 2) write_func16_debug(write_ptr, addr, access, val);
        if constexpr(sz == 4) write_func32_debug(write_ptr, addr, access, val);
    }
    else {
        if constexpr(sz == 1) write_func8(write_ptr, addr, access, val);
        if constexpr(sz == 2) write_func16(write_ptr, addr, access, val);
        if constexpr(sz == 4) write_func32(write_ptr, addr, access, val);
    }
}

template <armtype cpukind, typename scheduler_kind>
template<u8 sz, bool do_debug>
u32 core<cpukind, scheduler_kind>::fetch_ins() {
    u32 addr = regs.PC;
    if constexpr (cpukind >= AT_ARM946ES) {
        addr &= maskalign[sz];
        if (addr_in_itcm(addr) && nds_cp15.regs.control.itcm_enable && !nds_cp15.regs.control.itcm_load_mode) {
            return read_itcm<sz, false>(addr);
        }
    }
    if constexpr(do_debug) {
        if constexpr(sz == 2) return fetch_ins_func16_debug(fetch_ptr, addr, pipeline.access);
        if constexpr(sz == 4) return fetch_ins_func32_debug(fetch_ptr, addr, pipeline.access);
    }
    else {
        if constexpr(sz == 2) return fetch_ins_func16(fetch_ptr, addr, pipeline.access);
        if constexpr(sz == 4) return fetch_ins_func32(fetch_ptr, addr, pipeline.access);
    }
    NOGOHERE;
}

TU32P get_SPSR_by_mode() {
    switch(regs.CPSR.mode) {
        case M_user:
            return &regs.CPSR.u;
        case M_fiq:
            return &regs.SPSR_fiq;
        case M_irq:
            return &regs.SPSR_irq;
        case M_supervisor:
            return &regs.SPSR_svc;
        case M_abort:
            return &regs.SPSR_abt;
        case M_undefined:
            return &regs.SPSR_und;
        default:
        case M_system:
            printf("\nINVALID2!!!");
            return &regs.SPSR_invalid;
    }
}

TVOIDD decode_and_exec_THUMB(u32 opcode, u32 opcode_addr)
{
    if constexpr(do_debug) {
        arm_trace_format(opcode, opcode_addr, true, true);
        const arm32_cached_ins<cpukind, scheduler_kind> &ins = opcode_table_thumb[opcode];
        (this->*ins.exec_debug)(ins);
    }
    else {
        const arm32_cached_ins<cpukind, scheduler_kind> &ins = opcode_table_thumb[opcode];
        if (!ins.exec) {
            dbg_break("WHAT!?!!?", 0);
            return;
        }
        (this->*ins.exec)(ins);
    }
}

TVOIDD decode_and_exec_ARM(u32 opcode, u32 opcode_addr)
{
    // bits 27-0 and 7-4
    //if (regs.PC == 0x030024c4) dbg_break("GOT TO INS!", 0);
    if constexpr(do_debug)
        arm_trace_format(opcode, opcode_addr, false, true);
    u32 decode = ((opcode >> 4) & 15) | ((opcode >> 16) & 0xFF0);
    if constexpr(do_debug) arm_ins = &opcode_table_arm_debug[decode];
    else arm_ins = &opcode_table_arm[decode];
    (this->*arm_ins->exec)(opcode);
}

TU32P old_getR(u32 num) {
    // valid modes are,
    // 16-19, 23, 27, 31
    u32 m = regs.CPSR.mode;
    if ((m < 16) || ((m > 19) && (m != 23) && (m != 27) && (m != 31))) {
        if (num == 15) return &regs.R[15];
        return &regs.R_invalid[num];
    }
    switch(num) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            return &regs.R[num];
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
            return regs.CPSR.mode == M_fiq ? &regs.R_fiq[num - 8] : &regs.R[num];
        case 13:
        case 14: {
            switch(regs.CPSR.mode) {
                case M_abort:
                    return &regs.R_abt[num - 13];
                case M_fiq:
                    return &regs.R_fiq[num - 8];
                case M_irq:
                    return &regs.R_irq[num - 13];
                case M_supervisor:
                    return &regs.R_svc[num - 13];
                case M_undefined:
                    return &regs.R_und[num - 13];
                case M_user:
                case M_system:
                    return &regs.R[num];
                default:
                    //assert(1==2);
                    return nullptr;
            }
            break; }
        case 15:
            return &regs.R[15];
        default:
            //assert(1==2);
            return nullptr;
    }
}

template<armtype cpukind, typename scheduler_kind>
core<cpukind, scheduler_kind>::core(scheduler_kind *scheduler_in, u64 *master_clock_in, u64 *waitstates_in)  :
        scheduler{scheduler_in},
        nds_cp15(&regs.EBR, &halted),
        waitstates{waitstates_in},
        master_clock{master_clock_in}
{
    fill_ARM_table();
    for (u32 i = 0; i < 16; i++) {
        regmap[i] = &regs.R[i];
    }
    for (u32 i = 0; i < 65536; i++) {
        cached_decode_thumb32<false, false>(i, opcode_table_thumb[i]);
    }
    if constexpr(cpukind == AT_ARM946ES) {
        CP_ptr = &this->nds_cp15;
        CP_write = &NDS_CP15::ptr_write;
        CP_read = &NDS_CP15::ptr_read;
    }
}

TBOOL condition_passes(condition_codes which) const {
#define flag(x) (regs.CPSR. x)
    switch(which) {
        case CC_AL:    return true;
        case CC_NV:    return false;
        case CC_EQ:    return flag(Z) == 1;
        case CC_NE:    return flag(Z) != 1;
        case CC_CS_HS: return flag(C) == 1;
        case CC_CC_LO: return flag(C) == 0;
        case CC_MI:    return flag(N) == 1;
        case CC_PL:    return flag(N) == 0;
        case CC_VS:    return flag(V) == 1;
        case CC_VC:    return flag(V) == 0;
        case CC_HI:    return (flag(C) == 1) && (flag(Z) == 0);
        case CC_LS:    return (flag(C) == 0) || (flag(Z) == 1);
        case CC_GE:    return flag(N) == flag(V);
        case CC_LT:    return flag(N) != flag(V);
        case CC_GT:    return (flag(Z) == 0) && (flag(N) == flag(V));
        case CC_LE:    return (flag(Z) == 1) || (flag(N) != flag(V));
        default:
            NOGOHERE;
            return false;
    }
#undef flag
}

TVOIDD run_ARM() {
    if constexpr (cpukind == AT_ARM946ES) {
        if (halted) {
            (*waitstates)++;
            return;
        }
    }
    u32 opcode = pipeline.opcode[0];
    u32 opcode_addr = pipeline.addr[0];
    pipeline.opcode[0] = pipeline.opcode[1];
    pipeline.addr[0] = pipeline.addr[1];
    regs.PC &= 0xFFFFFFFE;

    pipeline.opcode[1] = fetch_ins<4, do_debug>();
    pipeline.addr[1] = regs.PC;
    if (condition_passes(static_cast<condition_codes>(opcode >> 28))) {
        decode_and_exec_ARM<do_debug>(opcode, opcode_addr);
    }
    else {
        // check for PLD and 4 undefined's
        bool execed = false;
        if constexpr (cpukind >= AT_ARM946ES) {
            if ((opcode >> 28) == 15) {
                if constexpr (do_debug) arm_trace_format(opcode, opcode_addr, false, true);
                u32 decode = ((opcode >> 4) & 15) | ((opcode >> 16) & 0xFF0);
                if constexpr(do_debug) arm_ins = &opcode_table_arm_never_debug[decode];
                else arm_ins = &opcode_table_arm_never[decode];
                if (arm_ins->valid) {
                    (this->*arm_ins->exec)(opcode);
                    execed = true;
                }
            }
        }

        if (!execed) {
            if constexpr(do_debug) arm_trace_format(opcode, opcode_addr, false, false);
            pipeline.access = ARM32P_code | ARM32P_sequential;
            regs.PC += 4;
        }
    }
}


TVOIDD run_THUMB()  {
    if constexpr (cpukind == AT_ARM946ES) {
        if (halted) {
            (*waitstates)++;
            return;
        }
    }
    u32 opcode = pipeline.opcode[0];
    u32 opcode_addr = pipeline.addr[0];
    pipeline.opcode[0] = pipeline.opcode[1];
    pipeline.addr[0] = pipeline.addr[1];
    regs.PC &= 0xFFFFFFFE;

    pipeline.opcode[1] = fetch_ins<2, do_debug>();
    pipeline.addr[1] = regs.PC;
    decode_and_exec_THUMB<do_debug>(opcode, opcode_addr);
}

TVOID direct_boot() {
    if constexpr (cpukind == AT_ARM946ES)
        nds_cp15.direct_boot();
}

TVOIDC reset(){
    regs.CPSR.F = 1;
    regs.CPSR.mode = M_supervisor;
    regs.SPSR_svc = regs.CPSR.u;
    regs.CPSR.T = 0;
    regs.CPSR.I = 1;
    fill_regmap();
    *regmap[14] = 0;
    *regmap[15] = 0;
    if constexpr (cpukind == AT_ARM7DI) {
        regs.R_irq[0] = 0x0300'7FA0;
        regs.R_svc[0] = 0x0300'7FE0;
    }
    if constexpr (cpukind >= AT_ARM946ES) {
        regs.R[15] = 0xFFFF0000;
        regs.EBR = 0xFFFF0000;
    }
    else {
        regs.EBR = 0;
    }
    if constexpr (cpukind == AT_ARM946ES)
        nds_cp15.reset();
    reload_pipeline<false, cached>();
}

template <armtype cpukind, typename scheduler_kind>
template<bool do_debug, bool cached>
void core<cpukind, scheduler_kind>::reload_pipeline() {
    if (regs.CPSR.T) reload_pipeline_THUMB<do_debug, cached>();
    else reload_pipeline_ARM<do_debug, cached>();
}

template <armtype cpukind, typename scheduler_kind>
template<bool do_debug, bool cached, bool do_sched>
bool core<cpukind, scheduler_kind>::IRQcheck() {
    if constexpr (do_sched) {
        scheduler->add_next(0, this, &sch_check_irq<false, cached>, &sch_check_irq<true, cached>, nullptr);
        return false;
    }
    if (regs.IRQ_line && !regs.CPSR.I) {
        do_interrupt<do_debug, cached, false>();
        return true;
    }
    if (regs.FIQ_line && !regs.CPSR.F) {
        do_interrupt<do_debug, cached, true>();
        return true;
    }
    return false;
}

TVOIDC schedule_IRQ_check()
{
    if (scheduler && !sch_irq_sch) {
        scheduler->add_next(0, this, &sch_check_irq<false, cached>, &sch_check_irq<true, cached>, &sch_irq_sch);
    }
}

TVOIDDC sch_check_irq(void *ptr, u64 key, u64 timecode, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    if (th->regs.IRQ_line && !th->regs.CPSR.I) {
        th->template do_interrupt<do_debug, cached, false>();
    }
}


template <armtype cpukind, typename scheduler_kind>
template<bool do_debug, bool cached>
void core<cpukind, scheduler_kind>::reload_pipeline_ARM() {
    if constexpr(cached) {
        (*waitstates) += ins_timing32(ins_timing_ptr, regs.PC, ARM32P_code | ARM32P_nonsequential);
        regs.PC += 4;
        pipeline.access = ARM32P_code | ARM32P_sequential;
        (*waitstates) += ins_timing32(ins_timing_ptr, regs.PC, pipeline.access);
        regs.PC += 4;
        return;
    }
    pipeline.access = ARM32P_code | ARM32P_nonsequential;
    pipeline.opcode[0] = fetch_ins<4, do_debug>();
    pipeline.addr[0] = regs.PC;
    regs.PC += 4;
    pipeline.access = ARM32P_code | ARM32P_sequential;
    pipeline.opcode[1] = fetch_ins<4, do_debug>();
    pipeline.addr[1] = regs.PC;
    regs.PC += 4;
}

template <armtype cpukind, typename scheduler_kind>
template<bool do_debug, bool cached>
void core<cpukind, scheduler_kind>::reload_pipeline_THUMB() {
    if constexpr(cached) {
        (*waitstates) += ins_timing16(ins_timing_ptr, regs.PC, ARM32P_code | ARM32P_nonsequential);
        regs.PC += 2;
        pipeline.access = ARM32P_code | ARM32P_sequential;
        (*waitstates) += ins_timing16(ins_timing_ptr, regs.PC, pipeline.access);
        regs.PC += 2;
        return;
    }
    pipeline.access = ARM32P_code | ARM32P_nonsequential;
    pipeline.opcode[0] = fetch_ins<2, do_debug>() & 0xFFFF;
    pipeline.addr[0] = regs.PC;
    regs.PC += 2;
    pipeline.access = ARM32P_code | ARM32P_sequential;
    pipeline.opcode[1] = fetch_ins<2, do_debug>() & 0xFFFF;
    pipeline.addr[1] = regs.PC;
    regs.PC += 2;
}

TVOID arm_trace_format(u32 opcode, u32 addr, bool T, bool taken) {
    bool do_dbglog = false;
    if (dbg.dvptr) {
        do_dbglog = dbg.dvptr->ids_enabled[dbg.dv_id];
    }
    u32 do_tracething = (dbg.tvptr && ::dbg.do_debug && ::dbg.traces.arm946es.instruction);
    if (do_dbglog || do_tracething) {
        ARMctxt ct;
        ct.regs = 0;
        if constexpr (cpukind <= AT_ARM7TDMI) {
            if (T) {
                THUMBv4_disassemble(opcode, &trace.str, static_cast<i64>(addr), &ct);
            } else {
                ARMv4_disassemble(opcode, &trace.str, static_cast<i64>(addr), &ct);
            }
        }
        else {
            if (T) {
                THUMBv5_disassemble(opcode, &trace.str, (i64) addr, &ct);
            } else {
                ARMv5_disassemble(opcode, &trace.str, (i64) addr, &ct);
            }
        }
        print_context(&ct, &trace.str2, taken);

        u64 tc;
        if (!trace.cycles) tc = 0;
        else tc = *trace.cycles;
        tc += *waitstates;

        if (do_dbglog) {
            dbglog_view *dv = dbg.dvptr;
            dv->add_printf(dbg.dv_id, tc, DBGLS_TRACE, "(%08x) %08x  %s", opcode, addr, trace.str.ptr);
            dv->extra_printf("%s", trace.str2.ptr);
        }

        if (do_tracething) {
            trace_view *tv = dbg.tvptr;
            tv->startline(trace.source_id);
            if (T) {
                tv->printf(0, "THUMB9");
                tv->printf(3, "%04x", opcode);
            } else {
                tv->printf(0, "ARM9");
                tv->printf(3, "%08x", opcode);
            }
            tv->printf(1, "%lld", tc);
            tv->printf(2, "%08x", addr);
            tv->printf(4, "%s", trace.str.ptr);
            tv->printf(5, "%s", trace.str2.ptr);
            tv->endline();
        }
    }
}

TVOID exit_cached_mode() {
    cached_mode = false;
}

TVOID enter_cached_mode() {
    // Clear the ITCM block cache (ARM946E-S only) so the cached interpreter
    // starts from a consistent state.  Interpreter execution does not
    // invalidate this cache, so it must be cleared before re-entering cached
    // mode to avoid stale blocks.
    if constexpr (cpukind >= AT_ARM946ES) {
        arm9_block_cache.clear_all_blocks();
    }
    cached_mode = true;
    // Fill the pipeline by peeking (no side-effects) at the two pre-fetched
    // addresses so the cached interpreter can start from a consistent state.
    if (regs.CPSR.T) {
        // THUMB: PC is 4 ahead of the executing instruction.
        // pipeline[0] = PC-4,  pipeline[1] = PC-2
        pipeline.access = ARM32P_code | ARM32P_nonsequential;
        pipeline.opcode[0] = fetch_ins_func16_peek(fetch_ptr, regs.PC - 4, pipeline.access) & 0xFFFF;
        pipeline.addr[0] = regs.PC - 4;
        pipeline.access = ARM32P_code | ARM32P_sequential;
        pipeline.opcode[1] = fetch_ins_func16_peek(fetch_ptr, regs.PC - 2, pipeline.access) & 0xFFFF;
        pipeline.addr[1] = regs.PC - 2;
    } else {
        // ARM: PC is 8 ahead of the executing instruction.
        // pipeline[0] = PC-8,  pipeline[1] = PC-4
        pipeline.access = ARM32P_code | ARM32P_nonsequential;
        pipeline.opcode[0] = fetch_ins_func32_peek(fetch_ptr, regs.PC - 8, pipeline.access);
        pipeline.addr[0] = regs.PC - 8;
        pipeline.access = ARM32P_code | ARM32P_sequential;
        pipeline.opcode[1] = fetch_ins_func32_peek(fetch_ptr, regs.PC - 4, pipeline.access);
        pipeline.addr[1] = regs.PC - 4;
    }
}

#undef TVOID
#undef TU32
#undef TU32P
#undef TBOOL
#undef PC