//
// Created by . on 11/13/25.
//

#pragma once

#include "helpers/physical_io.h"
#include "helpers/sys_interface.h"

#include "component/audio/nes_apu/nes_apu.h"

#include "nes_clock.h"
#include "nes_cart.h"
#include "nes_cpu.h"
#include "nes_ppu.h"
#include "mappers/mapper.h"
#include "mappers/nes_memmap.h"

#define NES_INPUTS_CHASSIS 0
#define NES_INPUTS_CARTRIDGE 1
#define NES_INPUTS_PLAYER1 0
#define NES_INPUTS_PLAYER2 1

#define NES_OUTPUTS_DISPLAY 0

enum NES_TIMINGS {
    NES_NTSC,
    NES_PAL,
    NES_DENDY
};

namespace NES {
    struct core;
}

namespace NES {

struct core : jsm_system {
private:
    void setup_audio(std::vector<physical_io_device> &inIOs);
    void sample_audio();

public:
    core();
    ~core();

    NES_clock clock{};
    NES_APU apu{};
    r2A03 cpu;
    NES_PPU ppu;

    NES_mapper *mapper{};

    void set_cart(physical_io_device &pio);
    void set_which_mapper(u32 wh);
    u32 CPU_read(u32 addr, u32 old_val, u32 has_effect);
    void CPU_write(u32 addr, u32 val);

    void PPU_write(u32 addr, u32 val);
    u32 PPU_read_effect(u32 addr);
    u32 PPU_read_noeffect(u32 addr);

    NES_mappers which{};
    void do_reset();

    void *mapper_ptr{};

    mirror_ppu_t ppu_mirror{};
    u32 ppu_mirror_mode{};
    struct {
        u32 has_sound{};
    } flags{};

    simplebuf8 CIRAM{0x800}; // 0x800 PPU RAM
    simplebuf8 CPU_RAM{0x800}; // 0x800 CPU RAM

    NES_memmap CPU_map[65536 / 0x2000]{};
    NES_memmap PPU_map[0x4000 / 0x400]{};

    persistent_store *SRAM{};
    simplebuf8 fake_PRG_RAM{};
    simplebuf8 PRG_ROM{};
    simplebuf8 CHR_ROM{};
    simplebuf8 CHR_RAM{};

    float NES_audio_bias=1.0f;
    float mapper_audio_bias=0.0f;

    u32 num_PRG_ROM_banks8K{};
    u32 num_PRG_ROM_banks16K{};
    u32 num_PRG_ROM_banks32K{};
    u32 num_CHR_ROM_banks1K{};
    u32 num_CHR_ROM_banks2K{};
    u32 num_CHR_ROM_banks4K{};
    u32 num_CHR_ROM_banks8K{};
    u32 num_CHR_RAM_banks{};
    u32 num_PRG_RAM_banks{};
    u32 has_PPU_RAM{};

    void map_PRG8K(u32 range_start, u32 range_end, simplebuf8 *buf, u32 bank, u32 is_readonly);
    void map_PRG16K(u32 range_start, u32 range_end, simplebuf8 *buf, u32 bank, u32 is_readonly);
    void map_PRG32K(u32 range_start, u32 range_end, simplebuf8 *buf, u32 bank, u32 is_readonly);
    void map_CHR1K(u32 range_start, u32 range_end, simplebuf8 *buf, u32 bank, u32 is_readonly);
    void map_CHR2K(u32 range_start, u32 range_end, simplebuf8 *buf, u32 bank, u32 is_readonly);
    void map_CHR4K(u32 range_start, u32 range_end, simplebuf8 *buf, u32 bank, u32 is_readonly);
    void map_CHR8K(u32 range_start, u32 range_end, simplebuf8 *buf, u32 bank, u32 is_readonly);
    void PPU_mirror_set();

    u32 described_inputs=0;
    i32 cycles_left=0;
    u32 display_enabled=0;

    NES_cart cart;

    struct {
        double master_cycles_per_audio_sample{};
        double next_sample_cycle{};
        audio_output_ring *output_ring{};
    } audio{};

    DBG_START
        DBG_CPU_REG_START1 *A, *X, *Y, *P, *S, *PC DBG_CPU_REG_END1
        DBG_EVENT_VIEW

        DBG_IMAGE_VIEWS_START
        MDBG_IMAGE_VIEW(nametables)
        DBG_IMAGE_VIEWS_END

        DBG_WAVEFORM_START1
            DBG_WAVEFORM_MAIN
            DBG_WAVEFORM_CHANS(5)
        DBG_WAVEFORM_END1

    DBG_END

    struct NESDBGDATA {
        struct DBGNESROW {
            struct {
                u32 bg_hide_left_8{}, bg_enable{}, emph_bits{}, bg_pattern_table{};
                u32 x_scroll{}, y_scroll{};
            } io{};
        } rows[240]{};

    } dbg_data{};

    void play() final;
    void pause() final;
    void stop() final;
    void get_framevars(framevars& out) final;
    void reset() final;
    void killall();
    u32 finish_frame() final;
    u32 finish_scanline() final;
    u32 step_master(u32 howmany) final;
    //void load_BIOS(multi_file_set& mfs) final;
    void enable_tracing();
    void disable_tracing();
    void describe_io() final;
    void save_state(serialized_state &state) final;
    void load_state(serialized_state &state, deserialize_ret &ret) final;
    void set_audio_ring(audio_output_ring *ring) final;
    void setup_debugger_interface(debugger_interface &intf) final;

    void serialize(serialized_state &state) const;
    void deserialize(serialized_state &state);
};

}
