#pragma once

#include <vector>
#include "helpers/int.h"
#include "helpers/sram.h"

namespace NGP::CART::FLASH {

enum mode {
    READ = 0,
    READ_ID,
    WRITE,
    ACKNOWLEDGE,
};

struct CHIP {
    void configure(u32 size_bytes);
    void reset();

    u8 read(u32 addr);
    void write(u32 addr, u8 data);
    explicit operator bool() const { return rom != nullptr; }

    u8 *rom{};
    u32 size{};
    u8 vendor_id{};
    u8 device_id{};
    bool modified{};

    mode state{READ};
    u32 index{};

    struct block { bool writable; u32 offset; u32 length; };
    std::vector<block> blocks;

private:
    void reset_read();
    int block_at(u32 addr) const;
    void program(u32 addr, u8 data);
    void erase(u32 addr);
    void erase_all();
    void protect(u32 addr);
};

}

namespace NGP::CART {

struct cart {
    void load_rom(persistent_store *store, const u8 *data, u32 sz);
    void unload();
    void reset();

    u8 read(u32 chip, u32 addr);
    void write(u32 chip, u32 addr, u8 data);

    persistent_store *store{};
    u8 *rom_seed{};
    bool bound{};
    FLASH::CHIP flash[2];

private:
    bool ensure_bound();
};

}
