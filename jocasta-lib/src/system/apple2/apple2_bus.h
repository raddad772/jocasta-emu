#pragma once

#include "helpers/sys_interface.h"
#include "helpers/physical_io.h"
#include "helpers/cvec.h"
#include "helpers/debugger/debugger.h"
#include "helpers/debug.h"
#include "helpers/simplebuf.h"
#include "iou.h"
#include "mmu.h"
#include "slot.h"
#include "slot_disk2.h"

#include <memory>
#include "component/cpu/m6502/m6502.h"
#include "component/cpu/m6502/m6502_opcodes.h"

namespace apple2 {
struct core : jsm_system {
    explicit core(const system_config& cfg = {});
    M6502::core cpu{M6502::decoded_opcodes};
    void cycle();
    struct {
        u64 master_cycles{};
        u64 frames_since_restart{};
        u64 master_frame{};
        u32 crt_x{}, crt_y{};

        u32 long_cycle_counter{};
        u32 cpu_divisor{};
        u32 iou_divisor{};

        u32 cpu_adder{};
        u32 iou_adder{};
    } clock{};
    IOU iou{this};
    MMU mmu{this};
    static constexpr u32 NUM_SLOTS = 8;
    std::unique_ptr<slot::interface> slots[NUM_SLOTS];
    bool described_inputs{};
    void CPU_cycle();
private:
    void setup_audio(float fps);

public:
    DBG_START
    DBG_EVENT_VIEW
    DBG_LOG_VIEW_SIMPLE
    DBG_END

public:
    void setup_crt(JSM_DISPLAY &d);
    void setup_audio(std::vector<physical_io_device> &inIOs);
    void setup_keyboard();
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
    void configure_slot(u32 slot_num, const char* card_name) final;
    //void save_state(serialized_state &state) final;
    //void load_state(serialized_state &state, deserialize_ret &ret) final;
    void set_audio_ring(audio_output_ring *ring) final;
    void setup_debugger_interface(debugger_interface &intf) final;
    //void sideload(multi_file_set& mfs) final;
};
}
