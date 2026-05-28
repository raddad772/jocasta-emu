//
// Created by . on 5/11/26.
//
#include <cassert>

#include "ng_bus.h"
#include "ng_debugger.h"

#include "helpers/setbits.h"

namespace NEOGEO {

void core::eval_z80_IRQs() {
    z80.notify_NMI(io.z80.nmi_enable && io.z80.nmi_line);
}

template<bool do_debug, bool peek>
u8 core::z80_IO_read(u32 addr, u8 old) {
    switch (addr & 0xFF) {
        case 0x00: {
            if constexpr (!peek) {
                io.z80.nmi_line = false;
                eval_z80_IRQs();
            }
            return io.z80.m68k_byte;
        }
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            return ym2610.read<do_debug, peek>(addr & 3);
        case 0x08:
            if constexpr(!peek) {
                io.z80.windows[0] = ((addr >> 8) & 0xFF) << 11;
            }
            return 0;
        case 0x09:
            if constexpr(!peek) {
                io.z80.windows[1] = ((addr >> 8) & 0xFF) << 12;
            }
            return 0;
        case 0x0A:
            if constexpr(!peek) {
                io.z80.windows[2] = ((addr >> 8) & 0xFF) << 13;
            }
            return 0;
        case 0x0B:
            if constexpr(!peek) {
                io.z80.windows[3] = ((addr >> 8) & 0xFF) << 14;
            }
            return 0;
        case 0x0C:
            return 0;
    }
    return 0xFF;
}

template<bool do_debug>
void core::z80_IO_write(u32 addr, u8 val) {
    switch(addr & 0x1F) {
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
            io.z80.nmi_enable = true;
            eval_z80_IRQs();
            break;
        case 0x18:
            io.z80.nmi_enable = false;
            eval_z80_IRQs();
            break;
    }

    switch(addr & 15) {
        case 0x00:
            io.z80.m68k_byte = 0;
            return;
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            ym2610.write<do_debug>(addr & 3, val);
            return;
        case 0x0C:
            io.m68k.z80_byte = val;
            return;
    }
}

template<bool do_debug, bool peek>
u8 core::z80_bus_read(u32 addr, u8 old) {
    if (addr < 0x8000) return cart.read_M(addr);
    if (addr < 0xC000) { // switching window 3, 16kb. 8000-BFFF
        addr &= 0x3FFF;
        addr |= io.z80.windows[3];
        return cart.read_M(addr);
    }
    if (addr < 0xE000) { // switching window 2, 8kb. C000-DFFF
        addr &= 0x1FFF;
        addr |= io.z80.windows[2];
        return cart.read_M(addr);
    }
    if (addr < 0xF000) { // switching window 1, 4kb. E000-EFFF
        addr &= 0xFFF;
        addr |= io.z80.windows[1];
        return cart.read_M(addr);
    }
    if (addr < 0xF800) { // switcing window 0. 2kb. F000-F7FF
        addr &= 0x7FF;
        addr |= io.z80.windows[0];
        return cart.read_M(addr);
    }
    return z80_RAM[addr & 0x7FF];
}

template<bool do_debug>
void core::z80_bus_write(u32 addr, u8 val) {
    if (addr >= 0xF800)
        z80_RAM[addr & 0x7FF] = val;
}

static constexpr u32 UDS_mask[4] = { 0, 0xFF, 0xFF00, 0xFFFF };

#define UDSMASK UDS_mask[((UDS) << 1) | (LDS)]

#define MRA(regaddr, maskby, mask_check) if (((addr & 0x##maskby ) == 0x##regaddr ) && (mask_check & mask))
#define MR(regaddr, maskby, mask_check) else if (((addr & 0x##maskby ) == 0x##regaddr ) && (mask_check & mask))
#define upper 0xFF00
#define lower 0x00FF
#define either 0xFFFF

template<bool do_debug, bool peek>
u16 core::mainbus_mmio_read(u32 addr, u16 mask, u16 old) {
    MRA(300000, FE0000, upper) {
        setbyte<1>(old, controllerport1.read_buttons());
    }

    MR(300000, FE0080, lower) {
        setbyte<0>(old, 0b11101111);
        // REG_DIPSW.
    }

    MR(300080, FE0080, lower) {
        setbit<6>(old, 0); // 0 = 2 slots, 1 = 4-6 slots
        setbit<7>(old, 1); // test button
    }

    MR(320000, FE0000, upper) {
        setbyte<1>(old, io.m68k.z80_byte);
    }

    // REG_STATUS_A
    MR(320000, FE0000, lower) {
        setbit<0>(old, is_MVS); // coin 1 active low MVS, AES 0 (same 0,1,3,4)
        setbit<1>(old, is_MVS);
        setbit<2>(old, 1); // service button
        setbit<3>(old, is_MVS); // coin 3 active low MVS, AES 0 (same 0,1,3,4)
        setbit<4>(old, is_MVS);
        setbit<5>(old, 0); // 0 = 4-slit, 1 6-slot
        setbit<6>(old, 0); // RTC time pulse
        setbit<7>(old, 0); // RTC data bit
    }

    MR(340000, FE0000, upper) {
        setbyte<1>(old, controllerport2.read_buttons());
    }

    MR(380000, FE0000, upper) {
        setbits<8, 9>(old, controllerport1.read_controls());
        setbits<10, 11>(old, controllerport2.read_controls());
        setbits<12, 13>(old, 0b11); // 0b00 = mem card inserted
        setbit<14>(old, card_slot.lock != 0);
        setbit<15>(old, is_MVS); // 0 = AES, 1 = MVS
    }

    MR(3A0000, FE001E, lower) {
    }

    MR(3C0000, FE0006, either) {
        old = read_VRAM(io.vram_addr);
    }

    MR(3C0002, FE0006, either) {
        old = read_VRAM(io.vram_addr);
    }

    MR(3C0004, FE0006, either) {
        old = io.vram_inc;
    }

    MR(3C0006, FE0006, either) {
        setbits<0, 2>(old, lpsc.auto_animation.frame);
        setbit<3>(old, 0); // 0 = 60hz, 1 =50hz
        setbits<4, 6>(old, 0); // empty
        setbits<7, 15>(old, 0xF8 + lpsc.raster_line_counter);
    }
    else {
    printf("\nUNSERVICED MMIO READ FROM %08x(%04x)", addr, mask);
    }
    return old & mask;
}
#undef MRA
#undef MR
#define MR(regaddr, maskby, mask_check) if (((addr & 0x##maskby ) == 0x##regaddr ) && (mask_check & mask))
template<bool do_debug>
void core::mainbus_mmio_write(u32 addr, u16 mask, u16 val) {
    MR(300000, FE0080, lower) {
        // kick watchdog, which we don't really impl.
        return;
    }
    MR(320000, FE0000, upper) { // REG_SOUND
        io.z80.m68k_byte = getbyte<1>(val);
        io.z80.nmi_line = true;
        eval_z80_IRQs();
        return;
    }
    MR(320000, FE0000, lower) {
        return;
    }
    MR(380000, FE0070, lower) { // REG_POUTPUT
        controllerport1.write_outputs(getbits<0,2>(val));
        controllerport2.write_outputs(getbits<3,5>(val));
        return;
    }
    MR(380010, FE0070, lower) {
        card_slot.bank = getbits<0,2>(val);
        return;
    }
    MR(380020, FE00F0, lower) { // REG_POUTPUT mirror on AES, slot select on MVS
        if (!is_MVS) {
            controllerport1.write_outputs(getbits<0,2>(val));
            controllerport2.write_outputs(getbits<3,5>(val));
        }
        else {
            io.mvs.slot_select = getbits<0, 3>(val);
        }
        return;
    }
    MR(380030, FE00F0, lower) {
        io.led_marquee = getbit<3>(val);
        io.led_latch1 = getbit<4>(val);
        io.led_latch2 = getbit<5>(val);
        return;
    }
    MR(380040, FE00F0, lower) {
        io.led_data = val & 0xFF;
        return;
    }
    MR(380050, FE00F0, lower) {
        // RTC stuff ?
        return;
    }
    MR(380060, FE0F6, lower) {
        // float coin counter 1
        return;
    }
    MR(380062, FE00F6, lower) {
        // float coin counter 2
        return;
    }
    MR(380064, FE00F6, lower) {
        // float coin lockout 1
        return;
    }
    MR(380066, FE00F6, lower) {
        // float coin lockout 2
        return;
    }
    MR(3800E0, FE00F6, lower) {
        // sink coin counter 1
        return;
    }
    MR(3800E2, FE00F6, lower) {
        // sink coin counter 2
        return;
    }
    MR(3800E4, FE00F6, lower) {
        // sink coin lockout 1
        return;
    }
    MR(3800E6, FE00F6, lower) {
        // sink coin lockout 2
        return;
    }
    MR(3A0000, FE001E, lower) {
        lpsc.io.shadow = 0;
        return;
    }
    MR(3A0010, FE001E, lower) {
        lpsc.io.shadow = 1;
        return;
    }
    MR(3A0002, FE001E, lower) {
        io.vector_select = 0;
        return;
    }
    MR(3A0012, FE001E, lower) {
        io.vector_select = 1;
        return;
    }
    MR(3A0004, FE001E, lower) {
        // card slot lock bit 0 = 0;
        setbit<0>(card_slot.lock, 0);
        return;
    }
    MR(3A0014, FE001E, lower) {
        // card slot lock bit 0 = 1;
        printf("\nWARN UNIMP. CARD SLOT1");
        setbit<0>(card_slot.lock, 1);
        return;
    }
    MR(3A0006, FE001E, lower) {
        printf("\nWARN UNIMP. CARD SLOT2");
        // card slot lock bit 1 = 1
        setbit<1>(card_slot.lock, 1);
        return;
    }
    MR(3A0016, FE001E, lower) {
        printf("\nWARN UNIMP. CARD SLOT3");
        setbit<1>(card_slot.lock, 0);
        return;
    }
    MR(3A0008, FE001E, lower) {
        printf("\nWARN UNIMP. CARD SLOT4");
        // card slot select = 1
        return;
    }
    MR(3A0018, FE001E, lower) {
        printf("\nWARN UNIMP. CARD SLOT5");
        // card slot select = 0
        return;
    }
    MR(3A000A, FE001E, lower) {
        io.fix_select = 0;
        return;
    }
    MR(3A001A, FE001E, lower) {
        io.fix_select = 1;
        return;
    }
    MR(3A000C, FE001E, lower) {
        io.sram_lock = 1;
        return;
    }
    MR(3A001C, FE001E, lower) {
        io.sram_lock = 0;
        return;
    }
    MR(3A000E, FE001E, lower) {
        io.pal_base_offset = 0x1000;
        return;
    }
    MR(3A001E, FE001E, lower) {
        io.pal_base_offset = 0;
        return;
    }
    MR(3C0000, FE000E, either) {
        u32 old_addr = io.vram_addr;
        if (mask & upper) setbyte<1>(io.vram_addr, getbits<8,15>(val));
        if (mask & lower) setbyte<0>(io.vram_addr, getbits<0, 7>(val));
        if constexpr(do_debug) {
            dbgloglog(NG_CAT_LSPC_VRAM, DBGLS_TRACE,
                      "LSPC VRAMADDR bus:%06x mask:%04x data:%04x old:%04x new:%04x",
                      addr, mask, val, old_addr, io.vram_addr);
        }
        return;
    }
    MR(3C0002, FE000E, either) {
        u32 old_addr = io.vram_addr;
        u32 old_word = read_VRAM(old_addr);
        write_VRAM(old_addr, val, mask);
        u32 new_word = read_VRAM(old_addr);
        setbits<0,14>(io.vram_addr, getbits<0,14>(io.vram_addr + io.vram_inc));
        if constexpr(do_debug) {
            dbgloglog(NG_CAT_LSPC_VRAM, DBGLS_TRACE,
                      "LSPC VRAMWRITE bus:%06x mask:%04x addr:%04x data:%04x old:%04x new:%04x inc:%04x next:%04x",
                      addr, mask, old_addr, val, old_word, new_word, io.vram_inc, io.vram_addr);
        }
        return;
    }
    MR(3C0004, FE000E, either) {
        u32 old_inc = io.vram_inc;
        if (mask & upper) setbyte<1>(io.vram_inc, getbyte<1>(val));
        if (mask & lower) setbyte<0>(io.vram_inc, getbyte<0>(val));
        if constexpr(do_debug) {
            dbgloglog(NG_CAT_LSPC_VRAM, DBGLS_TRACE,
                      "LSPC VRAMMOD bus:%06x mask:%04x data:%04x old:%04x new:%04x",
                      addr, mask, val, old_inc, io.vram_inc);
        }
        return;
    }
    MR(3C0006, FE000E, either) {
        if (mask & upper) {
            lpsc.auto_animation.reload = getbyte<1>(val);
        }
        if (mask & lower) {
            lpsc.auto_animation.disable = getbit<3>(val);
            lpsc.timer.irq_enable = getbit<4>(val);
            lpsc.timer.reload_on_change = getbit<5>(val);
            lpsc.timer.reload_on_vblank = getbit<6>(val);
            lpsc.timer.reload_on_zero = getbit<7>(val);
        }
        return;
    }

    MR(3C0008, FE000E, either) {
        if (mask & upper) setbyte<3>(lpsc.timer.reload, getbyte<1>(val));
        if (mask & lower) setbyte<2>(lpsc.timer.reload, getbyte<0>(val));
        return;
    }

    MR(3C000A, FE000E, either) {
        if (mask & upper) setbyte<1>(lpsc.timer.reload, getbyte<1>(val));
        if (mask & lower) setbyte<0>(lpsc.timer.reload, getbyte<0>(val));
        if (lpsc.timer.reload_on_change) lpsc.timer.counter = lpsc.timer.reload;
        return;
    }

    MR(3C000C, FE000E, lower) {
        lpsc.irq_ack.reset = getbit<0>(val);
        lpsc.irq_ack.timer = getbit<1>(val);
        lpsc.irq_ack.vblank = getbit<2>(val);
        return;
    }

    MR(3C000E, FE000E, lower) {
        lpsc.timer.stop = getbit<0>(val);
        return;
    }

    printf("\nUNSERVICED MMIO WRITE TO %08x(%04x): %04x", addr, mask, val);
}
#undef MR
#undef upper
#undef lower

template<bool do_debug, bool peek>
u16 core::read_memcard(u32 addr, u16 mask, u16 old) {
    printf("\nUNSERVICED MMIO READ FROM %08x(%04x)", addr, mask);
    return old & mask;
}

template<bool do_debug>
void core::write_memcard(u32 addr, u16 mask, u16 val) {
    printf("\nUNSERVICED MMIO WRITE TO %08x(%04x): %04x", addr, mask, val);
}


template<bool do_debug, bool peek>
u16 core::mainbus_read(u32 addr, u8 UDS, u8 LDS, u16 old) {
    //NEO-E0
    // TODO: add access timings
    u32 mask = UDSMASK;
    u8 top4 = (addr >> 20) & 0xF;
    if constexpr (!peek) io.m68k.cycles_til_DTACK = 1;
    u16 v;
    switch (top4) {
        case 0x00: // P1 ROM
            if constexpr (!peek) io.m68k.cycles_til_DTACK += io.ROMWAIT_delay;
            return cart.read_P<0>(addr) & mask;
        case 0x1: // Work RAM
            addr &= 0xFFFF;
            return RAM[addr >> 1];
        case 0x2: // P2 ROM
            if constexpr (!peek) io.m68k.cycles_til_DTACK += io.PORTwait_delay;
            return cart.read_P<1>(addr) & mask;
        case 0x3: // MMIO
            return mainbus_mmio_read<do_debug, peek>(addr, mask, old);
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            addr &= 0x1FFF;
            return PRAM[(addr >> 1) + io.pal_base_offset] & mask;
        case 0x8:
        case 0x9:
        case 0xA:
        case 0xB:
            if constexpr (!peek) io.m68k.cycles_til_DTACK += 2;
            return read_memcard<do_debug, peek>(addr, mask, old);
        case 0xC:
            addr &= 0x1'FFFF;
            if ((ROMs.BIOS.ptr == nullptr) || (ROMs.BIOS.sz < 2)) {
                return old & mask;
            }
            addr %= ROMs.BIOS.sz;
            v = ROMs.BIOS.ptr[addr];
            v |= ROMs.BIOS.ptr[(addr + 1) % ROMs.BIOS.sz] << 8;
            return v & mask;
        case 0xD:
            if (is_MVS) return MVS_backup_RAM[addr >> 1] & mask;
    }
    printf("\nUNKNOWN READ TO %08x", addr);
    return old & mask;
}

template<bool do_debug>
void core::mainbus_write(u32 addr, u8 UDS, u8 LDS, u16 val) {
    u8 top4 = (addr >> 20) & 0xF;
    u32 mask = UDSMASK;
    io.m68k.cycles_til_DTACK = 1;
    switch (top4) {
        //case 0x0:
            //printf("|NBIOS write")
        case 0x1:
            addr &= 0xFFFF;
            RAM[addr>>1] = (RAM[addr>>1] & ~mask) | (val & mask);
            return;
        case 0x2:
            cart.write_P(addr, val);
            return;
        case 0x3:
            mainbus_mmio_write<do_debug>(addr, mask, val);
            return;
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            addr &= 0x1FFF;
            addr = (addr >> 1) + io.pal_base_offset;
            PRAM[addr] = (PRAM[addr] & ~mask) | (val & mask);
            return;
        case 0x8:
        case 0x9:
        case 0xA:
        case 0xB:
            io.m68k.cycles_til_DTACK += io.PORTwait_delay;
            write_memcard<do_debug>(addr, mask, val);
            return;
        case 0xD:
            if (is_MVS) {
                addr &= 0xFFFF;
                MVS_backup_RAM[addr >> 1] = (MVS_backup_RAM[addr >> 1] & ~mask) | (val & mask);
                return;
            }
        default:
            break;
    }
    printf("\nWARN ATTEMPT WRITE TO %06x: %04x", addr, val);
}


u32 read_trace_z80(void *ptr, u32 addr) {
    auto *th = static_cast<core *>(ptr);
    return th->z80_bus_read<false, true>(addr, th->z80.pins.D);
}

u32 read_trace_m68k(void *ptr, u32 addr, u32 UDS, u32 LDS) {
    auto *th = static_cast<core *>(ptr);
    if(!th->io.vector_select) {
        if((addr & 0xFF'FF80) == 0x00'0000 || (addr & 0xFF'FF80) == 0xC0'0000) {
            addr ^= 0xC0'0000;
        }
    }
    return th->mainbus_read<false, true>(addr, UDS, LDS, th->io.m68k.open_bus_data);
}

template<bool do_debug>
void core::cycle_m68k() {
    /*if constexpr(do_debug) {
        if (io.m68k.stuck) printf("\nSTUCK cyc %lld", *m68k.trace.cycles);
    }*/
    if (io.m68k.cycles_til_DTACK > 0) {
        if (!--io.m68k.cycles_til_DTACK) {
            m68k.pins.DTACK = 1;
            /*if constexpr(do_debug) {
                dbgloglogn(NG_CAT_M68K_BUSRW, DBGLS_TRACE, "DTACK UP");
            }*/
        }
    }
    m68k.cycle<do_debug>();
    if (m68k.pins.FC == 7) {
        // Auto-vector interrupts!
        m68k.pins.VPA = 1;
        return;
    }
    if (m68k.pins.AS && (!m68k.pins.DTACK) && (!io.m68k.stuck)) {
        if (!m68k.pins.RW) { // read
            u32 addr = m68k.pins.Addr;
            if(!io.vector_select) {
                if((addr & 0xFF'FF80) == 0x00'0000 || (addr & 0xFF'FF80) == 0xC0'0000) {
                    addr ^= 0xC0'0000;
                }
            }
            io.m68k.open_bus_data = m68k.pins.D = mainbus_read<do_debug, false>(addr, m68k.pins.UDS, m68k.pins.LDS, io.m68k.open_bus_data);

            if constexpr(do_debug) {
                dbgloglog(NG_CAT_M68K_BUSRW, DBGLS_TRACE, "(%06x)  read%d%d  %04x", addr, m68k.pins.UDS, m68k.pins.LDS, m68k.pins.D);
            }
        }
        else { // write
            if constexpr(do_debug) {
                dbgloglog(NG_CAT_M68K_BUSRW, DBGLS_TRACE, "(%06x)  write%d%d %04x", m68k.pins.Addr, m68k.pins.UDS, m68k.pins.LDS, m68k.pins.D);
            }
            mainbus_write<do_debug>(m68k.pins.Addr, m68k.pins.UDS, m68k.pins.LDS, m68k.pins.D);
        }
    }
    assert(io.m68k.stuck == 0);
}

void core::reset() {
    z80.reset();
    io.z80.nmi_enable = false;
    m68k.reset();
    ym2610.reset();
    lpsc.reset();
    cart.reset();
    io.ROMWAIT = true;
    io.PORTwait = 3;
    io.ROMWAIT_delay = 0;
    io.PORTwait_delay = 0;
    io.pal_base_offset = 0;
    io.z80.windows[0] = 0x1E << 11;
    io.z80.windows[1] = 0x0E << 12;
    io.z80.windows[2] = 0x06 << 13;
    io.z80.windows[3] = 0x02 << 14;
    io.vector_select = 0;

    master_clock = 0;
    clock.z80_cycle = clock.m68k_cycle = 0;
    audio.next_sample_cycle = 0;
    audio.debug_generation = 0;

    scheduler.clear();
    schedule_first();
}


template<bool do_debug>
void core::cycle_z80()
{
    if (io.z80.reset_line) {
        io.z80.reset_line_count++;
        if (io.z80.reset_line_count >= 3) return; // If it's held down 3 or more, freeze!
    }
    io.z80.reset_line_count = 0;

    z80.cycle<do_debug>();
    if (z80.pins.RD) {
        if (z80.pins.MRQ) {
            z80.pins.D = z80_bus_read<do_debug, false>(z80.pins.Addr, z80.pins.D);
            if constexpr (do_debug) {
                dbgloglog(NG_CAT_Z80_BUSRW, DBGLS_TRACE, "(%04x)  read  %02x", z80.pins.Addr, z80.pins.D);
            }
        }
        else if (z80.pins.IO && (z80.pins.M1 == 0)) {
            z80.pins.D = z80_IO_read<do_debug, false>(z80.pins.Addr, z80.pins.D);
            if constexpr (do_debug) {
                dbgloglog(NG_CAT_Z80_BUSRW, DBGLS_TRACE, "(%04x)  IN    %02x", z80.pins.Addr, z80.pins.D);
            }
        }
    }
    else if (z80.pins.WR) {
        if (z80.pins.MRQ) {
            dbgloglog(NG_CAT_Z80_BUSRW, DBGLS_TRACE, "(%04x)  WRITE %02x", z80.pins.Addr, z80.pins.D);
            z80_bus_write<do_debug>(z80.pins.Addr, z80.pins.D);
        }
        else if (z80.pins.IO) {
            dbgloglog(NG_CAT_Z80_BUSRW, DBGLS_TRACE, "(%04x)  OUT   %02x", z80.pins.Addr, z80.pins.D);
            z80_IO_write<do_debug>(z80.pins.Addr, z80.pins.D);
        }
    }
    // Bus request/ack at end of cycle
    //io.z80.bus_ack = io.z80.bus_request;
}

template<bool do_debug>
static u8 ym2610_read_a(void *ptr, u32 addr) {
    auto *th = static_cast<core *>(ptr);
    return th->cart.read_V(addr);
}

template<bool do_debug>
static u8 ym2610_read_b(void *ptr, u32 addr) {
    auto *th = static_cast<core *>(ptr);
    return th->cart.read_V(addr);
}

template<bool do_debug>
static void do_run_m68k(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->cycle_m68k<do_debug>();
    th->clock.m68k_cycle += M68K_DIV;
    th->scheduler.only_add_abs(th->clock.m68k_cycle, 0, th, &do_run_m68k<false>, &do_run_m68k<true>, nullptr);
}

template<bool do_debug>
static void do_run_z80(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->cycle_z80<do_debug>();
    th->clock.z80_cycle += Z80_DIV;
    th->scheduler.only_add_abs(th->clock.z80_cycle, 0, th, &do_run_z80<false>, &do_run_z80<true>, nullptr);
}


static void do_run_ym2610(void *ptr, u64 key, u64 clock_val, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    u64 cur = clock_val - jitter;
    th->scheduler.only_add_abs(cur + YM2610_FM_DIV, 0, th, &do_run_ym2610, nullptr);
    th->ym2610.cycle();
    th->ym2610.push_samples();
}

static void do_run_ssg(void *ptr, u64 key, u64 clock_val, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    u64 cur = clock_val - jitter;
    th->scheduler.only_add_abs(cur + YM2610_SSG_SAMPLE_DIV, 0, th, &do_run_ssg, nullptr);

    th->ym2610.ssg.cycle_div_16();
    if (th->audio.ssg_ring) {
        float s = static_cast<float>(th->ym2610.ssg.mix_sample(false)) / 8192.0f;
        if (!th->ym2610.ext_enable) s = 0.0f;
        if (s < -1.0f) s = -1.0f;
        if (s >  1.0f) s =  1.0f;
        th->audio.ssg_ring->push(s, s);
    }
}

static void do_run_ym2610_timers(void *ptr, u64 key, u64 clock_val, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    u64 cur = clock_val - jitter;
    th->scheduler.only_add_abs(cur + YM2610_TIMER_DIV, 0, th, &do_run_ym2610_timers, nullptr);
    th->ym2610.cycle_timers();
}

void core::schedule_first()
{
    scheduler.only_add_abs(M68K_DIV, 0, this, &do_run_m68k<false>, &do_run_m68k<true>, nullptr);
    scheduler.only_add_abs(Z80_DIV, 0, this, &do_run_z80<false>, &do_run_z80<true>, nullptr);
    scheduler.only_add_abs(YM2610_FM_DIV, 0, this, &do_run_ym2610, nullptr);
    scheduler.only_add_abs(YM2610_TIMER_DIV, 0, this, &do_run_ym2610_timers, nullptr);
    scheduler.only_add_abs(YM2610_SSG_SAMPLE_DIV, 0, this, &do_run_ssg, nullptr);
    lpsc.schedule_first();

}

static void ym2610_irq_update(void *ptr, bool level) {
    auto *th = static_cast<core *>(ptr);
    th->z80.notify_IRQ(level);
}

core::core(jsm::systems variant) :
    lpsc{this}
    {
    has.set_audio_ring = true;
    has.load_BIOS = true;
    has.save_state = false;

    if (variant == jsm::NEOGEO_MVS) is_MVS = true;

    if (is_MVS) {
        snprintf(label, sizeof(label), "Neo Geo MVS");
        clock.cycles_per_second = 24'000'000;   // MVS: 24.000 MHz ... 59.1856 Hz
    }
    else {
        snprintf(label, sizeof(label), "Neo Geo AES");
        clock.cycles_per_second = 24'167'829;   // AES: 24.167829 MHz ... 59.599 Hz
    }
    // fps = clock / (pixel_div=4 * pixels_per_line=384 * lines_per_frame=264)
    clock.cycles_per_frame = 4 * 384 * 264;  // = 405504, same count for both variants
    clock.fps = static_cast<double>(clock.cycles_per_second) / static_cast<double>(clock.cycles_per_frame);

    jsm_debug_read_trace dt;
    dt.read_trace = &read_trace_z80;
    dt.read_trace_m68k = &read_trace_m68k;
    dt.ptr = static_cast<void *>(this);

    m68k.setup_tracing(&dt, &master_clock);
    z80.setup_tracing(&dt, &master_clock);

    ym2610.irq_ptr = this;
    ym2610.update_IRQs = &ym2610_irq_update;
    ym2610.mem_ptr = this;
    ym2610.adpcm_a_read = &ym2610_read_a<false>;
    ym2610.adpcm_a_read_debug = &ym2610_read_a<true>;
    ym2610.adpcm_b_read = &ym2610_read_b<false>;
    ym2610.adpcm_b_read_debug = &ym2610_read_b<true>;
    ym2610.scheduler_divider = YM2610_DIV;
}
}
