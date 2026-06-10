//
// Created by . on 4/21/25.
//

#include "snes_bus.h"
#include "snes_mem.h"
#include "snes_ppu.h"
#include "r5a22.h"

namespace SNES {
void MEM::write_bad(u32 addr, u32 val, memmap_block *bl)
{
    printf("\nWARN BAD WRITE %06x:%02x", addr, val);
}

u32 MEM::read_bad(u32 addr, u32 old, bool has_effect, memmap_block *bl)
{
    if (has_effect) {
        printf("\nWARN BAD READ %06x", addr);
        static int num = 0;
        num++;
        if (num > 0) {
//        dbg_break("BECAUSE", th->clock.master_cycle_count);
        }
    }
    return old;
}

void MEM::clear_map()
{
    printf("\nCLEAR MAP!");
    for (u32 i = 0; i < 0x1000; i++) {
        read[i] = &MEM::read_bad;
        write[i] = &MEM::write_bad;
        blockmap[i].clear(memmap_block::open);
    }
}

// typedef void (*SNES_memmap_write)(core *, u32 addr, u32 val, memmap_block *bl);
//typedef u32 (*SNES_memmap_read)(core *, u32 addr, u32 old, bool has_effect, memmap_block *bl);
void MEM::write_WRAM(u32 addr, u32 val, memmap_block *bl)
{
    WRAM[((addr & 0xFFF) + bl->offset) & 0x1FFFF] = val;
}

u32 MEM::read_WRAM(u32 addr, u32 old, bool has_effect, memmap_block *bl)
{
    return WRAM[((addr & 0xFFF) + bl->offset) & 0x1FFFF];
}

void MEM::write_loROM(u32 addr, u32 val, memmap_block *bl)
{
    static int a = 1;
    printf("\nWARNING writes to ROM area! %06x", addr);
    if (a) {
        a = 0;
        printf("\nWARNING writes to ROM area!");
    }
}

u32 MEM::read_loROM(u32 addr, u32 old, bool has_effect, memmap_block *bl)
{
    u32 maddr = ((addr & 0xFFF) + bl->offset) % snes->cart.ROM.size;
    return static_cast<u8 *>(snes->cart.ROM.ptr)[maddr];
}

void MEM::write_SRAM(u32 addr, u32 val, memmap_block *bl)
{
    static_cast<u8 *>(snes->cart.SRAM->data)[((addr & 0xFFF) + bl->offset) & snes->cart.header.sram_mask] = val;
    snes->cart.SRAM->dirty = true;
}

u32 MEM::read_SRAM(u32 addr, u32 old, bool has_effect, memmap_block *bl)
{
    return static_cast<u8 *>(snes->cart.SRAM->data)[((addr & 0xFFF) + bl->offset) & snes->cart.header.sram_mask];
}

void MEM::map_generic(u32 bank_start, u32 bank_end, u32 addr_start, u32 addr_end, u32 offset, memmap_block::kinds kind, memmap_read rfunc, memmap_write wfunc) {
    for (u32 c = bank_start; c <= bank_end; c++) {
        for (u32 i = addr_start; i <= addr_end; i += 0x1000) {
            u32 b = (c << 4) | (i >> 12);
            blockmap[b].kind = kind;
            blockmap[b].offset = (i - addr_start) + offset;
            read[b] = rfunc;
            write[b] = wfunc;
        }
    }
}

void MEM::map_loram(u32 bank_start, u32 bank_end, u32 addr_start, u32 addr_end, u32 offset)
{
    map_generic(bank_start, bank_end, addr_start, addr_end, offset, memmap_block::WRAM, &MEM::read_WRAM, &MEM::write_WRAM);
}

void MEM::map_hirom(u32 bank_start, u32 bank_end, u32 addr_start, u32 addr_end, u32 offset, u32 bank_mask)
{
    for (u32 c = bank_start; c <= bank_end; c++) {
        for (u32 i = addr_start; i <= addr_end; i += 0x1000) {
            u32 b = (c << 4) | (i >> 12);
            u32 mapaddr = ((c & bank_mask) << 16) | i;
            //printf("\nMAP %06X to %06X", (c << 16) | i, mapaddr);
            mapaddr %= snes->cart.header.rom_size;
            blockmap[b].kind = memmap_block::ROM;
            blockmap[b].offset = mapaddr;
            read[b] = &MEM::read_loROM;
            write[b] = &MEM::write_loROM;
            //printf("\nMap %06x with offset %04x", (c << 16) | i, offset);
        }
    }
}

void MEM::map_lorom(u32 bank_start, u32 bank_end, u32 addr_start, u32 addr_end)
{
    u32 offset = 0;
    for (u32 c = bank_start; c <= bank_end; c++) {
        for (u32 i = addr_start; i <= addr_end; i += 0x1000) {
            u32 b = (c << 4) | (i >> 12);
            blockmap[b].kind = memmap_block::ROM;
            blockmap[b].offset = offset;
            read[b] = &MEM::read_loROM;
            write[b] = &MEM::write_loROM;
            //printf("\nMap %06x with offset %04x", (c << 16) | i, offset);
            offset += 0x1000;
            if (offset >= ROMSize) offset = 0;
        }
    }
}

void MEM::sys_map_lo()
{
    for (u32 bank = 0; bank < 0xFF; bank += 0x80) {
        map_loram(bank+0x00, bank+0x3F, 0x0000, 0x1FFF, 0);
        map_generic(bank+0x00, bank+0x3F, 0x2000, 0x3FFF, 0x2000, memmap_block::PPU, &MEM::read_PPU, &MEM::write_PPU);
        map_generic(bank+0x00, bank+0x3F, 0x4000, 0x5FFF, 0x4000, memmap_block::CPU, &MEM::read_R5A22, &MEM::write_R5A22);
    }
}

void MEM::sys_map_hi()
{
    for (u32 bank = 0; bank < 0xFF; bank += 0x80) {
        map_loram(bank + 0x00, bank + 0x3F, 0x0000, 0x1FFF, 0);
        map_generic(bank + 0x00, bank + 0x3F, 0x2000, 0x3FFF, 0x2000, memmap_block::PPU, &MEM::read_PPU, &MEM::write_PPU);
        map_generic(bank + 0x00, bank + 0x3F, 0x4000, 0x5FFF, 0x4000, memmap_block::CPU, &MEM::read_R5A22, &MEM::write_R5A22);
    }
}

void MEM::map_sram_hi(u32 bank_start, u32 bank_end)
{
    u32 offset = 0;
    for (u32 c = bank_start; c <= bank_end; c++) {
        for (u32 i = 0x6000; i < 0x7FFF; i += 0x1000) {
            u32 b = (c << 4) | (i >> 12);
            blockmap[b].kind = memmap_block::SRAM;
            blockmap[b].offset = offset;
            read[b] = &MEM::read_SRAM;
            write[b] = &MEM::write_SRAM;
            offset += 0x1000;
            if (offset >= SRAMSize) offset = 0;
        }
    }
}

void MEM::map_sram(u32 bank_start, u32 bank_end, u32 addr_start, u32 addr_end)
{
    u32 offset = 0;
    for (u32 c = bank_start; c <= bank_end; c++) {
        for (u32 i = addr_start; i <= addr_end; i += 0x1000) {
            u32 b = (c << 4) | (i >> 12);
            blockmap[b].kind = memmap_block::SRAM;
            blockmap[b].offset = offset;
            read[b] = &MEM::read_SRAM;
            write[b] = &MEM::write_SRAM;
            offset += 0x1000;
            if (offset >= SRAMSize) offset = 0;
        }
    }
}

void MEM::map_wram(u32 bank_start, u32 bank_end, u32 addr_start, u32 addr_end)
{
    u32 offset = 0;
    for (u32 c = bank_start; c <= bank_end; c++) {
        for (u32 i = addr_start; i <= addr_end; i += 0x1000) {
            u32 b = (c << 4) | (i >> 12);
            blockmap[b].kind = memmap_block::WRAM;
            blockmap[b].offset = offset;
            read[b] = &MEM::read_WRAM;
            write[b] = &MEM::write_WRAM;
            offset += 0x1000;
        }
    }
}

void MEM::map_hirom_sram()
{
    //u32 mask = snes->cart.header.sram_mask;
    map_sram_hi(0x20, 0x3F);
    map_sram_hi(0xA0, 0xBF);
}

void MEM::map_lorom_sram()
{
    // TODO: fix th
    u32 hi;
    if (ROMSizebit > 11 || SRAMSizebit > 5)
        hi = 0x7FFF;
    else
        hi = 0xFFFF;

    // HMMM...
    hi = 0x7FFF;
    map_sram(0x70, 0x7D, 0x0000, hi);
    map_sram(0xF0, 0xFF, 0x0000, hi);
}

void MEM::setup_mem_map_lorom()
{
    clear_map();

    sys_map_lo();
    if (ROMSize > (2 * 1024 * 1024)) {
        printf("\nBLROM MAPPING!");
        map_lorom(0x00, 0x7F, 0x8000, 0xFFFF);
        map_lorom(0x80, 0xFF, 0x8000, 0xFFFF);
    }
    else {
        map_lorom(0x00, 0x3F, 0x8000, 0xFFFF);
        map_lorom(0x40, 0x7F, 0x8000, 0xFFFF);
        map_lorom(0x80, 0xBF, 0x8000, 0xFFFF);
        map_lorom(0xC0, 0xFF, 0x8000, 0xFFFF);
    }

    map_lorom_sram();
    map_loram(0x00, 0x3F, 0x0000, 0x1FFF, 0);
    map_loram(0x80, 0xBF, 0x0000, 0x1FFF, 0);
    map_wram(0x7E, 0x7F, 0x0000, 0xFFFF);
}

void MEM::setup_mem_map_hirom()
{
    clear_map();
    printf("\nMEM MAPPING!");


    sys_map_hi();

    map_hirom(0x00, 0x3F, 0x8000, 0xFFFF, 0, 0x3F);
    map_hirom(0x40, 0x7D, 0x0000, 0xFFFF, 0, 0x3F);
    map_hirom(0x80, 0xBF, 0x0000, 0xFFFF, 0, 0x3F);
    map_hirom(0xC0, 0xFF, 0x0000, 0xFFFF, 0, 0x3F);

    map_loram(0x00, 0x3F, 0x0000, 0x1FFF, 0);
    map_loram(0x80, 0xBF, 0x0000, 0x1FFF, 0);
    map_hirom_sram();

    map_wram(0x7E, 0x7F, 0x0000, 0xFFFF);
}

void MEM::cart_inserted()
{
    ROMSizebit = snes->cart.header.rom_sizebit;
    SRAMSizebit = snes->cart.header.sram_sizebit;
    ROMSize = snes->cart.ROM.size;
    SRAMSize = snes->cart.header.sram_size;

    /// TODO: SRAM
    if (snes->cart.header.lorom)
        setup_mem_map_lorom();
    else
        setup_mem_map_hirom();
}

// wdc65816_read and _write replaced with read_bus_a
u32 MEM::read_bus_A(u32 addr, u32 old, bool has_effect)
{
    return (this->*read[addr >> 12])(addr, old, has_effect, &blockmap[addr >> 12]);
}

void MEM::write_bus_A(u32 addr, u32 val)
{
    (this->*write[addr >> 12])(addr, val, &blockmap[addr >> 12]);
}

u32 MEM::read_R5A22(u32 addr, u32 old, bool has_effect, memmap_block *bl) {
    return snes->r5a22.reg_read(addr, old, has_effect, bl);
}

void MEM::write_R5A22(u32 addr, u32 val, memmap_block *bl) {
    snes->r5a22.reg_write(addr, val, bl);
}

u32 MEM::read_PPU(u32 addr, u32 old, bool has_effect, memmap_block *bl) {
    return snes->ppu.read(addr, old, has_effect, bl);
}

void MEM::write_PPU(u32 addr, u32 val, memmap_block *bl) {
    snes->ppu.write(addr, val, bl);
}


}