//
// Created by . on 3/28/26.
//

#include "pv1000_bus.h"

#include "system/sms_gg/sms_gg_bus.h"

namespace CASIO_PV1000 {
void core::cycle_z80() {
    if (io.z80.bus_request && io.z80.bus_ack) {
        return;
    }

    cpu.cycle<true>();
    if (cpu.pins.RD) {
        if (cpu.pins.MRQ) {
            cpu.pins.D = mainbus_read(cpu.pins.Addr, cpu.pins.D, true);
        }
        else if (cpu.pins.IO && (cpu.pins.M1 == 0)) {
            // All Z80 IO requests return 0xFF
            cpu.pins.D = mainbus_in(cpu.pins.Addr, cpu.pins.D, true);
        }
    }
    else if (cpu.pins.WR) {
        if (cpu.pins.MRQ) {
            // All Z80 IO requests are ignored
            mainbus_write(cpu.pins.Addr, cpu.pins.D);
        }
        else if (cpu.pins.IO) {
            mainbus_out(cpu.pins.Addr, cpu.pins.D);
        }
    }
    // Bus request/ack at end of cycle
    io.z80.bus_ack = io.z80.bus_request;
}

static u32 read_trace_z80(void *ptr, u32 addr) {
    auto *th = static_cast<core *>(ptr);
    return th->mainbus_read(addr, th->cpu.pins.D, false);
}

static void vdp_set_irq(void *ptr, bool val) {
    auto *th = static_cast<core *>(ptr);
    th->cpu.notify_IRQ(val);
}

static void vdp_set_busreq(void *ptr, bool val) {
    auto *th = static_cast<core *>(ptr);
    th->io.z80.bus_request = val;
}

static u8 vdp_read_mem(void *ptr, u16 addr) {
    auto *th = static_cast<core *>(ptr);
    return th->mainbus_read(addr, 0xFF, true);
}

static u8 vdp_joy_read(void *ptr) {
    auto *th = static_cast<core *>(ptr);
    u8 v = th->controller1.read();
    v |= th->controller2.read() << 2;
    return v;
}

static void vdp_joy_write(void *ptr, u8 val) {
    auto *th = static_cast<core *>(ptr);
    if (th->controller1.connected) th->controller1.write(val);
    if (th->controller2.connected) th->controller2.write(val);
}

core::core() {
    has.set_audio_ring = true;
    has.load_BIOS = false;
    has.save_state = false;
    jsm.described_inputs = false;

    vdp.set_irq = &vdp_set_irq;
    vdp.set_busreq = &vdp_set_busreq;
    vdp.read_mem = &vdp_read_mem;
    vdp.joy_read = &vdp_joy_read;
    vdp.joy_write = &vdp_joy_write;
    vdp.callback_ptr = static_cast<void *>(this);
    snprintf(label, sizeof(label), "Casio PV-1000");

    jsm_debug_read_trace dt;
    dt.read_trace = &read_trace_z80;
    dt.ptr = static_cast<void *>(this);
    cpu.setup_tracing(&dt, &clock.master_cycle_count);
}

u8 core::mainbus_read(u16 addr, u8 old, bool has_effect) {
    if (addr < 0x8000) {
        return ROM.ptr[addr % ROM.sz];
    }
    if ((addr >= 0xB800) && (addr < 0xC000)) {
        return RAM[addr & 0x7FF];
    }
    //if (has_effect) printf("\nUnhandled read from %04x: %02x", addr, old);
    return old;
}

void core::mainbus_write(u16 addr, u8 val) {
    if (addr < 0x8000) return; // Cart!
    if ((addr >= 0xB800) && (addr < 0xC000)) {
        RAM[addr& 0x7FF] = val;
        return;
    }
    printf("\nUnhandled write to %04x: %02x", addr, val);
}

u8 core::mainbus_in(u16 addr, u8 old, bool has_effect) {

    addr &= 0xFF;
    addr |= 0xF8;
    switch (addr) {
        case 0xF8:
        case 0xF9:
        case 0xFA: // PSG
            return io.regs[addr & 7];
        case 0xFB:
        case 0xFC:
        case 0xFD:
        case 0xFE:
        case 0xFF:
            return vdp.read(addr & 0x7, old, has_effect);
    }
    if (has_effect) printf("\nUnhandled IN from %02x: %02x", addr, io.regs[addr & 7]);
    return io.regs[addr & 7];
}

void core::mainbus_out(u16 addr, u8 val) {
    addr &= 0xFF;
    addr |= 0xF8;
    io.regs[addr & 7] = val;
    switch (addr) {
        case 0xF8:
        case 0xF9:
        case 0xFA: // PSG
        case 0xFB:
        case 0xFC:
        case 0xFD: // joy
        case 0xFE: // VDP not used write
        case 0xFF:
            vdp.write(addr & 7, val);
            return;
    }
    printf("\nUnhandled OUT to %02x: %02x", addr, val);
}

}
