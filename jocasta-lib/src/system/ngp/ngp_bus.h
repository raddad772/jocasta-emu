#pragma once
#include "helpers/int.h"
#include "helpers/cvec.h"
#include "helpers/sys_interface.h"
#include "component/cpu/z80/z80.h"
#include "tmp95c061.h"
#include "kxge.h"
#include "apu.h"
#include "ngp_cart.h"

namespace NGP {

static constexpr u32 SYS_CLKSPD = 6'144'000;
static constexpr u32 CPU_DIV = 1;
static constexpr u32 Z80_DIV = 2;
static constexpr u32 APU_DIV = 32;

struct core : jsm_system {
    core(jsm::systems kind);

    bool is_color{};
    u64 master_clock{};
    scheduler_t scheduler{&master_clock};
    Z80::core z80{false};

    TMP95C061::core cpu{&scheduler, SYS_CLKSPD, CPU_DIV};
    KXGE::core kxge;
    CART::cart cart;
    APU apu{&scheduler, SYS_CLKSPD, 32};

    bool described_inputs{};
    cvec_ptr<physical_io_device> controller_ptr{};
    cvec_ptr<physical_io_device> nvram_ptr{};
    cvec_ptr<physical_io_device> rtc_ptr{};
    void sync_rtc_pio();

    u8 BIOS[64 * 1024]{};

    static constexpr u32 WORK_RAM_SIZE = 16 * 1024;
    static constexpr u32 SOUND_RAM_OFF = 0x3000;
    u8 work_ram_owned[WORK_RAM_SIZE]{};
    u8 *work_ram = work_ram_owned;

    void ensure_work_ram_bound();
    bool boot_pending{};

    struct {
        struct {
            bool reset_line{true};
            u64 reset_line_count{3};
            u8 comms_byte{};
        } z80{};
        struct {
            bool yHold{}, xHold{};
            bool upLatch{}, downLatch{}, leftLatch{}, rightLatch{};
        } controls{};
        struct {
            bool rts_disable{};
            bool nmi_enable{};
            u8 unknown[4]{};
        } misc{};
    } io{};

    struct {
        u64 debug_generation{};
        double master_cycles_per_max_sample{}, master_cycles_per_min_sample{};
        double next_sample_cycle_max{}, next_sample_cycle_min{};
        bool nosolo{true};
    } audio{};

    DBG_START
        DBG_IMAGE_VIEWS_START
            MDBG_IMAGE_VIEW(chrmap_plane1)
            MDBG_IMAGE_VIEW(chrmap_plane2)
            MDBG_IMAGE_VIEW(plane1_iso)
            MDBG_IMAGE_VIEW(plane2_iso)
            MDBG_IMAGE_VIEW(sprites)
        DBG_IMAGE_VIEWS_END

        DBG_WAVEFORM2_START1
            DBG_WAVEFORM2_MAIN
            DBG_WAVEFORM2_BRANCH(psg, 4)
            DBG_WAVEFORM2_BRANCH(dac, 1)
        DBG_WAVEFORM2_END1

        DBG_LOG_VIEW_SIMPLE
    DBG_END

    u64 next_z80_memaccess{};

    bool power_pressed{};
    u32 power_press_hold{};
    bool power_nmi_done{};
    u32 power_hold_frames{};
    void poll_power_button();

private:
    void setup_audio();
    void setup_lcd(JSM_DISPLAY *d);
    void schedule_first(u64 from);
    void z80_reset_line(bool enabled);
    static u8 sfr_io_read(void *ptr, u8 addr, bool has_effect);
    static void sfr_io_write(void *ptr, u8 addr, u8 data);

public:
    template<bool do_debug> void cycle_z80(u64 sched);
    template<bool do_debug, bool peek> u32 mainbus_read(u32 addr, u8 sz, u32 old);
    template<bool do_debug> u32 mainbus_write(u32 addr, u8 sz, u32 val);
    template<bool do_debug, bool peek> u8 z80_bus_read(u16 addr, u8 old);
    template<bool do_debug> void z80_bus_write(u16 addr, u8 val);
    template<bool do_debug, bool peek> u8 z80_IO_read(u16 addr, u8 old);
    template<bool do_debug> void z80_IO_write(u16 addr, u8 val);

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
    void load_cart(multi_file_set& mfs, physical_io_device& pio);
    void enable_tracing();
    void disable_tracing();
    void describe_io() final;
    void set_audio_ring(audio_output_ring *ring) final;
    void audio_rings_ready() final;
    void setup_debugger_interface(debugger_interface &intf) final;
};
}
