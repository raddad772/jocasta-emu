//
// Created by . on 8/30/24.
//

#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cstring>

#include "apple2_bus.h"
namespace apple2 {
void MMU::aux_card_RAMwrite(u32 addr, u8 val)
{
    AUX_RAM.ptr[addr] = val;
}

u8 MMU::read_ROM(u32 addr)
{
    return ROM.ptr[addr & ROM.mask];
}

u8 MMU::slot_read(u32 addr, u8 old_val)
{
    u32 sn = (addr >> 8) & 0xF;
    if (sn >= 1 && sn <= 7) {
        auto& s = bus->slots[sn];
        if (s && s->has_rom()) return s->rom_read(addr, old_val);
    }
    return old_val;
}

u8 MMU::aux_card_RAMread(u32 addr)
{
    return AUX_RAM.ptr[addr];
}

void MMU::reset()
{
    RAM_bank = 0xD000; // 0xC000 or 0xD000
    page1_accesses = 0;
    io.WRTCOUNT = 0;
    io = {};
}

u8 MMU::read_cxxx(u32 addr, bool is_write, u8 old_val, bool has_effect)
{
    // access C3XX with SLOTC3ROM reset, will enabled SLOTC8ROM,
    //  which fill fix motherboard ROM to C800-CFFF.
    // This is then canceled by reading CFFF or on reset
    if ((addr >= 0xC300) && (addr < 0xC400)) {
        if (has_effect && (io.SLOTC3ROM == 0)) io.INTC8ROM = 1;
    }

    u32 INTC8ROMWAS = io.INTC8ROM;

    if (has_effect && (addr == 0xCFFF))
        io.INTC8ROM = 0;


    if ((addr >= 0xC800) && INTC8ROMWAS)
        return read_ROM(addr);

    /*
 CXXX will not go high if
INTCXROM is set and $C100—$CFFF is addressed,
ifSLOTC3ROM is reset and $C3XX is addressed,or
if INTC8O0M is set and $C800-$CFFFis addres-
sed.
     */

    u32 things = (io.INTCXROM << 1) | io.SLOTC3ROM;
    u32 group0 = ((addr >= 0xC100) && (addr < 0xC300)) || (addr >= 0xC400);
    switch(things) {
        case 0: // INTCXROM=0, SLOTC3ROM=0
            if (group0) return slot_read(addr, old_val);
            return read_ROM(addr);
        case 1: // INTCXROM=0, SLOTC3ROM=1
            return slot_read(addr, old_val);
        case 2: // INTCXROM=1, SLOTC3ROM=0
        case 3: // INTCXROM=1, SLOTC3ROM=1
            return read_ROM(addr);
        default:
            assert(1==2);
    }
    NOGOHERE;
    return 0;
}

u8 MMU::access_c0xx(u32 addr, bool is_write, u8 old_val, bool has_effect)
{
    if (addr >= 0xC090 && addr < 0xC100) {
        u32 sn = (addr - 0xC080) >> 4;
        auto& s = bus->slots[sn];
        if (s) {
            if (is_write) { s->io_write(addr, old_val); return 0; }
            return s->io_read(addr, old_val, has_effect);
        }
        return old_val;
    }

    // 80STORE
    u32 r = 0;
    u32 MSB = 0;
    u32 caddr = addr & 0xFFFE;
    switch (caddr) {
        #define RW(base, component, name) case base: if (has_effect)  bus-> component.io. name = addr & 1; break;
        #define W(base, component, name) case base: if (has_effect && is_write) bus-> component.io. name = addr & 1; break;
        W(0xC000, mmu, STORE80);
        W(0xC002, mmu, RAMRD);
        W(0xC004, mmu, RAMWRT);
        W(0xC006, mmu, INTCXROM);
        W(0xC008, mmu, ALTZP);
        W(0xC00A, mmu, SLOTC3ROM);
        W(0xC00C, iou, COL80);
        W(0xC00E, iou, ALTCHRSET);
        RW(0xC050, iou, TEXT);
        RW(0xC052, iou, MIXED);
        RW(0xC054, iou, PAGE2);
        RW(0xC056, iou, HIRES);
        RW(0xC058, iou, AN0);
        RW(0xC05A, iou, AN1);
        RW(0xC05C, iou, AN2);
        RW(0xC05E, iou, AN3);
        #undef W
        #undef RW
    }

    if (!is_write) {
        #define R(addr, component, name) case addr: MSB = bus-> component.io. name << 7; break;
        switch(addr) {
            R(0xC013, mmu, RAMRD);
            R(0xC014, mmu, RAMWRT);
            R(0xC015, mmu, INTCXROM);
            R(0xC016, mmu, ALTZP);
            R(0xC017, mmu, SLOTC3ROM);
            R(0xC018, mmu, STORE80);
            R(0xC01A, iou, TEXT);
            R(0xC01B, iou, MIXED);
            R(0xC010, iou, AKD);
            R(0xC01E, iou, ALTCHRSET);
            R(0xC01F, iou, COL80);
            R(0xC01C, iou, PAGE2);
            R(0xC01D, iou, HIRES);
            case 0xC011: MSB = (io.BANK1 ^ 1) << 7; break;
            case 0xC019: MSB = (bus->iou.io.VBL ^ 1) << 7; break;
            R(0xC012, mmu, HRAMRD);
        }
        #undef R
    }

    // Do C08X shenanigans
#define SBANK1 { RAM_bank = 0xC000; io.BANK1 = 1; }
#define SBANK2 { RAM_bank = 0xD000; io.BANK1 = 0; }
#define BANKS if (addr & 8) SBANK1 else SBANK2
    if ((addr >= 0xC080) && (addr < 0xC090)) {
        // last two bits = 11 or 00, enable
        // last two bits = 01 or 10, disable
        switch(addr) {
            case 0xC080: // read enable  0000
            case 0xC088: // read enable  1000
            case 0xC084: // read enable  0100
            case 0xC08C: // read enable  1100
            case 0xC082: // rd disable 0010
            case 0xC08A: // rd disable 1010
            case 0xC086: // rd disable 0110
            case 0xC08E: // rd disable 1110
                BANKS
                io.HRAMRD = (((addr & 3) == 0) || ((addr & 3) == 3));
                io.WRTCOUNT = 0;
                io.HRAMWRT = 0;
                break;
            case 0xC081: // rd disable 0001
            case 0xC089: // rd disable 1001
            case 0xC085: // rd disable 0101
            case 0xC08D: // rd disable 1101
            case 0xC083: // rd enalbe  0011
            case 0xC08B: // rd enable  1011
            case 0xC087: // rd enable  0111
            case 0xC08F: // rd enable  1111
                BANKS;
                io.HRAMRD = (((addr & 3) == 0) || ((addr & 3) == 3));
                if (!is_write) {
                    io.WRTCOUNT++;
                    if (io.WRTCOUNT >= 2) io.HRAMWRT = 1;
                }
                else {
                    io.WRTCOUNT = 0;
                }
                break;
        }
    }

    // IOU softswitches...
    bus->iou.access_c0xx(addr, has_effect, is_write, &r, &MSB);

    // Handle reading from hardware...

    return r | MSB;
}

bool MMU::addr_is_aux(u32 addr, bool RDWRT) {
    // 0-0x1FF, always main RAM
    if (addr < 0x200) {
        if (io.ALTZP) return true;
        return false;
    }
    // 0x200-0x3FF, affected by RAMRD
    if (addr < 0x400)
        return RDWRT;
    // 0x400-0x7FF
    if (addr < 0x800) {
        if (io.STORE80)
            return bus->iou.io.PAGE2;
        return RDWRT;
    }
    // 0x800-0x1FFF, RAMRD only...
    if (addr < 0x2000)
        return RDWRT;
    // 0x2000-0x3FFF, RAMRD unless HIRES
    if (addr < 0x4000) {
        if (io.STORE80 && bus->iou.io.HIRES)
            return bus->iou.io.PAGE2;
        return RDWRT;
    }
    if (addr >= 0xD000)
        return io.ALTZP;

    return RDWRT;
}

u8 MMU::cpu_bus_read(u32 addr, u8 old_val, bool has_effect)
{
    if (has_effect) {
        if ((addr >= 0x100) && (addr < 0x200)) {
            page1_accesses++;
        }
        else {
            if ((page1_accesses >= 3) && (addr == 0xFFFC)) reset();
            page1_accesses = 0;
        }
    }
    if (addr < 0xC000) {
        if (addr_is_aux(addr, io.RAMRD))
            return aux_card_RAMread(addr);
        return RAM.ptr[addr];
    }
    if (addr < 0xC100) {
        return access_c0xx(addr, false, old_val, has_effect);
    }
    if (addr < 0xD000) {
        return read_cxxx(addr, false, old_val, has_effect);
    }
    if (addr < 0xE000) { // Two 4k banks, plus BIOS
        if (!io.HRAMRD) return read_ROM(addr);
        if (addr_is_aux(addr, io.RAMRD)) return AUX_RAM.ptr[(addr & 0xFFF) | RAM_bank];
        return RAM.ptr[(addr & 0xFFF) | RAM_bank];
    }
    if (addr_is_aux(addr, io.RAMRD)) return aux_card_RAMread(addr);
    if (!io.HRAMRD) return read_ROM(addr);
    return RAM.ptr[addr];
}

void MMU::cpu_bus_write(u32 addr, u8 val)
{
    if (addr < 0xC000) {
        if (addr_is_aux(addr, io.RAMWRT))
            aux_card_RAMwrite(addr, val);
        else RAM.ptr[addr] = val;
        return;
    }
    if (addr < 0xC100) {
        access_c0xx(addr, true, val, true);
        return;
    }
    if (addr < 0xD000) {
        read_cxxx(addr, false, 0, true);
        return;
    }
    if (addr < 0xE000) {
        if (io.HRAMWRT) {
            if (addr_is_aux(addr, io.RAMWRT)) AUX_RAM.ptr[(addr & 0xFFF) | RAM_bank] = val;
            else RAM.ptr[(addr & 0xFFF) | RAM_bank] = val;
        }
        return;
    }
    if (io.HRAMWRT) {
        if (addr_is_aux(addr, io.RAMWRT)) aux_card_RAMwrite(addr, val);
        else RAM.ptr[addr] = val;
    }
}
}
