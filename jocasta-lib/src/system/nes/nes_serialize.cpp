//
// Created by . on 12/7/24.
//

#include <cstdlib>
#include <cassert>
#include "nes_bus.h"
#include "nes_serialize.h"
#include "helpers/present/sys_present.h"
#include "helpers/serialize/serialize.h"
#include "component/audio/nes_apu/nes_apu.h"
#include "component/cpu/m6502/m6502.h"

void NES::core::serialize(serialized_state &state) const {
    state.new_section("console", NESSS_console, 1);
#define S(x) Sadd(state, &(this-> x), sizeof(this-> x))
    S(cycles_left);
    S(display_enabled);
    S(dbg_data);
    S(audio.master_cycles_per_audio_sample);
    S(audio.next_sample_cycle);
#undef S
}

void NES_clock::serialize(serialized_state &state) const {
    state.new_section("clock", NESSS_clock, 1);
#define S(x) Sadd(state, &x, sizeof(x))
    S(master_clock);
    S(master_frame);
    S(frames_since_restart);
    S(cpu_master_clock);
    S(apu_master_clock);
    S(ppu_master_clock);
    S(trace_cycles);
    S(nmi);
    S(cpu_frame_cycle);
    S(ppu_frame_cycle);

    S(timing);
    S(ppu_y);
    S(frame_odd);
#undef S
}

static void serialize_apu(NES::core &nes, serialized_state &state) {
    state.new_section("APU", NESSS_apu, 1);
    nes.apu.serialize(state);
}

void r2A03::serialize(serialized_state &state) {
    state.new_section("CPU", NESSS_cpu, 1);
#define S(x) Sadd(state, &x, sizeof(x))
    S(open_bus);
    S(irq);
    S(io);
    S(joypad1.counter);
    S(joypad1.latched);
    S(joypad2.counter);
    S(joypad2.latched);
    cpu.serialize(state);
#undef S
}

void NES_PPU::serialize(serialized_state &state) {
    state.new_section("PPU", NESSS_ppu, 1);
#define S(x) Sadd(state, &x, sizeof(x))

    // render_cycle...//visible, postrender, prerender
    u32 v = 0;
    if (render_cycle == &NES_PPU::scanline_visible) v = 1;
    if (render_cycle == &NES_PPU::scanline_postrender) v = 2;
    Sadd(state, &v, sizeof(v));

    S(line_cycle);
    S(OAM);
    S(secondary_OAM);
    S(secondary_OAM_index);
    S(secondary_OAM_sprite_index);
    S(secondary_OAM_sprite_total);
    S(secondary_OAM_lock);
    S(OAM_transfer_latch);
    S(OAM_eval_index);
    S(OAM_eval_done);
    S(sprite0_on_next_line);
    S(sprite0_on_this_line);
    S(w2006_buffer);
    S(CGRAM);
    S(bg_fetches[0]);
    S(bg_fetches[1]);
    S(bg_fetches[2]);
    S(bg_fetches[3]);
    S(bg_shifter);
    S(bg_palette_shifter);
    S(bg_tile_fetch_addr);
    S(bg_tile_fetch_buffer);
    for (u32 i = 0; i < 8; i++) {
        S(sprite_pattern_shifters[i]);
        S(sprite_attribute_latches[i]);
        S(sprite_x_counters[i]);
        S(sprite_y_lines[i]);
    }
    S(last_sprite_addr);
    S(io);
    S(dbg.v);
    S(dbg.t);
    S(dbg.x);
    S(dbg.w);
    S(status);
    S(latch);
    S(rendering_enabled);
    S(prev_rendering_enabled);
    S(new_rendering_enabled);
    S(v_update_delay);
    S(v_pending);
#undef S
}

void serialize_cart(NES::core &nes, serialized_state &state) {
    state.new_section("cart", NESSS_cartridge, 1);
#define S(x) Sadd(state, &(nes. x), sizeof(nes. x))
    S(ppu_mirror_mode);
    Sadd(state, nes.CIRAM.ptr, nes.CIRAM.sz);
    Sadd(state, nes.CPU_RAM.ptr, nes.CPU_RAM.sz);
    Sadd(state, nes.CHR_RAM.ptr, nes.CHR_RAM.sz);
    nes.mapper->serialize(state);
#undef S
}

void NES::core::save_state(serialized_state &state) {
    // Basic info
    state.version = 1;
    state.opt.len = 0;

    // Make screenshot
    state.has_screenshot = 1;
    state.screenshot.allocate(256, 240);
    state.screenshot.clear();
    NES_present(ppu.display_ptr.get(), state.screenshot.data.ptr, 0, 0, 256, 240);

    serialize(state);
    clock.serialize(state);
    serialize_apu(*this, state);
    cpu.serialize(state);
    ppu.serialize(state);
    serialize_cart(*this, state);
}

