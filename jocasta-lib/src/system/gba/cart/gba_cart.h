//
// Created by . on 12/4/24.
//

#pragma once

#include "helpers/int.h"
#include "helpers/buf.h"

namespace GBA {
    struct core;
}

namespace GBA::CART {

enum flash_kinds {
    FK_atmel,
    FK_macronix,
    FK_panasonic,
    FK_SST,
    FK_sanyo128k,
    FK_macronix128k,
    FK_other
};


enum flash_states {
    FS_idle,
    FS_get_id,
    FS_erase_4k,
    FS_write_byte,
    FS_await_bank
};

struct EEPROM {
    void serial_clear();
    void serial_add(u32 v);
    u32 addr_bus_size{};
    bool size_was_detected{true};
    u32 size_in_bytes{8192};
    u32 addr_mask{};

    u32 mode{1}; // STATE_GET_CMD

    u64 ready_at{0};

    struct {
        u64 data{};
        u32 sz{};
    } serial{};

    struct {
        u32 cur_bit_addr{};
    } cmd{};

};

struct FLASH {
    u32 cmd_loc{};
    flash_kinds kind{};
    flash_states state{};
    u32 flash_cmd[2]{};
    u32 bank_offset{};
    u32 last_cmd{};

    struct {
        u32 r55{};
        u32 r2a{};
    } regs{};
};

struct core {
    explicit core(GBA::core *parent) : gba(parent) {}
    GBA::core *gba;
    BUF ROM{};
    u32 last_read{};
    bool prefetch_stop() const;
    template<u8 sz, bool do_debug> static void write(GBA::core *gba, u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug, bool peek> static u32 read_sram(GBA::core *gba, u32 addr, u8 access);
    template<u8 sz, bool do_debug> static void write_sram(GBA::core *gba, u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug, bool peek> static u32 read(GBA::core *gba, u32 addr, u8 access);
    bool load_ROM_from_RAM(const char* fil, u64 fil_sz, physical_io_device *pio, u32 *SRAM_enable);

private:
    void write_RTC(u32 addr, u8 sz, u8 access, u32 val);
    void detect_RTC(const BUF *ROM);
    void init_eeprom();
    void write_eeprom(u32 addr, u8 sz, u8 access, u32 val);
    template<u8 sz, bool do_debug, bool peek> u32 read_eeprom(u32 addr, u8 access);
    void erase_flash();
    void write_flash_cmd(u32 addr, u32 cmd);
    template<u8 sz, bool do_debug, bool peek> u32 read_flash(u32 addr, u8 access);
    void write_flash(u32 addr, u8 sz, u8 access, u32 val);
    void finish_flash_cmd(u32 addr, u8 sz, u32 val);

public:
    struct {
        u32 mask{}, size{}, present{}, persists{};
        bool is_sram{};
        bool is_flash{};
        bool is_eeprom{};

        FLASH flash{};
        EEPROM eeprom{};
        persistent_store *store{};
    } RAM{};

    struct {
        u32 present{};
        u64 timestamp_start{};
    } RTC{};

    struct {
        i64 cycles_banked{};
        u32 next_addr{};
        u32 duty_cycle{};
        u64 last_access{};
        bool enable{true};
        bool was_disabled{};
    } prefetch{};
};

}