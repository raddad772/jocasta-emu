//
// Created by . on 7/25/24.
//

#include <cstdio>

#include "via6522.h"

namespace VIA6522 {

// IFR/IER bit positions
static constexpr u8 IFR_CA2 = 0x01;
static constexpr u8 IFR_CA1 = 0x02;
static constexpr u8 IFR_SR  = 0x04;
static constexpr u8 IFR_CB2 = 0x08;
static constexpr u8 IFR_CB1 = 0x10;
static constexpr u8 IFR_T2  = 0x20;
static constexpr u8 IFR_T1  = 0x40;
static constexpr u8 IFR_ANY = 0x80;

u8 chip::read(u8 addr, u8 old, bool has_effect) {
    addr &= 15;
    switch(addr) {
        case 0b0000: return read_PRB(has_effect);
        case 0b0001: return read_PRA(has_effect, true);
        case 0b0010: return regs.DDRB;
        case 0b0011: return regs.DDRA;
        case 0b0100: // T1C-L: reading clears T1 interrupt flag
            if (has_effect) { regs.IFR &= ~IFR_T1; update_IRQs(); }
            return timerA.count & 0xFF;
        case 0b0101: return (timerA.count >> 8) & 0xFF;
        case 0b0110: return timerA.latch & 0xFF;
        case 0b0111: return (timerA.latch >> 8) & 0xFF;
        case 0b1000: // T2C-L: reading clears T2 interrupt flag
            if (has_effect) { regs.IFR &= ~IFR_T2; update_IRQs(); }
            return timerB.count & 0xFF;
        case 0b1001: return (timerB.count >> 8) & 0xFF;
        case 0b1010: return regs.SR;
        case 0b1011: return regs.ACR;
        case 0b1100: return regs.PCR;
        case 0b1101: return regs.IFR;
        case 0b1110: return regs.IER | 0x80; // bit 7 always reads as 1
        case 0b1111: return read_PRA(has_effect, false);
    }
    return old;
}

u8 chip::read_PRA(bool has_effect, bool handshake) {
    u8 out = pins.PRA_out &  regs.DDRA;
    u8 in  = pins.PRA_in  & ~regs.DDRA;
    if (has_effect) {
        regs.IFR &= ~IFR_CA1;
        if (handshake) regs.IFR &= ~IFR_CA2;
        update_IRQs();
    }
    return out | in;
}

u8 chip::read_PRB(bool has_effect) {
    u8 out = pins.PRB_out &  regs.DDRB;
    u8 in  = pins.PRB_in  & ~regs.DDRB;
    u8 v = out | in;
    // ACR bit 7: T1 output on PB7
    if (regs.ACR & 0x80)
        v = (v & 0x7F) | (timerA.out << 7);
    if (has_effect) {
        regs.IFR &= ~IFR_CB1;
        update_IRQs();
        pins.PC = 1;
        PC_count = 1;
    }
    return v;
}

void chip::write(u8 addr, u8 val) {
    addr &= 15;
    switch(addr) {
        case 0b0000: write_PRB(val); return;
        case 0b0001: write_PRA(val); return;
        case 0b0010: write_DDRB(val); return;
        case 0b0011: write_DDRA(val); return;
        case 0b0100: // T1C-L: write to latch only
            timerA.latch = (timerA.latch & 0xFF00) | val;
            return;
        case 0b0101: // T1C-H: write latch, load counter, start timer, clear T1 flag
            timerA.latch = (timerA.latch & 0x00FF) | ((u16)val << 8);
            timerA.count = timerA.latch;
            if (regs.ACR & 0x80) timerA.out = 0; // PB7 goes low on timer load
            regs.T1_running = true;
            regs.IFR &= ~IFR_T1;
            update_IRQs();
            return;
        case 0b0110: // T1L-L: write to latch only (no counter load)
            timerA.latch = (timerA.latch & 0xFF00) | val;
            return;
        case 0b0111: // T1L-H: write to latch only (no counter load)
            timerA.latch = (timerA.latch & 0x00FF) | ((u16)val << 8);
            return;
        case 0b1000: // T2C-L: write to latch only
            timerB.latch = (timerB.latch & 0xFF00) | val;
            return;
        case 0b1001: // T2C-H: write latch, load counter, start timer, clear T2 flag
            timerB.latch = (timerB.latch & 0x00FF) | ((u16)val << 8);
            timerB.count = timerB.latch;
            regs.T2_running = true;
            regs.IFR &= ~IFR_T2;
            update_IRQs();
            return;
        case 0b1010: write_SR(val); return;
        case 0b1011: write_ACR(val); return;
        case 0b1100: write_PCR(val); return;
        case 0b1101: write_IFR(val); return;
        case 0b1110: write_IER(val); return;
        case 0b1111: write_PRA(val); return; // ORA, no handshake
    }
}

void chip::write_PRA(u8 val) {
    pins.PRA_out = val;
}

void chip::write_PRB(u8 val) {
    pins.PRB_out = val;
    pins.PC = 1;
    PC_count = 1;
}

void chip::write_DDRA(u8 val) { regs.DDRA = val; }
void chip::write_DDRB(u8 val) { regs.DDRB = val; }

void chip::write_ACR(u8 val) { regs.ACR = val; }
void chip::write_PCR(u8 val) { regs.PCR = val; }

void chip::write_SR(u8 val) { regs.SR = val; }

void chip::write_IFR(u8 val) {
    // writing a 1 to a bit clears that flag; bit 7 not directly writable
    regs.IFR &= ~(val & 0x7F);
    update_IRQs();
}

void chip::write_IER(u8 val) {
    // bit 7 = 1: set the written bits; bit 7 = 0: clear them
    if (val & 0x80) regs.IER |=  (val & 0x7F);
    else            regs.IER &= ~(val & 0x7F);
    update_IRQs();
}

void chip::reset() {
    pins.SP = 1;
}

void chip::tick_A() {
    if (!timerA.count--) {
        timerA.count = timerA.latch;
        // ACR bits 7-6: T1 mode
        //   00 = one-shot, no PB7
        //   01 = free-run, no PB7
        //   10 = one-shot + PB7 output (goes high on underflow)
        //   11 = free-run + PB7 square wave (toggle on underflow)
        if (regs.ACR & 0x80) {
            if (regs.ACR & 0x40) timerA.out ^= 1; // free-run: toggle PB7
            else                  timerA.out = 1;  // one-shot: PB7 goes high
        }
        regs.IFR |= IFR_T1;
        update_IRQs();
        // one-shot (ACR bit 6 = 0): stop after first underflow
        if (!(regs.ACR & 0x40)) regs.T1_running = false;
    }
}

void chip::tick_B() {
    if (!timerB.count--) {
        regs.IFR |= IFR_T2;
        update_IRQs();
        regs.T2_running = false; // T2 is always one-shot
    }
}

void chip::update_IRQs() {
    if (regs.IFR & regs.IER & 0x7F) regs.IFR |=  IFR_ANY;
    else                              regs.IFR &= ~IFR_ANY;
    set_IRQ_pin((regs.IFR & IFR_ANY) ? 1 : 0);
}

void chip::set_IRQ_pin(u8 val) {
    u8 old = pins.IRQ;
    pins.IRQ = val;
    if (old != val && update_irq.func) update_irq.func(update_irq.ptr, update_irq.device_num, val);
}

void chip::cycle() {
    // PC pin pulses for one clock after a Port B read or write
    pins.PC &= PC_count--;

    // CA1 (FLAG pin) edge detection ... IFR_CA1
    // PCR bit 0: 0 = active on negative edge, 1 = active on positive edge
    bool flag_edge = (regs.PCR & 0x01) ? (pins.FLAG && !old_FLAG)
                                        : (!pins.FLAG && old_FLAG);
    if (flag_edge) { regs.IFR |= IFR_CA1; update_IRQs(); }
    old_FLAG = pins.FLAG;

    bool cnt_edge = pins.CNT && !old_CNT;
    old_CNT = pins.CNT;

    // Timer A: always counts phi2 pulses (no external clock source in 6522)
    if (regs.T1_running) tick_A();

    // Timer B: ACR bit 5 = 0: count phi2, 1: count PB6 low-to-high transitions
    // PB6 counting uses CNT as proxy until PB6 edge detection is wired up
    if (regs.T2_running) {
        if (!(regs.ACR & 0x20) || cnt_edge) tick_B();
    }
}

}
