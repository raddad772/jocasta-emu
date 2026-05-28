#pragma once

template<bool do_debug>
void core::cycle()
{
    u32 quit = 0;
    while (!quit) {
        // only functions that cause "work" (i.e. cycles to pass) cause a quit.
        // so waiting for cycles, or doing bus transactions.
        switch(state.current) {
            case S_bus_cycle_iaq:
                bus_cycle_iaq();
                quit = 1;
                break;
            case S_exc_interrupt: {
                exc_interrupt<do_debug>();
                break;
            }
            case S_exc_group0: {
                exc_group0();
                break; }
            case S_exc_group12: {
                exc_group12();
                break; }
            case S_decode: {
#ifdef M68K_TESTING
                if (testing && *trace.cycles > 0) { // FOR TESTING JSONs
                    ins_decoded = 1;
                    return;
                }
#endif
                if (process_interrupts<do_debug>()) break;
                decode<do_debug>();
                if constexpr (do_debug) {
                    debug.ins_PC = regs.PC - 4;
                    /*if ((debug.ins_PC >= 0x400000 + 0x1822) && (debug.ins_PC <= 0x400000 + 0x195A)// P_Sony_DiskPrime
                    //if ((debug.ins_PC >= 0x400000 + 0x1E2C) && (debug.ins_PC <= 0x400000 + 0x1E4A)) { // P_Sony_MotorOn2wh
                    //if ((debug.ins_PC >= 0x400000 + 0x1D84) && (debug.ins_PC <= 0x400000 + 0x1E22) || // P_Sony_Recal
                        //|| (debug.ins_PC >= 0x400000 + 0x1D02) && (debug.ins_PC <= 0x400000 + 0x1D4C) // P_Sony_WakeUp
                        ) {
                        dbg_break("M68000 BREAK", 0);
                        }*/
                    //if (debug.ins_PC == 0x400000 + 0x20B4) dbg_break("GOT IT!", 0);
                    // This must be done AFTER interrupt, trace, etc. processing
                    ins_decoded = 1;
                }
                regs.SR.T = regs.next_SR_T;
                state.current = S_exec;
#ifdef LYCODER
                //if (opc != 0xFFFFFFFF) lycoder_pprint2();
                lycoder_pprint1();
                lycoder_pprint2();
#else
                if constexpr(do_debug) {
                    if (::dbg.traces.cpu2) DFT("\nPC %06x", regs.PC-4);
                    trace_format();
                }
#endif
                break; }
            case S_exec: {
                (this->*ins->exec)();
                if (state.instruction.done)
                    state.current = S_decode;
                break; }
            case S_prefetch: {
                prefetch();
                break; }
            case S_read_operands: {
                read_operands();
                break; }
            case S_read8:
            case S_read16:
            case S_read32:
            case S_write8:
            case S_write16:
            case S_write32: {
                (this->*state.bus_cycle.func)();
                quit = 1;
                break; }
            case S_wait_cycles: { // exit on end of waiting cycles
                if (state.wait_cycles.cycles_left <= 0) {
                    state.current = state.wait_cycles.next_state;
                }
                else {
                    quit = 1;
                    state.wait_cycles.cycles_left--;
                }
                break; }
            case S_stopped: {
                sample_interrupts();
                if (state.exception.interrupt.on_next_instruction)
                    state.current = S_exec;
                else
                    quit = 1;
                break; }
            default:
                assert(1==0);
        }
    }
#ifdef M68K_E_CLOCK
    state.e_clock_count = (state.e_clock_count + 1) % 10;
#endif
}

template<bool do_debug>
u32 core::process_interrupts()
{
    if (state.exception.interrupt.on_next_instruction) {
        state.exception.interrupt.on_next_instruction = false;
        state.exception.interrupt.PC = regs.PC - 4;
        state.exception.interrupt.TCU = 0;
        state.exception.interrupt.new_I = pins.IPL;
        state.current = S_exc_interrupt;
        if constexpr (do_debug) {
            if (::dbg.do_debug && ::dbg.traces.m68000.irq) {
                dbg_printf(DBGC_M68K "\n M68K  (%06llu)  !!!!    INTERRUPT level:%d!" DBGC_RST, *trace.cycles, state.exception.interrupt.new_I);
            }
            if (dbg.dvptr && dbg.irq_id && dbg.dvptr->ids_enabled[dbg.irq_id]) {
                dbg.dvptr->add_printf(dbg.irq_id, *trace.cycles, DBGLS_TRACE, "M68K IRQ %d", pins.IPL);
            }
        }
        return 1;
    }
    return 0;
}

template<bool do_debug>
void core::decode()
{
    u32 IRD = regs.IR;
    regs.IRD = IRD;
    if constexpr(do_debug) {
        /*if ((((regs.PC-4) & 0xFFFFFF) == BREAKPOINT) && (!dbg.did_breakpoint)) {
            dbg_break("M68K PC BREAKPOINT", *trace.cycles);
            dbg.did_breakpoint =1;
            //printf("\nBREAK FOR WAIT ON INTERRUPT!! cycle:%d", *trace.cycles);
        }*/
    }
    last_decode = IRD & 0xFFFF;
    //printf("\nDECODE %04x @%06x", last_decode, debug.ins_PC & 0xFFFFFF);
    ins_t *m_ins = &decoded[IRD & 0xFFFF];
    state.instruction.TCU = 0;
    ins = m_ins;
    state.instruction.done = 0;
    state.instruction.prefetch = 1; // 1 prefetches are needed currently
}
