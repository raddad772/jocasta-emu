//
// Created by . on 6/18/25.
//

#pragma once

#include "helpers/sys_interface.h"
#include "helpers/scheduler.h"
#include "helpers/debugger/debugger.h"
#include "helpers/debugger/debuggerdefs.h"

#include "component/cpu/huc6280/huc6280.h"
#include "component/gpu/huc6270/huc6270.h"
#include "component/gpu/huc6260/huc6260.h"
#include "tg16_clock.h"
#include "tg16_cart.h"
#include "tg16_controllerport.h"
#include "tg16_cdrom.h"
#include "component/controller/tg16b2/tg16b2.h"

namespace TG16 {

static constexpr u32 CDROM_RAM_BASE = 0x0D0000;
static constexpr u32 CDROM_RAM_SIZE = 0x040000;
static constexpr u32 BIOS_MAX = 0x040000;
static constexpr u32 AC_RAM_SIZE = 0x200000;
static constexpr u32 BRAM_BASE = 0x1EE000;
static constexpr u32 BRAM_SIZE = 0x000800;

struct core : jsm_system {
    explicit core(jsm::systems kind = jsm::TURBOGRAFX16);
    u8 bus_read(u32 addr, u8 old, bool has_effect);
    static u8 huc_read_mem(void *ptr, u32 addr, u8 old, bool has_effect);
    static void huc_write_mem(void *ptr, u32 addr, u8 val);
    static void huc_write_io(void *ptr, u8 val);
    static u8 huc_read_io(void *ptr);
    void bus_write(u32 addr, u8 val);
    CLOCK clock{};
    scheduler_t scheduler{&clock.master_cycles};
    HUC6280::core cpu{&scheduler, clock.timing.second.cycles};
    HUC6270::chip vdc0{&scheduler}, vdc1{&scheduler}; // Video Display Controller
    HUC6260::chip vce{&scheduler, &vdc0, &vdc1}; // Video Color Encoder
    CART cart{};
    controllerport controller_port{};
    controller_2button controller{};

    u8 RAM[8192]{};

    bool is_cd{};
    bool is_arcade_card{};
    u32 bios_size{};
    u8 BIOS[BIOS_MAX]{};
    u8 cdrom_ram[CDROM_RAM_SIZE]{};
    u8 ac_ram[AC_RAM_SIZE]{};
    CDROM::core cdrom;

    persistent_store *bram_store{};
    bool bram_initialized{};

    struct {
        bool described_inputs{};
    } jsm{};

    struct {
        f64 master_cycles_per_audio_sample{}, master_cycles_per_min_sample{}, master_cycles_per_max_sample{};
        f64 next_sample_cycle_max{}, next_sample_cycle_min{}, next_sample_cycle{};
        f64 next_debug_cycle{};
        f64 master_cycles_per_cdda_sample{}, next_cdda_cycle{};
        f64 master_cycles_per_adpcm_sample{}, next_adpcm_cycle{};
        audio_output_ring *output_ring{};
        u64 cycles{};
    } audio{};


    DBG_START
        DBG_EVENT_VIEW
        DBG_CPU_REG_START1
            *A{}, *X{}, *Y{}, *PC{}, *S{}, *P{}, *MPR[8]{}
        DBG_CPU_REG_END1

        DBG_IMAGE_VIEWS_START
            MDBG_IMAGE_VIEW(palettes)
            MDBG_IMAGE_VIEW(tiles)
            MDBG_IMAGE_VIEW(sys_info)
        DBG_IMAGE_VIEWS_END
        DBG_LOG_VIEW_SIMPLE
        DBG_MEMORY_VIEW

        DBG_WAVEFORM_START(psg)
            DBG_WAVEFORM_MAIN
            DBG_WAVEFORM_CHANS(6)
        DBG_WAVEFORM_END(psg)

        DBG_WAVEFORM_START(cd)
            DBG_WAVEFORM_MAIN
            DBG_WAVEFORM_CHANS(1)
        DBG_WAVEFORM_END(cd)

    DBG_END

private:
    void setup_audio();
    static void vdc0_update_irqs(void *ptr, bool val);
    static void sample_psg(void *ptr, u64 key, u64 clock, u32 jitter);
    static void sample_cdda(void *ptr, u64 key, u64 clock, u32 jitter);
    static void sample_adpcm(void *ptr, u64 key, u64 clock, u32 jitter);
    static void sample_audio_debug_max(void *ptr, u64 key, u64 clock, u32 jitter);
    static void sample_audio_debug_min(void *ptr, u64 key, u64 clock, u32 jitter);
    static void psg_go(void *ptr, u64 key, u64 clock, u32 jitter);
    void fixup_audio();
    void schedule_first();
    events_view &get_events_view();
    void setup_crt(JSM_DISPLAY &d);

public:
    void play() final;
    void pause() final;
    void stop() final;
    void get_framevars(framevars& out) final;
    void reset() final;
    void killall();
    u32 finish_frame() final;
    u32 finish_scanline() final;
    u32 step_master(u32 howmany) final;
    void load_BIOS(multi_file_set& mfs) final;
    void enable_tracing();
    void disable_tracing();
    void describe_io() final;
    void save_state(serialized_state &state) final;
    void load_state(serialized_state &state, deserialize_ret &ret) final;
    void set_audio_ring(audio_output_ring *ring) final;
    void audio_rings_ready() final;
    void setup_debugger_interface(debugger_interface &intf) final;
    //void sideload(multi_file_set& mfs) final;
};

}
