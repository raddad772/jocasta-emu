//
// Created by . on 12/13/25.
//
#include <cstring>
#include "nds_serialize.h"
#include "nds_bus.h"
#include "nds_timers.h"
#include "nds_apu.h"
#include "cart/nds_cart.h"
#include "system/nds/3d/nds_ge.h"

namespace NDS {

#define S(x) Sadd(state, &(x), sizeof(x))
#define L(x) Sload(state, &(x), sizeof(x))

// ============================================================
// Memory
// ============================================================
void core::serialize_memory(serialized_state &state) {
    state.new_section("memory", SS_memory, 1);
    S(mem.RAM);
    S(mem.WRAM_share);
    S(mem.WRAM_arm7);
    S(mem.oam);
    S(mem.palette);
    S(mem.vram.data);
    // VRAM bank IO (mst/ofs/enable per bank, 9 banks)
    for (u32 i = 0; i < 9; i++) {
        S(mem.vram.io.bank[i].mst);
        S(mem.vram.io.bank[i].ofs);
        u8 en = mem.vram.io.bank[i].enable ? 1 : 0;
        S(en);
    }
    // WRAM split IO
    S(mem.io.RAM7.base); S(mem.io.RAM7.mask); S(mem.io.RAM7.disabled); S(mem.io.RAM7.val);
    S(mem.io.RAM9.base); S(mem.io.RAM9.mask); S(mem.io.RAM9.disabled); S(mem.io.RAM9.val);
}

void core::deserialize_memory(serialized_state &state) {
    L(mem.RAM);
    L(mem.WRAM_share);
    L(mem.WRAM_arm7);
    L(mem.oam);
    L(mem.palette);
    L(mem.vram.data);
    for (u32 i = 0; i < 9; i++) {
        L(mem.vram.io.bank[i].mst);
        L(mem.vram.io.bank[i].ofs);
        u8 en; L(en);
        mem.vram.io.bank[i].enable = en != 0;
    }
    L(mem.io.RAM7.base); L(mem.io.RAM7.mask); L(mem.io.RAM7.disabled); L(mem.io.RAM7.val);
    L(mem.io.RAM9.base); L(mem.io.RAM9.mask); L(mem.io.RAM9.disabled); L(mem.io.RAM9.val);
}

// ============================================================
// Clock
// ============================================================
void core::serialize_clock(serialized_state &state) {
    state.new_section("clock", SS_clock, 1);
    S(clock.frame_start_cycle);
    S(clock.cycles_left_this_frame);
    S(clock.master_cycle_count7);
    S(clock.master_cycle_count9);
    S(clock.master_frame);
    S(clock.ppu.x); S(clock.ppu.y);
    S(clock.ppu.scanline_start);
    S(clock.ppu.hblank_active); S(clock.ppu.vblank_active);
    S(clock.cycles7); S(clock.cycles9);
    S(waitstates.current_transaction);
    S(waitstates.current_shift);
}

void core::deserialize_clock(serialized_state &state) {
    L(clock.frame_start_cycle);
    L(clock.cycles_left_this_frame);
    L(clock.master_cycle_count7);
    L(clock.master_cycle_count9);
    L(clock.master_frame);
    L(clock.ppu.x); L(clock.ppu.y);
    L(clock.ppu.scanline_start);
    L(clock.ppu.hblank_active); L(clock.ppu.vblank_active);
    L(clock.cycles7); L(clock.cycles9);
    L(waitstates.current_transaction);
    L(waitstates.current_shift);
}

// ============================================================
// CPUs
// ============================================================
void core::serialize_arm7(serialized_state &state) {
    state.new_section("arm7", SS_arm7, 1);
    arm7.serialize(state);
}
void core::deserialize_arm7(serialized_state &state) {
    arm7.deserialize(state);
}

void core::serialize_arm9(serialized_state &state) {
    state.new_section("arm9", SS_arm9, 1);
    arm9.serialize(state);
}
void core::deserialize_arm9(serialized_state &state) {
    arm9.deserialize(state);
}

// ============================================================
// IO registers
// ============================================================
void core::serialize_io(serialized_state &state) {
    state.new_section("io", SS_io, 1);
    // IPC FIFOs + control
    S(io.ipc.to_arm7.data); S(io.ipc.to_arm7.head); S(io.ipc.to_arm7.tail);
    S(io.ipc.to_arm7.last); S(io.ipc.to_arm7.len); S(io.ipc.to_arm7.enable);
    S(io.ipc.to_arm9.data); S(io.ipc.to_arm9.head); S(io.ipc.to_arm9.tail);
    S(io.ipc.to_arm9.last); S(io.ipc.to_arm9.len); S(io.ipc.to_arm9.enable);
    S(io.ipc.arm7); S(io.ipc.arm9);
    S(io.ipc.arm7sync); S(io.ipc.arm9sync);
    // IRQ, EXMEM, buttons
    S(io.arm7); S(io.arm9);
    // Rights, div, sqrt, sio, powcnt
    S(io.rights);
    S(io.div); S(io.sqrt);
    S(io.sio); S(io.powcnt);
    // Open bus, POSTFLG, DMA fill
    S(io.open_bus); S(io.POSTFLG);
    S(io.dma.filldata);
    // RTC
    S(io.rtc);
}
void core::deserialize_io(serialized_state &state) {
    L(io.ipc.to_arm7.data); L(io.ipc.to_arm7.head); L(io.ipc.to_arm7.tail);
    L(io.ipc.to_arm7.last); L(io.ipc.to_arm7.len); L(io.ipc.to_arm7.enable);
    L(io.ipc.to_arm9.data); L(io.ipc.to_arm9.head); L(io.ipc.to_arm9.tail);
    L(io.ipc.to_arm9.last); L(io.ipc.to_arm9.len); L(io.ipc.to_arm9.enable);
    L(io.ipc.arm7); L(io.ipc.arm9);
    L(io.ipc.arm7sync); L(io.ipc.arm9sync);
    L(io.arm7); L(io.arm9);
    L(io.rights);
    L(io.div); L(io.sqrt);
    L(io.sio); L(io.powcnt);
    L(io.open_bus); L(io.POSTFLG);
    L(io.dma.filldata);
    L(io.rtc);
}

// ============================================================
// SPI
// ============================================================
void core::serialize_spi(serialized_state &state) {
    state.new_section("spi", SS_spi, 1);
    S(spi.cnt.u); S(spi.enable); S(spi.busy_until);
    S(spi.input); S(spi.output); S(spi.chipsel);
    S(spi.pwm); S(spi.firmware);
    // Touchscreen (skip pio pointer — it's rewired from IOs vector)
    S(spi.touchscr.cnt.u); S(spi.touchscr.result); S(spi.touchscr.pos);
    S(spi.touchscr.touch_x); S(spi.touchscr.touch_y); S(spi.touchscr.hold);
    S(spi.touchscr.adc_x_top_left); S(spi.touchscr.adc_y_top_left);
    S(spi.touchscr.adc_x_delta); S(spi.touchscr.adc_y_delta);
    S(spi.touchscr.screen_x_top_left); S(spi.touchscr.screen_y_top_left);
    S(spi.touchscr.screen_x_delta); S(spi.touchscr.screen_y_delta);
    S(spi.irq_id); S(spi.irq_scheduled); S(spi.hold);
}
void core::deserialize_spi(serialized_state &state) {
    L(spi.cnt.u); L(spi.enable); L(spi.busy_until);
    L(spi.input); L(spi.output); L(spi.chipsel);
    L(spi.pwm); L(spi.firmware);
    L(spi.touchscr.cnt.u); L(spi.touchscr.result); L(spi.touchscr.pos);
    L(spi.touchscr.touch_x); L(spi.touchscr.touch_y); L(spi.touchscr.hold);
    L(spi.touchscr.adc_x_top_left); L(spi.touchscr.adc_y_top_left);
    L(spi.touchscr.adc_x_delta); L(spi.touchscr.adc_y_delta);
    L(spi.touchscr.screen_x_top_left); L(spi.touchscr.screen_y_top_left);
    L(spi.touchscr.screen_x_delta); L(spi.touchscr.screen_y_delta);
    L(spi.irq_id); L(spi.irq_scheduled); L(spi.hold);
}

// ============================================================
// DMA
// ============================================================
static void save_dma_ch(DMA_ch &ch, serialized_state &state) {
    Sadd(state, &ch.active, sizeof(ch.active));
    Sadd(state, &ch.num, sizeof(ch.num));
    Sadd(state, &ch.io, sizeof(ch.io));
    Sadd(state, &ch.op, sizeof(ch.op));
    Sadd(state, &ch.run_counter, sizeof(ch.run_counter));
}
static void load_dma_ch(DMA_ch &ch, serialized_state &state) {
    Sload(state, &ch.active, sizeof(ch.active));
    Sload(state, &ch.num, sizeof(ch.num));
    Sload(state, &ch.io, sizeof(ch.io));
    Sload(state, &ch.op, sizeof(ch.op));
    Sload(state, &ch.run_counter, sizeof(ch.run_counter));
}

void core::serialize_dma(serialized_state &state) {
    state.new_section("dma", SS_dma, 1);
    for (auto &ch : dma7) save_dma_ch(ch, state);
    for (auto &ch : dma9) save_dma_ch(ch, state);
}
void core::deserialize_dma(serialized_state &state) {
    for (auto &ch : dma7) load_dma_ch(ch, state);
    for (auto &ch : dma9) load_dma_ch(ch, state);
}

// ============================================================
// Timers
// ============================================================
static void save_timer7(timer7_t &t, serialized_state &state) {
    Sadd(state, &t.divider, sizeof(t.divider));
    Sadd(state, &t.shift, sizeof(t.shift));
    Sadd(state, &t.enable_at, sizeof(t.enable_at));
    Sadd(state, &t.overflow_at, sizeof(t.overflow_at));
    Sadd(state, &t.sch_id, sizeof(t.sch_id));
    Sadd(state, &t.sch_scheduled_still, sizeof(t.sch_scheduled_still));
    Sadd(state, &t.cascade, sizeof(t.cascade));
    Sadd(state, &t.val_at_stop, sizeof(t.val_at_stop));
    Sadd(state, &t.irq_on_overflow, sizeof(t.irq_on_overflow));
    Sadd(state, &t.reload, sizeof(t.reload));
    Sadd(state, &t.reload_ticks, sizeof(t.reload_ticks));
}
static void load_timer7(timer7_t &t, serialized_state &state) {
    Sload(state, &t.divider, sizeof(t.divider));
    Sload(state, &t.shift, sizeof(t.shift));
    Sload(state, &t.enable_at, sizeof(t.enable_at));
    Sload(state, &t.overflow_at, sizeof(t.overflow_at));
    Sload(state, &t.sch_id, sizeof(t.sch_id));
    Sload(state, &t.sch_scheduled_still, sizeof(t.sch_scheduled_still));
    Sload(state, &t.cascade, sizeof(t.cascade));
    Sload(state, &t.val_at_stop, sizeof(t.val_at_stop));
    Sload(state, &t.irq_on_overflow, sizeof(t.irq_on_overflow));
    Sload(state, &t.reload, sizeof(t.reload));
    Sload(state, &t.reload_ticks, sizeof(t.reload_ticks));
}
static void save_timer9(timer9_t &t, serialized_state &state) {
    Sadd(state, &t.divider, sizeof(t.divider));
    Sadd(state, &t.shift, sizeof(t.shift));
    Sadd(state, &t.enable_at, sizeof(t.enable_at));
    Sadd(state, &t.overflow_at, sizeof(t.overflow_at));
    Sadd(state, &t.sch_id, sizeof(t.sch_id));
    Sadd(state, &t.sch_scheduled_still, sizeof(t.sch_scheduled_still));
    Sadd(state, &t.cascade, sizeof(t.cascade));
    Sadd(state, &t.val_at_stop, sizeof(t.val_at_stop));
    Sadd(state, &t.irq_on_overflow, sizeof(t.irq_on_overflow));
    Sadd(state, &t.reload, sizeof(t.reload));
    Sadd(state, &t.reload_ticks, sizeof(t.reload_ticks));
}
static void load_timer9(timer9_t &t, serialized_state &state) {
    Sload(state, &t.divider, sizeof(t.divider));
    Sload(state, &t.shift, sizeof(t.shift));
    Sload(state, &t.enable_at, sizeof(t.enable_at));
    Sload(state, &t.overflow_at, sizeof(t.overflow_at));
    Sload(state, &t.sch_id, sizeof(t.sch_id));
    Sload(state, &t.sch_scheduled_still, sizeof(t.sch_scheduled_still));
    Sload(state, &t.cascade, sizeof(t.cascade));
    Sload(state, &t.val_at_stop, sizeof(t.val_at_stop));
    Sload(state, &t.irq_on_overflow, sizeof(t.irq_on_overflow));
    Sload(state, &t.reload, sizeof(t.reload));
    Sload(state, &t.reload_ticks, sizeof(t.reload_ticks));
}

void core::serialize_timers(serialized_state &state) {
    state.new_section("timers", SS_timers, 1);
    for (auto &t : timer7) save_timer7(t, state);
    for (auto &t : timer9) save_timer9(t, state);
}
void core::deserialize_timers(serialized_state &state) {
    for (auto &t : timer7) load_timer7(t, state);
    for (auto &t : timer9) load_timer9(t, state);
}

// ============================================================
// APU
// ============================================================
void core::serialize_apu(serialized_state &state) {
    state.new_section("apu", SS_apu, 1);
    for (auto &ch : apu.CH) {
        S(ch.dirty); S(ch.sample); S(ch.scheduled); S(ch.schedule_id);
        S(ch.status); S(ch.adpcm); S(ch.io);
        u8 psg = ch.has_psg ? 1 : 0;
        u8 noise = ch.has_noise ? 1 : 0;
        u8 cap = ch.has_cap ? 1 : 0;
        S(psg); S(noise); S(cap);
        S(ch.lfsr);
    }
    // soundcap[2]
    for (auto &sc : apu.soundcap) {
        S(sc.status); S(sc.cap_source); S(sc.repeat_mode); S(sc.format);
        S(sc.pos); S(sc.scheduler_id); S(sc.scheduled); S(sc.ctrl_src);
        S(sc.dest_addr); S(sc.len_words); S(sc.len_bytes);
    }
    S(apu.io);
    S(apu.left_output); S(apu.right_output);
    S(apu.sample_cycles); S(apu.next_sample); S(apu.total_samples);
}
void core::deserialize_apu(serialized_state &state) {
    for (auto &ch : apu.CH) {
        L(ch.dirty); L(ch.sample); L(ch.scheduled); L(ch.schedule_id);
        L(ch.status); L(ch.adpcm); L(ch.io);
        u8 psg, noise, cap;
        L(psg); L(noise); L(cap);
        ch.has_psg = psg != 0;
        ch.has_noise = noise != 0;
        ch.has_cap = cap != 0;
        L(ch.lfsr);
    }
    for (auto &sc : apu.soundcap) {
        L(sc.status); L(sc.cap_source); L(sc.repeat_mode); L(sc.format);
        L(sc.pos); L(sc.scheduler_id); L(sc.scheduled); L(sc.ctrl_src);
        L(sc.dest_addr); L(sc.len_words); L(sc.len_bytes);
    }
    L(apu.io);
    L(apu.left_output); L(apu.right_output);
    L(apu.sample_cycles); L(apu.next_sample); L(apu.total_samples);
    // Mark all channels unscheduled — post_deserialize will reschedule active ones
    for (auto &ch : apu.CH) ch.scheduled = 0;
}

// ============================================================
// PPU
// ============================================================
static void save_eng2d(PPU::ENG2D &eng, serialized_state &state) {
    // mem (palette/oam copies)
    Sadd(state, &eng.mem, sizeof(eng.mem));
    // io
    Sadd(state, &eng.io, sizeof(eng.io));
    // blend
    Sadd(state, &eng.blend, sizeof(eng.blend));
    // BG[4] (all POD, no pointers)
    for (auto &bg : eng.BG) Sadd(state, &bg, sizeof(bg));
    // mosaic
    Sadd(state, &eng.mosaic, sizeof(eng.mosaic));
    // window[4]
    for (auto &w : eng.window) Sadd(state, &w, sizeof(w));
    // obj
    Sadd(state, &eng.obj, sizeof(eng.obj));
    // num, enable
    Sadd(state, &eng.num, sizeof(eng.num));
    Sadd(state, &eng.enable, sizeof(eng.enable));
}
static void load_eng2d(PPU::ENG2D &eng, serialized_state &state) {
    Sload(state, &eng.mem, sizeof(eng.mem));
    Sload(state, &eng.io, sizeof(eng.io));
    Sload(state, &eng.blend, sizeof(eng.blend));
    for (auto &bg : eng.BG) Sload(state, &bg, sizeof(bg));
    Sload(state, &eng.mosaic, sizeof(eng.mosaic));
    for (auto &w : eng.window) Sload(state, &w, sizeof(w));
    Sload(state, &eng.obj, sizeof(eng.obj));
    Sload(state, &eng.num, sizeof(eng.num));
    Sload(state, &eng.enable, sizeof(eng.enable));
}

void core::serialize_ppu(serialized_state &state) {
    state.new_section("ppu", SS_ppu, 1);
    S(ppu.io);
    S(ppu.line_a); S(ppu.line_b);
    u8 dc = ppu.doing_capture ? 1 : 0;
    S(dc);
    S(ppu.mosaic);
    save_eng2d(ppu.eng2d[0], state);
    save_eng2d(ppu.eng2d[1], state);
}
void core::deserialize_ppu(serialized_state &state) {
    L(ppu.io);
    L(ppu.line_a); L(ppu.line_b);
    u8 dc; L(dc);
    ppu.doing_capture = dc != 0;
    L(ppu.mosaic);
    load_eng2d(ppu.eng2d[0], state);
    load_eng2d(ppu.eng2d[1], state);
    // memp (VRAM pointer arrays) are rebuilt by VRAM_resetup_banks in post_deserialize
}

// ============================================================
// GE (Geometry Engine)
// ============================================================
static void save_vtx_list_len(GFX::VTX_list &l, serialized_state &state) {
    // Only save the count — at frame boundary these are empty; list is rebuilt
    Sadd(state, &l.len, sizeof(l.len));
}
static void load_vtx_list_len(GFX::VTX_list &l, serialized_state &state) {
    i32 dummy; Sload(state, &dummy, sizeof(dummy));
    l.init(); // reset to empty; rebuilt by game next frame
}

void core::serialize_ge(serialized_state &state) {
    state.new_section("ge", SS_ge, 1);
    S(ge.enable);
    S(ge.ge_has_buffer);
    S(ge.clip_mtx_dirty);
    S(ge.winding_order);

    // Matrix stacks (all plain i32 arrays)
    S(ge.matrices.stacks.projection_ptr);
    S(ge.matrices.stacks.view_vector_ptr);
    S(ge.matrices.stacks.texture_ptr);
    S(ge.matrices.stacks.view);
    S(ge.matrices.stacks.vector);
    S(ge.matrices.stacks.projection);
    S(ge.matrices.stacks.texture);
    S(ge.matrices.view); S(ge.matrices.texture);
    S(ge.matrices.projection); S(ge.matrices.vector);
    S(ge.matrices.clip);

    // IO
    S(ge.io);

    // Lights & material
    S(ge.lights);

    // Current command results
    S(ge.results);

    // Params — scalar parts only (vtx lists rebuilt by game)
    S(ge.params.poly.on_vtx_start);
    S(ge.params.poly.current);
    S(ge.params.vtx.color);
    S(ge.params.vtx.uv); S(ge.params.vtx.original_uv);
    S(ge.params.vtx.x); S(ge.params.vtx.y);
    S(ge.params.vtx.z); S(ge.params.vtx.w);
    S(ge.params.vtx_strip.mode);
    // save input_list and debug_input_list as length-only (rebuilt by game)
    save_vtx_list_len(ge.params.vtx.input_list, state);
    save_vtx_list_len(ge.params.vtx.debug_input_list, state);

    // FIFO
    S(ge.fifo.items);
    S(ge.fifo.head); S(ge.fifo.tail); S(ge.fifo.len);
    S(ge.fifo.total_complete_cmds);
    u8 pausing = ge.fifo.pausing_cpu ? 1 : 0;
    S(pausing);
    S(ge.fifo.cmd_scheduled);
    u8 waiting = ge.fifo.waiting_for_cmd ? 1 : 0;
    S(waiting);
    S(ge.fifo.cur_cmd);
    S(ge.fifo.cmd_queue);

    // cur_cmd data buffer
    S(ge.cur_cmd.data);
}

void core::deserialize_ge(serialized_state &state) {
    L(ge.enable);
    L(ge.ge_has_buffer);
    L(ge.clip_mtx_dirty);
    L(ge.winding_order);

    L(ge.matrices.stacks.projection_ptr);
    L(ge.matrices.stacks.view_vector_ptr);
    L(ge.matrices.stacks.texture_ptr);
    L(ge.matrices.stacks.view);
    L(ge.matrices.stacks.vector);
    L(ge.matrices.stacks.projection);
    L(ge.matrices.stacks.texture);
    L(ge.matrices.view); L(ge.matrices.texture);
    L(ge.matrices.projection); L(ge.matrices.vector);
    L(ge.matrices.clip);

    L(ge.io);
    L(ge.lights);
    L(ge.results);

    L(ge.params.poly.on_vtx_start);
    L(ge.params.poly.current);
    L(ge.params.vtx.color);
    L(ge.params.vtx.uv); L(ge.params.vtx.original_uv);
    L(ge.params.vtx.x); L(ge.params.vtx.y);
    L(ge.params.vtx.z); L(ge.params.vtx.w);
    L(ge.params.vtx_strip.mode);
    load_vtx_list_len(ge.params.vtx.input_list, state);
    load_vtx_list_len(ge.params.vtx.debug_input_list, state);

    L(ge.fifo.items);
    L(ge.fifo.head); L(ge.fifo.tail); L(ge.fifo.len);
    L(ge.fifo.total_complete_cmds);
    u8 pausing; L(pausing); ge.fifo.pausing_cpu = pausing != 0;
    L(ge.fifo.cmd_scheduled);
    u8 waiting; L(waiting); ge.fifo.waiting_for_cmd = waiting != 0;
    L(ge.fifo.cur_cmd);
    L(ge.fifo.cmd_queue);
    L(ge.cur_cmd.data);

    // Clear polygon buffers — they'll be rebuilt on next frame
    for (auto &buf : ge.buffers) {
        buf.polygon_index = 0;
        buf.vertex_index = 0;
    }
    // Clear GE command-in-progress flag (safe at frame boundary)
    ge.fifo.cmd_scheduled = 0;
}

// ============================================================
// RE (Rendering Engine)
// ============================================================
void core::serialize_re(serialized_state &state) {
    state.new_section("re", SS_re, 1);
    S(re.enable);
    S(re.io);
    // render_list and linebuffers are transient — not saved
}
void core::deserialize_re(serialized_state &state) {
    L(re.enable);
    L(re.io);
    // render_list cleared by reset; linebuffers rebuilt per-frame
    re.render_list.len = 0;
    re.render_list.num_opaque = 0;
    re.render_list.num_translucent = 0;
}

// ============================================================
// Cart
// ============================================================
void core::serialize_cart(serialized_state &state) {
    state.new_section("cart", SS_cart, 1);
    S(cart.sch_id); S(cart.sch_sch);
    S(cart.cmd);
    S(cart.io);
    // backup state (skip store pointer — it's a separate file)
    S(cart.backup.cmd);
    S(cart.backup.data_in);
    S(cart.backup.data_in_pos);
    S(cart.backup.cmd_addr);
    S(cart.backup.data_out);
    S(cart.backup.status);
    S(cart.backup.chipsel);
    S(cart.backup.wrote_since_select);
    S(cart.backup.page_mask);
    S(cart.backup.uh); S(cart.backup.uq);
    S(cart.backup.detect);
    S(cart.rom_busy_until);
    u32 dm = static_cast<u32>(cart.data_mode);
    S(dm);
}
void core::deserialize_cart(serialized_state &state) {
    L(cart.sch_id); L(cart.sch_sch);
    L(cart.cmd);
    L(cart.io);
    L(cart.backup.cmd);
    L(cart.backup.data_in);
    L(cart.backup.data_in_pos);
    L(cart.backup.cmd_addr);
    L(cart.backup.data_out);
    L(cart.backup.status);
    L(cart.backup.chipsel);
    L(cart.backup.wrote_since_select);
    L(cart.backup.page_mask);
    L(cart.backup.uh); L(cart.backup.uq);
    L(cart.backup.detect);
    L(cart.rom_busy_until);
    u32 dm; L(dm);
    cart.data_mode = static_cast<CART::data_modes>(dm);
}

// ============================================================
// Audio timing
// ============================================================
void core::serialize_audio(serialized_state &state) {
    state.new_section("audio", SS_audio, 1);
    S(audio.master_cycles_per_audio_sample);
    S(audio.next_sample_cycle);
    S(audio.master_cycles_per_max_sample);
    S(audio.master_cycles_per_min_sample);
    S(audio.next_sample_cycle_max);
    S(audio.next_sample_cycle_min);
}
void core::deserialize_audio(serialized_state &state) {
    L(audio.master_cycles_per_audio_sample);
    L(audio.next_sample_cycle);
    L(audio.master_cycles_per_max_sample);
    L(audio.master_cycles_per_min_sample);
    L(audio.next_sample_cycle_max);
    L(audio.next_sample_cycle_min);
}

// ============================================================
// Post-deserialize: rewire pointers and reschedule events
// ============================================================
void core::post_deserialize()
{
    // 0. Zero the waitstate transaction counter so current7()/current9() return
    //    clean values from master_cycle_count7/9 alone. The saved non-zero value
    //    was mid-frame state that no longer applies once the scheduler is rebuilt.
    waitstates.current_transaction = 0;
    waitstates.current_shift = 0;

    // 1. Rewire memory bus function pointers
    map_memory();

    // 2. Re-establish VRAM bank mappings (also rewires ppu.eng2d[*].memp)
    VRAM_resetup_banks();

    // 3. Invalidate all JIT/block caches (stale after state load)
    mem.RAM_block_cache.clear_all_blocks();
    mem.WRAM_share_block_cache.clear_all_blocks();
    mem.WRAM_arm7_block_cache.clear_all_blocks();
    mem.bios7_block_cache.clear_all_blocks();
    mem.bios9_block_cache.clear_all_blocks();
    mem.vram.block_cache.clear_all_blocks();
    arm9.arm9_block_cache.reset();

    // 4. Rewire PPU display pointers
    ppu.display_top = &ppu.display_top_ptr.get().display;
    ppu.display_bot = &ppu.display_bot_ptr.get().display;
    ppu.cur_output_top = static_cast<u32 *>(ppu.display_top->output[ppu.display_top->active_draw_buffer]);
    ppu.cur_output_bot = static_cast<u32 *>(ppu.display_bot->output[ppu.display_bot->active_draw_buffer]);

    // 5. Rebuild the scheduler from scratch
    scheduler.clear();

    // Schedule PPU scanline events from current scanline forward
    u64 cur_clock = clock.frame_start_cycle +
                    (u64)clock.ppu.y * clock.timing.scanline.cycles_total;
    for (u32 line = clock.ppu.y; line < clock.timing.scanline.number; line++) {
        if (line == clock.timing.frame.vblank_up_on)
            scheduler.only_add_abs(cur_clock, 1, this,
                &PPU::core::vblank<false>, &PPU::core::vblank<true>, nullptr);
        if (line == clock.timing.frame.vblank_down_on)
            scheduler.only_add_abs(cur_clock, 0, this,
                &PPU::core::vblank<false>, &PPU::core::vblank<true>, nullptr);
        scheduler.only_add_abs(cur_clock, 0, this,
            &PPU::core::hblank<false>, &PPU::core::hblank<true>, nullptr);
        scheduler.only_add_abs(cur_clock + clock.timing.scanline.cycle_of_hblank, 1, this,
            PPU::core::hblank<false>, PPU::core::hblank<true>, nullptr);
        cur_clock += clock.timing.scanline.cycles_total;
    }
    // Frame-end tag
    scheduler.only_add_abs_w_tag(
        clock.frame_start_cycle + clock.timing.frame.cycles, 0, this,
        &do_next_scheduled_frame, nullptr, 1);

    // APU master sample callback
    scheduler.only_add_abs(static_cast<i64>(apu.next_sample), 0, &apu,
        &APU::core::master_sample_callback, nullptr);

    // APU per-channel callbacks (reschedule at current time + interval)
    for (auto &ch : apu.CH) {
        if (ch.io.status && ch.status.sampling_interval > 0) {
            ch.schedule_id = scheduler.only_add_abs(
                static_cast<i64>(clock.master_cycle_count7 + ch.status.sampling_interval),
                ch.num, &ch, &APU::MCH::run, &ch.scheduled);
            ch.scheduled = 1;
        }
    }

    // Timers
    for (auto &t : timer7) {
        t.sch_id = 0;
        t.sch_scheduled_still = 0;
        if (!t.cascade && t.overflow_at != 0xFFFFFFFFFFFFFFFFULL &&
            t.overflow_at > clock.master_cycle_count7) {
            t.sch_id = scheduler.only_add_abs(static_cast<i64>(t.overflow_at), t.num, this,
                &timer7_t::overflow_callback<false>, &timer7_t::overflow_callback<true>,
                &t.sch_scheduled_still);
        }
    }
    for (auto &t : timer9) {
        t.sch_id = 0;
        t.sch_scheduled_still = 0;
        if (!t.cascade && t.overflow_at != 0xFFFFFFFFFFFFFFFFULL &&
            t.overflow_at > clock.master_cycle_count9) {
            t.sch_id = scheduler.only_add_abs(static_cast<i64>(t.overflow_at), t.num, this,
                &timer9_t::overflow_callback<false>, &timer9_t::overflow_callback<true>,
                &t.sch_scheduled_still);
        }
    }

    // Cart transfer (if one was in progress)
    if (cart.sch_sch) {
        cart.sch_sch = 0;
        scheduler.add_or_run_abs<false>(cart.rom_busy_until,
            CART::ANB_after_read, &cart,
            &CART::ridge::check_transfer<false>, &CART::ridge::check_transfer<true>,
            &cart.sch_sch);
    }

    // SPI IRQ (if one was scheduled)
    if (spi.irq_id) {
        spi.irq_id = 0;
        spi.irq_id = scheduler.add_or_run_abs<false>(spi.busy_until, 0, this,
            &core::SPI_irq<false>, &core::SPI_irq<true>, nullptr);
    }

    // RTC (always reschedule tick)
    scheduler.add_or_run_abs<false>(clock.master_cycle_count7 + 32768, 0, this,
        &core::RTC_tick<false>, &core::RTC_tick<true>, nullptr);
}

// ============================================================
// Public save_state / load_state
// ============================================================
void core::save_state(serialized_state &state)
{
    state.version = 1;
    state.opt.len = 0;

    serialize_memory(state);
    serialize_clock(state);
    serialize_arm7(state);
    serialize_arm9(state);
    serialize_io(state);
    serialize_spi(state);
    serialize_dma(state);
    serialize_timers(state);
    serialize_apu(state);
    serialize_ppu(state);
    serialize_ge(state);
    serialize_re(state);
    serialize_cart(state);
    serialize_audio(state);
}

void core::load_state(serialized_state &state, deserialize_ret &r)
{
    state.iter.offset = 0;

    for (u32 i = 0; i < state.sections.size(); i++) {
        serialized_state_section &sec = state.sections.at(i);
        state.iter.offset = sec.offset;
        switch (sec.kind) {
            case SS_memory:  deserialize_memory(state);  break;
            case SS_clock:   deserialize_clock(state);   break;
            case SS_arm7:    deserialize_arm7(state);    break;
            case SS_arm9:    deserialize_arm9(state);    break;
            case SS_io:      deserialize_io(state);      break;
            case SS_spi:     deserialize_spi(state);     break;
            case SS_dma:     deserialize_dma(state);     break;
            case SS_timers:  deserialize_timers(state);  break;
            case SS_apu:     deserialize_apu(state);     break;
            case SS_ppu:     deserialize_ppu(state);     break;
            case SS_ge:      deserialize_ge(state);      break;
            case SS_re:      deserialize_re(state);      break;
            case SS_cart:    deserialize_cart(state);    break;
            case SS_audio:   deserialize_audio(state);   break;
            default: break;
        }
    }

    post_deserialize();
}

#undef S
#undef L

} // namespace NDS
