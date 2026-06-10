//
// Created by . on 7/25/24.
//

#pragma once

#include "helpers/int.h"

namespace VIA6522 {

union PINS {
    struct {
        u64 PRA_in : 8;
        u64 PRB_in : 8;
        u64 PRA_out : 8;
        u64 PRB_out : 8;
        u64 PC : 1;   // CB2 handshake pulse (Port B read/write)
        u64 FLAG : 1; // CA1 external interrupt input
        u64 CNT : 1;  // external clock (T2 pulse count / SR)
        u64 IRQ : 1;
        u64 SP : 1;   // serial port data
        u8 CA1 : 1;
        u8 CA2 : 1;
        u8 CB1 : 1;
        u8 CB2 : 1;
    };
    u64 u;
};

struct TIMER {
    u16 latch{};
    u16 count{};
    u8 out{};
    u8 out_count{};
};

struct chip {
    void reset();
    void cycle();

    struct {
        u8 DDRA{}, DDRB{};
        u8 SR{};
        u8 IFR{}, IER{};   // Interrupt Flag / Enable registers
        u8 ACR{}, PCR{};   // Auxiliary / Peripheral Control registers
        bool T1_running{}, T2_running{};
    } regs{};

    PINS pins{};
    TIMER timerA{}, timerB{};

    u8 read(u8 addr, u8 old, bool has_effect);
    void write(u8 addr, u8 val);
    u8 read_PRB(bool has_effect);
    u8 read_PRA(bool has_effect, bool handshake);
    void set_IRQ_pin(u8 val);

    struct {
        void *ptr{};
        u32 device_num{};
        void (*func)(void *ptr, u32 device_num, u8 lvl);
    } update_irq{};

private:
    int PC_count{};
    u8 old_FLAG{};
    u8 old_CNT{};

    void write_PRA(u8 val);
    void write_PRB(u8 val);
    void write_DDRA(u8 val);
    void write_DDRB(u8 val);
    void write_ACR(u8 val);
    void write_PCR(u8 val);
    void write_IFR(u8 val);
    void write_IER(u8 val);
    void write_SR(u8 val);

    void tick_A();
    void tick_B();
    void update_IRQs();
};

}
