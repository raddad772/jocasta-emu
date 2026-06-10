#include <cstdlib>
#include <cstring>
#include <algorithm>

#include "ngp_cart.h"


namespace NGP::CART::FLASH {

static constexpr u32 K64 = 0x10000;

void CHIP::configure(u32 size_bytes)
{
    rom = nullptr;
    size = size_bytes;
    blocks.clear();
    if (!size) return;

    vendor_id = 0x98;
    if (size <= 0x80000) device_id = 0xab;
    else if (size <= 0x100000) device_id = 0x2c;
    else device_id = 0x2f;

    for (u32 off = 0; off + K64 < size; off += K64) blocks.push_back({true, off, K64});
    blocks.push_back({true, size - 0x10000, 0x8000});
    blocks.push_back({true, size - 0x8000, 0x2000});
    blocks.push_back({true, size - 0x6000, 0x2000});
    blocks.push_back({true, size - 0x4000, 0x4000});

    reset();
}

void CHIP::reset() { state = READ; index = 0; }
void CHIP::reset_read() { state = READ; index = 0; }

u8 CHIP::read(u32 addr)
{
    if (state == READ_ID) {
        switch (addr & 3) {
            case 0: return vendor_id;
            case 1: return device_id;
            case 2: return 0x02;
            default: return 0x80;
        }
    }
    if (state == ACKNOWLEDGE) {
        reset_read();
        return 0xFF;
    }
    return rom[addr & (size - 1)];
}

void CHIP::write(u32 addr, u8 data)
{
    if (state == WRITE) { program(addr, data); return; }
    if (data == 0xF0) { reset_read(); return; }

    const u32 cmd = addr & 0x7FFF;
    switch (index) {
        case 0: if (cmd == 0x5555 && data == 0xAA) { index = 1; return; } break;
        case 1: if (cmd == 0x2AAA && data == 0x55) { index = 2; return; } break;
        case 2:
            if (cmd == 0x5555) switch (data) {
                case 0x80: index = 3; return;
                case 0x90: state = READ_ID; index = 0; return;
                case 0xA0: state = WRITE; index = 0; return;
                case 0xF0: reset_read(); return;
            }
            break;
        case 3: if (cmd == 0x5555 && data == 0xAA) { index = 4; return; } break;
        case 4: if (cmd == 0x2AAA && data == 0x55) { index = 5; return; } break;
        case 5:
            if (cmd == 0x5555 && data == 0x10) { erase_all(); return; }
            if (data == 0x30) { erase(addr); return; }
            if (data == 0x9A) { protect(addr); return; }
            break;
    }
    reset_read();
}

int CHIP::block_at(u32 addr) const
{
    u32 a = addr & (size - 1);
    for (int i = 0; i < static_cast<i32>(blocks.size()); i++)
        if (a >= blocks[i].offset && a < blocks[i].offset + blocks[i].length) return i;
    return -1;
}

void CHIP::program(u32 addr, u8 data)
{
    int b = block_at(addr);
    if (b >= 0 && blocks[b].writable) {
        u32 a = addr & (size - 1);
        u8 result = rom[a] & data;
        if (result != rom[a]) { rom[a] = result; modified = true; }
    }
    reset_read();
}

void CHIP::erase(u32 addr)
{
    int b = block_at(addr);
    if (b >= 0 && blocks[b].writable) {
        memset(rom + blocks[b].offset, 0xFF, blocks[b].length);
        modified = true;
    }
    state = ACKNOWLEDGE;
    index = 0;
}

void CHIP::erase_all()
{
    for (auto &blk : blocks)
        if (blk.writable) memset(rom + blk.offset, 0xFF, blk.length);
    modified = true;
    state = ACKNOWLEDGE;
    index = 0;
}

void CHIP::protect(u32 addr)
{
    int b = block_at(addr);
    if (b >= 0 && blocks[b].writable) { blocks[b].writable = false; modified = true; }
    reset_read();
}

}

namespace NGP::CART {

static constexpr u32 CHIP_MAX = 0x200000;

void cart::load_rom(persistent_store *store_in, const u8 *data, u32 sz)
{
    unload();
    store = store_in;

    rom_seed = static_cast<u8 *>(malloc(sz));
    memcpy(rom_seed, data, sz);

    store->requested_size = sz;
    store->fill_value = 0xFF;
    store->persistent = true;
    store->init_data = rom_seed;
    store->init_data_size = sz;
    store->dirty = false;
    store->ready_to_use = false;

    flash[0].configure(std::min(sz, CHIP_MAX));
    flash[1].configure(sz > CHIP_MAX ? sz - CHIP_MAX : 0);
    bound = false;
}

void cart::unload()
{
    if (rom_seed) free(rom_seed);
    rom_seed = nullptr;
    flash[0] = {};
    flash[1] = {};
    store = nullptr;
    bound = false;
}

void cart::reset()
{
    flash[0].reset();
    flash[1].reset();
}

bool cart::ensure_bound()
{
    if (bound) return true;
    if (!store || !store->ready_to_use || !store->data) return false;
    flash[0].rom = static_cast<u8 *>(store->data);
    if (flash[1].size) flash[1].rom = static_cast<u8 *>(store->data) + CHIP_MAX;
    bound = true;
    return true;
}

u8 cart::read(u32 chip, u32 addr)
{
    if (chip >= 2 || !ensure_bound() || !flash[chip]) return 0xFF;
    return flash[chip].read(addr);
}

void cart::write(u32 chip, u32 addr, u8 data)
{
    if (chip >= 2 || !ensure_bound() || !flash[chip]) return;
    flash[chip].write(addr, data);
    if (flash[chip].modified) store->dirty = true;
}

}
