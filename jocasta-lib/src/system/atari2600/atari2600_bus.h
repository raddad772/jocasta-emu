//
// Created by . on 6/11/25.
//

#pragma once

#include "helpers/int.h"
#include "helpers/sys_interface.h"

#include "component/controller/atari2600/atari2600_controller.h"
#include "component/cpu/m6502/m6502.h"
#include "component/cpu/m6502/m6502_opcodes.h"
#include "component/misc/m6532/m6532.h"

#include "cart.h"
#include "tia.h"

namespace atari2600 {

struct CPU_bus {
    union {
        struct {
            u16 _0_6: 7;
            u16 a7: 1;
            u16 _8: 1;
            u16 a9: 1;
            u16 _2: 2;
            u16 a12: 1;
        };
        u32 u{};
    } Addr{};
    u8 D{};
    u32 RW{};
};

struct core : jsm_system {
    //struct atari2600_clock clock{};
    //struct atari2600_bus bus{};
    core();
    M6502::core cpu{M6502::decoded_opcodes};
    M6532 riot{};
    TIA tia{};

    cvec_ptr<physical_io_device> controller1_pio{};
    cvec_ptr<physical_io_device> controller2_pio{};

    void CPU_run_cycle();

    CONTROLLER controller1{};
    CONTROLLER controller2{};

    bool described_inputs{};

    CPU_bus cpu_bus{};

    i32 cycles_left{};
    bool display_enabled{};
    u64 master_clock{};

    CART cart{};

    struct {
        bool reset{};
        bool select{};
        bool color{};
        u8 p0_difficulty{};
        u8 p1_difficulty{};
    } case_switches{};

    DBG_START
        DBG_EVENT_VIEW
        DBG_CPU_REG_START1 *A, *X, *Y, *P, *S, *PC DBG_CPU_REG_END1
        DBG_MEMORY_VIEW
        DBG_LOG_VIEW_SIMPLE
    DBG_END

private:
    void latch_inputs();

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
    void setup_debugger_interface(debugger_interface &intf) final;
};

}