void NES::core::deserialize(serialized_state &state) {
#define L(x) Sload(state, &this-> x, sizeof(this-> x))
    L(cycles_left);
    L(display_enabled);
    L(dbg_data);
    L(audio.master_cycles_per_audio_sample);
    L(audio.next_sample_cycle);
#undef L
}

void NES_clock::deserialize(serialized_state &state) {
#define L(x) Sload(state, &x, sizeof(x))
    L(master_clock);
    L(master_frame);
    L(frames_since_restart);
    L(cpu_master_clock);
    L(apu_master_clock);
    L(ppu_master_clock);
    L(trace_cycles);
    L(nmi);
    L(cpu_frame_cycle);
    L(ppu_frame_cycle);

    L(timing);
    L(ppu_y);
    L(frame_odd);
#undef L
}

void deserialize_apu(NES::core &nes, serialized_state &state) {
    nes.apu.deserialize(state);
}

void r2A03::deserialize(serialized_state &state) {
#define L(x) Sload(state, &x, sizeof(x))
    L(open_bus);
    L(irq);
    L(io);
    L(joypad1.counter);
    L(joypad1.latched);
    L(joypad2.counter);
    L(joypad2.latched);
    cpu.deserialize(state);
#undef L
}

void NES_PPU::deserialize(serialized_state &state) {
#define L(x) Sload(state, &x, sizeof(x))
    u32 v = 0;
    Sload(state, &v, sizeof(v));
    switch(v) {
        case 0:
            render_cycle = &NES_PPU::scanline_prerender;
            break;
        case 1:
            render_cycle = &NES_PPU::scanline_visible;
            break;
        case 2:
            render_cycle = &NES_PPU::scanline_postrender;
            break;
        default:
            NOGOHERE;
            break;
    }

    L(line_cycle);
    L(OAM);
    L(secondary_OAM);
    L(secondary_OAM_index);
    L(secondary_OAM_sprite_index);
    L(secondary_OAM_sprite_total);
    L(secondary_OAM_lock);
    L(OAM_transfer_latch);
    L(OAM_eval_index);
    L(OAM_eval_done);
    L(sprite0_on_next_line);
    L(sprite0_on_this_line);
    L(w2006_buffer);
    L(CGRAM);
    L(bg_fetches[0]);
    L(bg_fetches[1]);
    L(bg_fetches[2]);
    L(bg_fetches[3]);
    L(bg_shifter);
    L(bg_palette_shifter);
    L(bg_tile_fetch_addr);
    L(bg_tile_fetch_buffer);
    for (u32 i = 0; i < 8; i++) {
        L(sprite_pattern_shifters[i]);
        L(sprite_attribute_latches[i]);
        L(sprite_x_counters[i]);
        L(sprite_y_lines[i]);
    }
    L(last_sprite_addr);
    L(io);
    L(dbg.v);
    L(dbg.t);
    L(dbg.x);
    L(dbg.w);
    L(status);
    L(latch);
    L(rendering_enabled);
    L(prev_rendering_enabled);
    L(new_rendering_enabled);
    L(v_update_delay);
    L(v_pending);
#undef L
}

void deserialize_cart(NES::core &nes, serialized_state &state) {
#define L(x) Sload(state, &nes. x, sizeof(nes. x))
    L(ppu_mirror_mode);

    Sload(state, nes.CIRAM.ptr, nes.CIRAM.sz);
    Sload(state, nes.CPU_RAM.ptr, nes.CPU_RAM.sz);
    Sload(state, nes.CHR_RAM.ptr, nes.CHR_RAM.sz);
    // SRAM is bundled separately in the .jsst zip; do not load here.
    nes.mapper->deserialize(state);
#undef L
}

void NES::core::load_state(serialized_state &state, deserialize_ret &ret) {
    state.iter.offset = 0;
    assert(state.version == 1);

    for (auto & sec : state.sections) {
        state.iter.offset = sec.offset;
        switch (sec.kind) {
            case NESSS_console:
                deserialize(state);
                break;
            case NESSS_clock:
                clock.deserialize(state);
                break;
            case NESSS_apu:
                deserialize_apu(*this, state);
                break;
            case NESSS_cpu:
                cpu.deserialize(state);
                break;
            case NESSS_ppu:
                ppu.deserialize(state);
                break;
            case NESSS_cartridge:
                deserialize_cart(*this, state);
                break;
            default: NOGOHERE;
        }
    }
}
