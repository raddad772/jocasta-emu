//
// Created by . on 6/18/25.
//

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "tg16_bus.h"

namespace TG16 {
u8 core::bus_read(u32 addr, u8 old, bool has_effect)
{
    if (addr >= 0x1FE000) {
        if (addr < 0x1FE400) {
            return vdc0.read(addr, old);
        }
        else if (addr < 0x1FE800) {
            return vce.read(addr, old);
        }
        else if (is_cd && addr >= 0x1FF800) {
            return cdrom.read(addr, old);
        }
        if (has_effect) {
            assert(1 == 2);
            printf("\nWHAT23");
        }
        return 0;
    }
    if (is_cd && addr >= CDROM_RAM_BASE && addr < CDROM_RAM_BASE + CDROM_RAM_SIZE) {
        return cdrom_ram[addr - CDROM_RAM_BASE];
    }
    if (is_cd && addr >= BRAM_BASE && addr < BRAM_BASE + 0x2000) {
        if (!bram_store || !bram_store->ready_to_use) return 0xFF;
        if (!bram_initialized) {
            bram_initialized = true;
            auto *data = static_cast<u8 *>(bram_store->data);
            if (data[0] != 'H' || data[1] != 'U' || data[2] != 'B' || data[3] != 'M') {
                memset(data, 0, BRAM_SIZE);
                data[0] = 0x48; data[1] = 0x55; data[2] = 0x42; data[3] = 0x4D;
                data[4] = 0x00; data[5] = 0xA0; data[6] = 0x10; data[7] = 0x80;
                bram_store->dirty = true;
            }
        }
        u32 offset = addr - BRAM_BASE;
        if (offset < BRAM_SIZE) return static_cast<u8 *>(bram_store->data)[offset];
        return 0xFF;
    }
    if (is_cd && bios_size > 0 && addr < bios_size) {
        return BIOS[addr];
    }
    if (is_cd && bios_size == 0 && addr < 0x40000) {
        static bool warned = false;
        if (!warned) { warned = true; printf("\nTG16CD: BIOS NOT LOADED - addr %06X returns 0", addr); }
    }
    if (!is_cd && addr < 0x100000) return cart.read(addr, old);
    else if (!is_cd && (addr >= 0x1EE000) && (addr < 0x1F0000)) {
        return cart.read_SRAM(addr);
    }
    else if ((addr >= 0x1F0000) && (addr <= 0x1F8000)) {
        return RAM[addr & 0x1FFF];
    }

    printf("\nUnserviced bus read addr:%06x", addr);
    return 0;
}

void core::bus_write(u32 addr, u8 val)
{
    if (addr >= 0x1FE000) {
        if (addr < 0x1FE400) {
            return vdc0.write(addr, val);
        }
        else if (addr < 0x1FE800) {
            return vce.write(addr, val);
        }
        else if (is_cd && addr >= 0x1FF800) {
            cdrom.write(addr, val);
            return;
        }
        assert(1==2);
        printf("\nWHAT22");
        return;
    }
    if (is_cd && addr >= CDROM_RAM_BASE && addr < CDROM_RAM_BASE + CDROM_RAM_SIZE) {
        cdrom_ram[addr - CDROM_RAM_BASE] = val;
        return;
    }
    if (is_cd && addr >= BRAM_BASE && addr < BRAM_BASE + 0x2000) {
        if (!bram_store || !bram_store->ready_to_use) return;
        u32 offset = addr - BRAM_BASE;
        if (offset < BRAM_SIZE) {
            static_cast<u8 *>(bram_store->data)[offset] = val;
            bram_store->dirty = true;
        }
        return;
    }
    if (!is_cd && addr < 0x100000) return cart.write(addr, val);
    else if (!is_cd && (addr >= 0x1EE000) && (addr < 0x1F0000)) {
        cart.write_SRAM(addr, val);
        return;
    }
    else if ((addr >= 0x1F0000) && (addr <= 0x1F8000)) {
        RAM[addr & 0x1FFF] = val;
        return;
    }

    printf("\nUnserviced bus write addr%06x val:%02x", addr, val);
}


u8 core::huc_read_mem(void *ptr, u32 addr, u8 old, bool has_effect)
{
    auto *th = static_cast<core *>(ptr);
    return th->bus_read(addr, old, has_effect);
}

void core::huc_write_mem(void *ptr, u32 addr, u8 val)
{
    auto *th = static_cast<core *>(ptr);
    th->bus_write(addr, val);
}

u8 core::huc_read_io(void *ptr)
{
    auto *th = static_cast<core *>(ptr);
    return th->controller_port.read_data() & 0x0F;
}

void core::huc_write_io(void *ptr, u8 val)
{
    auto *th = static_cast<core *>(ptr);
    th->controller_port.write_data(val & 3);
}
}
