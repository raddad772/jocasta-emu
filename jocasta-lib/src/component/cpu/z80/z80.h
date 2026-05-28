//
// Created by Dave on 2/7/2024.
//
#pragma once
#include "helpers/debugger/debugger.h"
#include "helpers/cvec.h"
#include "helpers/int.h"
#include "helpers/debug.h"
#include "helpers/debugger/debuggerdefs.h"
#include "helpers/serialize/serialize.h"

struct dbglog_view;

namespace Z80 {
constexpr u32 S_IRQ = 0x100;
constexpr u32 S_RESET = 0x101;
constexpr u32 S_DECODE = 0x102;
constexpr u32 MAX_OPCODE = 0x101;

struct regs_F {
    union {
        struct {
            u8 C : 1{}; // 0 - 1
            u8 N : 1{}; // 1 - 2
            u8 PV: 1{}; // 2 - 4
            u8 X : 1{}; // 3 - 8
            u8 H : 1{}; // 4 - 16
            u8 Y : 1{}; // 5 - 32
            u8 Z : 1{}; // 6 - 40
            u8 S : 1{}; // 7 - 80
            /*
             *return this->C | (this->N << 1) | (this->PV << 2) | (this->X << 3) | (this->H << 4) | (this->Y << 5) | (this->Z << 6) | (this->S << 7);
             */
        };
        u8 u;
    };
};

struct REGS {
    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);
    u16 reset_vector{};
    u32 IR{}; // Instruction Register
    u32 TCU{}; // Internal instruction cycle timer register (not on real Z80 under this name)
    u16 ins_PC{};
    u32 A{};
    u32 B{};
    u32 C{};
    u32 D{};
    u32 E{};
    u32 H{};
    u32 L{};
    regs_F F;
    u32 I{}; // Iforget
    u32 R{}; // Refresh counter

    // Shadow registers
    u32 AF_{};
    u32 BC_{};
    u32 DE_{};
    u32 HL_{};

    // Junk calculations
    u32 TR{}, TA{};

    // Temps for register swapping
    u32 AFt{};
    u32 BCt{};
    u32 DEt{};
    u32 HLt{};
    u32 Ht{};
    u32 Lt{};

    // 16-bit registers
    u16 PC{};
    u16 SP{};
    u16 IX{};
    u16 IY{};

    u32 t[10]{};
    u16 WZ{};
    u16 EI{};
    u32 P{};
    u32 Q{};
    u8 IFF1{};
    u8 IFF2{};
    u8 IM{};
    u8 HALT{};

    u32 data{};
    enum prefix {
        P_HL,
        P_IX,
        P_IY
      };

    i32 IRQ_vec{};
    prefix rprefix=P_HL;
    u32 prefix{};

    u32 poll_IRQ{};
    void exchange_shadow_af();
    void exchange_de_hl();
    void exchange_shadow();
    void inc_R();
};

struct PINS {
    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);
    u16 Addr{};
    u8 D{};

    u8 IRQ_maskable{1};
    u8 RD{}, WR{}, IO{}, MRQ{};

    u8 M1{}, WAIT{}; // M1 pin
};

typedef void (*ins_func)(REGS&, PINS&);

struct core {
    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);
    void notify_IRQ(bool level);
    void notify_NMI(bool level);
    void request_NMI();
    explicit core(bool CMOS);
    static ins_func fetch_decoded(u32 opcode, u32 prefix);
    void setup_tracing(jsm_debug_read_trace* dbg_read_trace, u64 *trace_cycle_pointer);
    void reset();
    void set_pins_opcode();
    void set_pins_nothing();
    void set_instruction(u32 to);
    template<bool do_debug> void ins_cycles();
    template<bool do_debug> void cycle();
    void lycoder_print();
    void printf_trace();
    void trace_format();
private:
    void pprint_context(jsm_string &out);

public:

    REGS regs{};
    PINS pins{};
    bool CMOS{};
    bool IRQ_pending{};
    bool NMI_pending{};
    bool NMI_line{};

    struct {
        jsm_debug_read_trace strct{};
        u32 ok{};
        u64 *cycles{};
        u64 last_cycle{};
        u64 my_cycles{};
        jsm_string str{1000}, str2{200};
    } trace{};

    ins_func current_instruction{};

    DBG_START
        DBG_EVENT_VIEW_START
            IRQ, NMI
        DBG_EVENT_VIEW_END
        DBG_LOG_VIEW_SIMPLE
        u32 irq_id{};
    DBG_END
};

#ifndef Z80_NOIMPL
#include "z80_exec.h"
#endif

u32 parity(u32 val);


