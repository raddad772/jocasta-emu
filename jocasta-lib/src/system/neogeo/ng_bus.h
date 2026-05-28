#pragma once

#include "helpers/int.h"
#include "helpers/sys_interface.h"
#include "helpers/scheduler.h"
#include "helpers/simplebuf.h"

#include "component/cpu/m68000/m68000.h"
#include "component/cpu/z80/z80.h"
#include "component/audio/ym2610/ym2610.h"
#include "component/controller/neogeo4/neogeo4.h"
#include "ng_lpsc2_a2.h"
#include "neo_b1.h"
#include "ng_cart.h"
#include "ng_controllerport.h"

namespace NEOGEO {
    static constexpr u32 YM2610_DIV = 3;
    static constexpr u32 YM2610_TIMER_DIV = YM2610_DIV * 72;
    static constexpr u32 YM2610_FM_DIV   = YM2610_DIV * 144;
    static constexpr u32 YM2610_SSG_DIV  = 12;
    static constexpr u32 YM2610_SSG_SAMPLE_DIV = YM2610_SSG_DIV * 8;
    static constexpr u32 Z80_DIV = 6;
    static constexpr u32 M68K_DIV = 2;
    static constexpr u32 YM2610_WAIT_CYCLES = 17;


struct core : jsm_system {
    scheduler_t scheduler{&master_clock};
    core(jsm::systems variant);

    M68k::core m68k{false};
    Z80::core z80{false};
    YM2610::core ym2610{&master_clock, YM2610_WAIT_CYCLES};

    u32 samples_pushed{};
    NEOB1 nb1{this};

    controller_4button controller1{};
    controller_port controllerport1{this};
    controller_port controllerport2{this};

    u64 master_clock{};
    bool is_MVS{};

    struct {
        simplebuf8 BIOS{};
        u8 lo[64 * 1024];
    } ROMs{};

    CART cart{this};


    [[nodiscard]] u16 read_VRAM(u16 addr) const { return VRAM[addr % (34 * 1024)]; };
    void write_VRAM(u16 addr, u16 val, u16 mask) {
        VRAM[addr % (34 * 1024)] = (VRAM[addr % (34 * 1024)] & ~mask) | (val & mask);
    };

    struct {
        struct {
            u16 open_bus_data{};
            bool stuck{}; // TODO: do I need this?
            i32 cycles_til_DTACK{};
            u8 z80_byte{};
        } m68k{};
        struct {
            bool reset_line{};
            u32 reset_line_count{};
            u32 windows[4];
            u8 m68k_byte{};

            bool nmi_enable{}, nmi_line{};
        } z80{};
        u32 pal_base_offset{};
        bool ROMWAIT{};
        u8 PORTwait{};
        u32 ROMWAIT_delay{};
        u32 PORTwait_delay{};
        u8 vector_select{}; // CART/BIOS vectors
        u8 fix_select{};
        u8 sram_lock{};

        u32 vram_addr{};
        u32 vram_inc{};

        struct {
            u32 slot_select{};
        } mvs{};

        u8 led_marquee{}, led_latch1{}, led_latch2{}, led_data{};
    } io{};

    struct {
        u32 lock{};
        u32 bank{};
    } card_slot{};

    LPSC2_A2::core lpsc;

    u16 RAM[32 * 1024]{};
    u16 PRAM[8 * 1024]{};
    u16 MVS_backup_RAM[32768]{};
    u16 VRAM[34 * 1024];
    u8 z80_RAM[0x800]{};


    template<bool do_debug> void cycle_m68k();
    template<bool do_debug> void cycle_z80();
    template<bool do_debug, bool peek> u8 z80_bus_read(u32 addr, u8 old);
    template<bool do_debug> void z80_bus_write(u32 addr, u8 val);
    template<bool do_debug, bool peek> u8 z80_IO_read(u32 addr, u8 old);
    template<bool do_debug> void z80_IO_write(u32 addr, u8 val);

    void eval_z80_IRQs();

    template<bool do_debug, bool peek> u16 mainbus_read(u32 addr, u8 UDS, u8 LDS, u16 old);
    template<bool do_debug> void mainbus_write(u32 addr, u8 UDS, u8 LDS, u16 val);
    template<bool do_debug, bool peek> u16 mainbus_mmio_read(u32 addr, u16 mask, u16 old);
    template<bool do_debug> void mainbus_mmio_write(u32 addr, u16 mask, u16 val);
    template<bool do_debug, bool peek> u16 read_memcard(u32 addr, u16 mask, u16 old);
    template<bool do_debug> void write_memcard(u32 addr, u16 mask, u16 val);

    struct {
        double master_cycles_per_audio_sample{}, master_cycles_per_min_sample{}, master_cycles_per_max_sample{};
        double next_sample_cycle_max{}, next_sample_cycle_min{}, next_sample_cycle{};
        double next_debug_cycle{};
        u64 cycles{}, debug_generation{};
        audio_output_ring *ssg_ring{nullptr};
        bool nosolo{true};
    } audio{};

    struct {
        u32 cycles_per_second{};
        // Derived once in ng_bus.cpp after cycles_per_second is set:
        //   fps              = cycles_per_second / (4 * 384 * 264)
        //   cycles_per_frame = 4 * 384 * 264  (= 405504, constant regardless of variant)
        double fps{};
        u32    cycles_per_frame{};
        u64 z80_cycle{}, m68k_cycle{};
    } clock{};

    bool described_inputs{};

    DBG_START
        DBG_CPU_REG_START(m68k)
            *D[8], *A[8],
            *PC, *USP, *SSP, *SR,
            *supervisor, *trace,
            *IMASK, *CSR, *IR, *IRC
        DBG_CPU_REG_END(m68k)

        DBG_CPU_REG_START(z80)
            *A, *B, *C, *D, *E, *HL, *F,
            *AF_, *BC_, *DE_, *HL_,
            *PC, *SP,
            *IX, *IY,
            *EI, *HALT, *CE
        DBG_CPU_REG_END(z80)

        DBG_EVENT_VIEW

        DBG_IMAGE_VIEWS_START
            MDBG_IMAGE_VIEW(nametables[3])
            MDBG_IMAGE_VIEW(palette)
            MDBG_IMAGE_VIEW(sprites)
            MDBG_IMAGE_VIEW(tiles)
            MDBG_IMAGE_VIEW(ym_info)
            MDBG_IMAGE_VIEW(lpsc_output)
            MDBG_IMAGE_VIEW(neo_b1_output)
        DBG_IMAGE_VIEWS_END

        DBG_WAVEFORM2_START1
            DBG_WAVEFORM2_MAIN
            DBG_WAVEFORM2_BRANCH(fm, 4)
            DBG_WAVEFORM2_BRANCH(adpcm_a, 6)
            DBG_WAVEFORM2_BRANCH(adpcm_b, 1)
            DBG_WAVEFORM2_BRANCH(ssg, 3)
        DBG_WAVEFORM2_END1
        DBG_LOG_VIEW_SIMPLE
    DBG_END


private:
    void populate_opts();
    void read_opts();
    void schedule_first();
    void setup_audio();
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
    //void save_state(serialized_state &state) final;
    //void load_state(serialized_state &state, deserialize_ret &ret) final;
    void set_audio_ring(audio_output_ring *ring) final;
    void audio_rings_ready() final;
    void setup_debugger_interface(debugger_interface &intf) final;

};

}
