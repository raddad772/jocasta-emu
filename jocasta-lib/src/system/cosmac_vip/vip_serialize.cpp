//
// Created by . on 11/21/25.
//

#include "vip_serialize.h"
#include "vip_bus.h"

namespace VIP {

void core::serialize_console(serialized_state &state)
{
    state.new_section("console", SS_console, 1);
#define S(x) Sadd(state, &(x), sizeof(x))
    S(RAM);
    S(RAM_mask);
    S(u6b);
    S(cycles_deficit);
    S(hex_keypad.latch);
    S(audio.master_cycles_per_audio_sample);
    S(audio.master_cycles_per_max_sample);
    S(audio.next_sample_cycle);
    S(audio.next_sample_cycle_max);
    S(audio.cycles);
#undef S
}

void core::serialize_clock(serialized_state &state)
{
    state.new_section("clock", SS_clock, 1);
    Sadd(state, &clock.master_cycle_count, sizeof(clock.master_cycle_count));
}

void core::serialize_cpu(serialized_state &state)
{
    state.new_section("cdp1802", SS_cdp1802, 1);
    cpu.serialize(state);
}

void core::serialize_pixie(serialized_state &state)
{
    state.new_section("cdp1861", SS_cdp1861, 1);
    pixie.serialize(state);
}

void core::deserialize_console(serialized_state &state)
{
#define L(x) Sload(state, &(x), sizeof(x))
    L(RAM);
    L(RAM_mask);
    L(u6b);
    L(cycles_deficit);
    L(hex_keypad.latch);
    L(audio.master_cycles_per_audio_sample);
    L(audio.master_cycles_per_max_sample);
    L(audio.next_sample_cycle);
    L(audio.next_sample_cycle_max);
    L(audio.cycles);
#undef L
}

void core::deserialize_clock(serialized_state &state)
{
    Sload(state, &clock.master_cycle_count, sizeof(clock.master_cycle_count));
}

void core::deserialize_cpu(serialized_state &state)
{
    cpu.deserialize(state);
}

void core::deserialize_pixie(serialized_state &state)
{
    pixie.deserialize(state);
}

void core::save_state(serialized_state &state)
{
    state.version = 1;
    state.opt.len = 0;

    serialize_console(state);
    serialize_clock(state);
    serialize_cpu(state);
    serialize_pixie(state);
}

void core::load_state(serialized_state &state, deserialize_ret &ret)
{
    state.iter.offset = 0;

    for (u32 i = 0; i < state.sections.size(); i++) {
        serialized_state_section &sec = state.sections.at(i);
        state.iter.offset = sec.offset;
        switch (sec.kind) {
            case SS_console:  deserialize_console(state); break;
            case SS_clock:    deserialize_clock(state);   break;
            case SS_cdp1802:  deserialize_cpu(state);     break;
            case SS_cdp1861:  deserialize_pixie(state);   break;
            default: break;
        }
    }

    // Rewire display output pointers (they are raw pointers into the IO device vector)
    pixie.display = &pixie.display_ptr.get().display;
    pixie.cur_output = static_cast<u8 *>(pixie.display->output[pixie.display->active_draw_buffer]);
    pixie.line_output = pixie.cur_output + (pixie.y * 112);

    // Reschedule all events from scratch based on restored clock
    scheduler.clear();
    schedule_first();
}

}
