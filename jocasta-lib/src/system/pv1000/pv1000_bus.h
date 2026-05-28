#pragma once

#include "helpers/sys_interface.h"
#include "helpers/audio_dump.h"
#include "component/cpu/z80/z80.h"
#include "helpers/simplebuf.h"
#include "component/gpu/nec_d65010g031/nec_d65010g031.h"
#include "component/controller/pv1000/pv1000_controller.h"

namespace CASIO_PV1000 {

static constexpr u32 CYCLES_PER_SEC = 17897727;
static constexpr u32 Z80_DIV = 5;
static constexpr u32 VDP_DIV = 3;
static constexpr u32 PSG_DIV = 512;
static constexpr u32 MASTER_CYCLES_PER_FRAME = VDP_DIV * 380 * 262;


struct core : jsm_system {
    core();
    Z80::core cpu{false};
    NEC_D65010G031::core vdp{};

    u8 RAM[2 * 1024]{};
    simplebuf8 ROM{};

    u8 mainbus_read(u16 addr, u8 old, bool has_effect);
    void mainbus_write(u16 addr, u8 val);

    u8 mainbus_in(u16 addr, u8 old, bool has_effect);
    void mainbus_out(u16 addr, u8 val);

    void cycle();

    CASIO_PV1000_controller controller1{}, controller2{};

    struct {
        u64 master_cycle_count{};
        u32 z80_div{};
        u32 vdp_div{};
        u32 psg_div{};
    } clock{};
    struct {
        bool described_inputs{false};
    } jsm{};

    struct {
        struct {
            bool reset_line{};
            bool bus_request{}, bus_ack{};
        } z80{};
        u8 regs[8];
    } io{};

    struct {
        audio_output_ring *output_ring{};
        u64 cycles{};
        double master_cycles_per_audio_sample{};
        double next_sample_cycle{};
        jsm_audio_dump dump{};
    } audio{};

    DBG_START
        DBG_WAVEFORM_START1
            DBG_WAVEFORM_MAIN
            DBG_WAVEFORM_CHANS(3)
        DBG_WAVEFORM_END1
    DBG_END

private:
    void setup_crt(JSM_DISPLAY &d);
    void setup_audio();
    void sample_audio();
    void cycle_z80();

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
    void setup_debugger_interface(debugger_interface &intf) final;

};

}