template<bool do_debug>
void core::ins_cycles() {
    switch(regs.TCU) {
        // 1-4 is fetch next thing and interpret
        // addr T0-1
        // REFRESH on addr T2-3
        // MREQ on T1
        // RD on T1
        // data latch T2
        case 0: // already handled by fetch of next instruction starting
            set_pins_opcode();
            break;
        case 1: // T1 MREQ, RD
            if (regs.poll_IRQ || regs.HALT) {
                // Make sure we only do this at start of an instruction
                regs.poll_IRQ = false;
                if (NMI_pending) {
                    if constexpr(do_debug) {
                        if (dbg.dvptr && dbg.irq_id && dbg.dvptr->ids_enabled[dbg.irq_id]) {
                            dbg.dvptr->add_printf(dbg.irq_id, *trace.cycles, DBGLS_TRACE, "Z80 NMI!");
                        }
                        if (dbg.events.view.vec) {
                            DBG_EVENT(dbg.events.NMI);
                        }
                    }
                    regs.IFF2 = regs.IFF1; // save IFF1 before NMI clears it, so RETN restores correctly
                    NMI_pending = false;
                    regs.HALT = 0;
                    regs.PC = (regs.PC - 1) & 0xFFFF;
                    regs.IRQ_vec = 0x66;
                    pins.IRQ_maskable = false;
                    set_instruction(S_IRQ);
                } else if (IRQ_pending && regs.IFF1 && (!(regs.EI))) {
                    if constexpr(do_debug) {
                        if (dbg.dvptr && dbg.irq_id && dbg.dvptr->ids_enabled[dbg.irq_id]) {
                            dbg.dvptr->add_printf(dbg.irq_id, *trace.cycles, DBGLS_TRACE, "Z80 IRQ!");
                        }
                        if (dbg.events.view.vec) {
                            DBG_EVENT(dbg.events.IRQ);
                        }
                    }
                    regs.HALT = 0;
                    regs.PC = (regs.PC - 1) & 0xFFFF;
                    pins.IRQ_maskable = true;
                    regs.IRQ_vec = 0x38;
                    pins.D = 0xFF;
                    set_instruction(S_IRQ);
                }
            }
            if (regs.HALT) { regs.TCU = 0; break; }
            pins.RD = 1;
            pins.MRQ = 1;
            pins.M1 = 1;
            break;
        case 2: // T2, RD to 0 and data latch, REFRESH and MRQ=1 for REFRESH
            if (pins.WAIT) {
                regs.TCU--;
                return;
            }
            pins.RD = 0;
            pins.MRQ = 0;
            pins.M1 = 0;
            regs.t[0] = pins.D;
            pins.Addr = (regs.I << 8) | regs.R;
            break;
        case 3: // T3 not much here
            // If we need to fetch another, start that and set TCU back to 1
            regs.inc_R();
            if (regs.t[0] == 0xDD) { regs.prefix = 0xDD; regs.rprefix = REGS::P_IX; regs.TCU = -1; break; }
            if (regs.t[0] == 0xfD) { regs.prefix = 0xFD; regs.rprefix = REGS::P_IY; regs.TCU = -1; break; }
            // elsewise figure out what to do next
            // this gets a little tricky
            // 4, 5, 6, 7, 8, 9, 10, 11, 12 = rprefix != HL and is CB, execute CBd
            if ((regs.t[0] == 0xCB) && (regs.rprefix != REGS::P_HL)) {
                regs.prefix = ((regs.prefix << 8) | 0xCB) & 0xFFFF;
                break;
            }
            // . so 13, 14, 15, 16. opcode, then immediate execution CB
            else if (regs.t[0] == 0xCB) {
                regs.prefix = 0xCB;
                regs.TCU = 12;
                break;
            }
            // reuse 13-16
            else if (regs.t[0] == 0xED) {
                regs.prefix = 0xED;
                regs.TCU = 12;
                break;
            }
            else {
                //regs.prefix = 0x00;
                set_instruction(regs.t[0]);
                break;
            }
        case 4: // CBd begins here, as does operand()
            //
            switch(regs.rprefix) {
                case REGS::P_HL:
                    regs.WZ = (regs.H << 8) | regs.L;
                    break;
                case REGS::P_IX:
                    regs.WZ = regs.IX;
                    break;
                case REGS::P_IY:
                    regs.WZ = regs.IY;
                    break;
            }
            set_pins_opcode();
            break;
        case 5: // operand() middle
            pins.RD = 1;
            pins.MRQ = 1;
            break;
        case 6: // operand() end
            regs.WZ = static_cast<u32>((static_cast<i32>(regs.WZ) + static_cast<i32>(static_cast<i8>(pins.D))) & 0xFFFF);
            set_pins_nothing();
            regs.TCU += 2;
            break;
        case 7: // wait a cycle
            break;
        case 8: // wait one more cycle
            break;
        case 9: // start opcode fetch
            set_pins_opcode();
            break;
        case 10:
            pins.RD = 1;
            pins.MRQ = 1;
            break;
        case 11: // cycle 3 of opcode tech
            set_pins_nothing();
            regs.t[0] = pins.D;
            set_instruction(regs.t[0]);
            break;
        case 12: // cycle 4 of opcode fetch. execute instruction!
            //set_instruction(regs.t[0]);
            break;
        case 13: // CB regular and ED regular starts here
            set_pins_opcode();
            break;
        case 14:
            pins.MRQ = 1;
            pins.RD = 1;
            break;
        case 15:
            pins.Addr = (regs.I << 8) | regs.R;
            regs.inc_R();
            regs.t[0] = pins.D;
            set_pins_nothing();
            break;
        case 16:
            // execute from CB or ED now
            set_instruction(regs.t[0]);
            break;
        default:
            NOGOHERE;
    }
}

template<bool do_debug>
void core::cycle() {
    regs.TCU++;
    trace.my_cycles++;
    if (regs.IR == S_DECODE) {
        // Long logic to decode opcodes and decide what to do
        ins_cycles<do_debug>();
    }
    else {
        if constexpr(do_debug) {
            if (regs.TCU == 1) {
                trace.last_cycle = regs.ins_PC;
                trace_format();
                if (regs.IR == 0xB0) {
                    static int a = 4;
                    a++;
                }
            }
        }
        // Execute an actual opcode
        current_instruction(regs, pins);
    }
}

}
