//
// Created by . on 12/2/25.
//

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

#include "gba_serialize.h"
#include "gba_bus.h"
#include "helpers/present/sys_present.h"
#include "helpers/serialize/serialize.h"

namespace GBA {

void core::serialize_console(serialized_state &state)
{
    state.new_section("console", SS_console, 1);
#define S(x) Sadd(state, &( x), sizeof( x))
    S(waitstates.current_transaction);
    S(waitstates.io);
    S(WRAM_slow);
    S(WRAM_fast);
    S(BIOS.has);
    S(BIOS.data);
    S(io);
    S(jsm.cycles_left);
    S(scanline_cycles_to_execute);
    S(audio.master_cycles_per_audio_sample);
    S(audio.master_cycles_per_min_sample);
    S(audio.master_cycles_per_max_sample);
    S(audio.next_sample_cycle_max);
    S(audio.next_sample_cycle_min);
    S(audio.next_sample_cycle);
#undef S
}

void core::serialize_clock(serialized_state &state)
{
    state.new_section("clock", SS_clock, 1);
#define S(x) Sadd(state, &(clock. x), sizeof(clock. x))
    S(master_cycle_count);
    S(master_frame);
    S(ppu.x);
    S(ppu.y);
    S(ppu.scanline_start);
    S(ppu.hblank_active);
    S(ppu.vblank_active);
#undef S
}

void core::serialize_cpu(serialized_state &state)
{
    state.new_section("arm7tdmi", SS_cpu, 1);
    cpu.serialize(state);
}

void core::serialize_ppu(serialized_state &state)
{
    state.new_section("ppu", SS_ppu, 1);
#define S(x) Sadd(state, &(ppu. x), sizeof(ppu. x))
    S(palette_RAM);
    S(VRAM);
    S(OAM);
    S(mosaic);
    S(blend);
    S(io);
    S(mbg);
    S(window);
    S(obj);
#undef S

    u32 active_draw_buffer = ppu.display ? (ppu.display->active_draw_buffer & 1) : 0;
    u32 cur_output_index = active_draw_buffer;
    if (ppu.display && (ppu.cur_output == static_cast<u16 *>(ppu.display->output[0]))) cur_output_index = 0;
    if (ppu.display && (ppu.cur_output == static_cast<u16 *>(ppu.display->output[1]))) cur_output_index = 1;
    active_draw_buffer = cur_output_index;
    Sadd(state, &active_draw_buffer, sizeof(active_draw_buffer));
    Sadd(state, &cur_output_index, sizeof(cur_output_index));
}

static void serialize_fifo(APU::FIFO &fifo, serialized_state &state)
{
#define S(x) Sadd(state, &(fifo. x), sizeof(fifo. x))
    S(timer_id);
    S(head);
    S(tail);
    S(len);
    S(output_head);
    S(data);
    S(sample);
    S(output);
    S(enable_l);
    S(enable_r);
    S(vol);
    S(needs_commit);
#undef S
}

static void serialize_channel(APU::CHANNEL &channel, serialized_state &state)
{
#define S(x) Sadd(state, &(channel. x), sizeof(channel. x))
    S(dac_on);
    S(vol);
    S(on);
    S(left);
    S(right);
    S(enable_l);
    S(enable_r);
    S(period);
    S(next_period);
    S(length_enable);
    S(period_counter);
    S(wave_duty);
    S(polarity);
    S(duty_counter);
    S(short_mode);
    S(divisor);
    S(clock_shift);
    S(length_counter);
    S(samples);
    S(sample_bank_mode);
    S(sample_sample_bank);
    S(cur_sample_bank);
    S(sample_buffer);
    S(sweep);
    S(env);
#undef S
}

void core::serialize_apu(serialized_state &state)
{
    state.new_section("apu", SS_apu, 1);
    serialize_fifo(apu.fifo[0], state);
    serialize_fifo(apu.fifo[1], state);
#define S(x) Sadd(state, &(apu. x), sizeof(apu. x))
    S(clocks);
    S(output);
    S(psg);
    S(io);
    S(divider2);
#undef S
    for (auto &channel : apu.channels) {
        serialize_channel(channel, state);
    }
}

void core::serialize_cartridge(serialized_state &state)
{
    state.new_section("cartridge", SS_cartridge, 1);
#define S(x) Sadd(state, &(cart. x), sizeof(cart. x))
    S(last_read);
    S(RAM.mask);
    S(RAM.size);
    S(RAM.present);
    S(RAM.persists);
    S(RAM.is_sram);
    S(RAM.is_flash);
    S(RAM.is_eeprom);
    S(RAM.flash);
    S(RAM.eeprom);
    S(RTC);
    S(prefetch);
#undef S

    u64 requested_size = cart.RAM.store ? cart.RAM.store->requested_size : 0;
    u64 actual_size = (cart.RAM.store && cart.RAM.store->data) ? cart.RAM.store->actual_size : 0;
    Sadd(state, &requested_size, sizeof(requested_size));
    Sadd(state, &actual_size, sizeof(actual_size));
    if (cart.RAM.store && cart.RAM.store->data && actual_size > 0) {
        Sadd(state, cart.RAM.store->data, actual_size);
    }
}

void core::serialize_dma(serialized_state &state)
{
    state.new_section("dma", SS_dma, 1);
    for (auto &ch : dma.channel) {
#define S(x) Sadd(state, &(ch. x), sizeof(ch. x))
        S(io);
        S(latch);
        S(is_sound);
        S(src_add);
        S(dest_add);
        S(word_mask);
#undef S
    }
    Sadd(state, &dma.bit_mask, sizeof(dma.bit_mask));
}

void core::serialize_timers(serialized_state &state)
{
    state.new_section("timers", SS_timers, 1);
    for (auto &tm : timer) {
#define S(x) Sadd(state, &(tm. x), sizeof(tm. x))
        S(divider);
        S(shift);
        S(enable_at);
        S(overflow_at);
        S(cascade);
        S(val_at_stop);
        S(irq_on_overflow);
        S(reload);
        S(reload_ticks);
#undef S
    }
}

void core::save_state(serialized_state &state)
{
    state.version = 1;
    state.opt.len = 0;

    state.has_screenshot = 1;
    state.screenshot.allocate(240, 160);
    state.screenshot.clear();
    GBA_present(ppu.display_ptr.get(), state.screenshot.data.ptr, 240, 160, 0);

    serialize_console(state);
    serialize_clock(state);
    serialize_cpu(state);
    serialize_ppu(state);
    serialize_apu(state);
    serialize_cartridge(state);
    serialize_dma(state);
    serialize_timers(state);
}

void core::deserialize_console(serialized_state &state)
{
#define L(x) Sload(state, &( x), sizeof( x))
    L(waitstates.current_transaction);
    L(waitstates.io);
    L(WRAM_slow);
    L(WRAM_fast);
    L(BIOS.has);
    L(BIOS.data);
    L(io);
    L(jsm.cycles_left);
    L(scanline_cycles_to_execute);
    L(audio.master_cycles_per_audio_sample);
    L(audio.master_cycles_per_min_sample);
    L(audio.master_cycles_per_max_sample);
    L(audio.next_sample_cycle_max);
    L(audio.next_sample_cycle_min);
    L(audio.next_sample_cycle);
#undef L
}

void core::deserialize_clock(serialized_state &state)
{
#define L(x) Sload(state, &(clock. x), sizeof(clock. x))
    L(master_cycle_count);
    L(master_frame);
    L(ppu.x);
    L(ppu.y);
    L(ppu.scanline_start);
    L(ppu.hblank_active);
    L(ppu.vblank_active);
#undef L
}

void core::deserialize_cpu(serialized_state &state)
{
    cpu.deserialize(state);
}

void core::deserialize_ppu(serialized_state &state)
{
#define L(x) Sload(state, &(ppu. x), sizeof(ppu. x))
    L(palette_RAM);
    L(VRAM);
    L(OAM);
    L(mosaic);
    L(blend);
    L(io);
    L(mbg);
    L(window);
    L(obj);
#undef L

    u32 active_draw_buffer = 0;
    u32 cur_output_index = 0;
    Sload(state, &active_draw_buffer, sizeof(active_draw_buffer));
    Sload(state, &cur_output_index, sizeof(cur_output_index));
    if (ppu.display) {
        const u32 draw_buffer = (cur_output_index <= 1 ? cur_output_index : active_draw_buffer) & 1;
        ppu.display->active_draw_buffer = draw_buffer;
        ppu.cur_output = static_cast<u16 *>(ppu.display->output[ppu.display->active_draw_buffer]);
    }
}

static void deserialize_fifo(APU::FIFO &fifo, serialized_state &state)
{
#define L(x) Sload(state, &(fifo. x), sizeof(fifo. x))
    L(timer_id);
    L(head);
    L(tail);
    L(len);
    L(output_head);
    L(data);
    L(sample);
    L(output);
    L(enable_l);
    L(enable_r);
    L(vol);
    L(needs_commit);
#undef L
}

static void deserialize_channel(APU::CHANNEL &channel, serialized_state &state)
{
#define L(x) Sload(state, &(channel. x), sizeof(channel. x))
    L(dac_on);
    L(vol);
    L(on);
    L(left);
    L(right);
    L(enable_l);
    L(enable_r);
    L(period);
    L(next_period);
    L(length_enable);
    L(period_counter);
    L(wave_duty);
    L(polarity);
    L(duty_counter);
    L(short_mode);
    L(divisor);
    L(clock_shift);
    L(length_counter);
    L(samples);
    L(sample_bank_mode);
    L(sample_sample_bank);
    L(cur_sample_bank);
    L(sample_buffer);
    L(sweep);
    L(env);
#undef L
}

void core::deserialize_apu(serialized_state &state)
{
    deserialize_fifo(apu.fifo[0], state);
    deserialize_fifo(apu.fifo[1], state);
#define L(x) Sload(state, &(apu. x), sizeof(apu. x))
    L(clocks);
    L(output);
    L(psg);
    L(io);
    L(divider2);
#undef L
    for (u32 i = 0; i < 4; i++) {
        deserialize_channel(apu.channels[i], state);
        apu.channels[i].number = i;
    }
}

void core::deserialize_cartridge(serialized_state &state)
{
#define L(x) Sload(state, &(cart. x), sizeof(cart. x))
    L(last_read);
    L(RAM.mask);
    L(RAM.size);
    L(RAM.present);
    L(RAM.persists);
    L(RAM.is_sram);
    L(RAM.is_flash);
    L(RAM.is_eeprom);
    L(RAM.flash);
    L(RAM.eeprom);
    L(RTC);
    L(prefetch);
#undef L

    u64 requested_size = 0;
    u64 actual_size = 0;
    Sload(state, &requested_size, sizeof(requested_size));
    Sload(state, &actual_size, sizeof(actual_size));
    if (cart.RAM.store) {
        cart.RAM.store->requested_size = requested_size;
    }
    if (actual_size > 0) {
        if (cart.RAM.store && cart.RAM.store->data && cart.RAM.store->actual_size >= actual_size) {
            Sload(state, cart.RAM.store->data, actual_size);
            cart.RAM.store->dirty = true;
        }
        else {
            std::vector<u8> tmp(static_cast<size_t>(actual_size));
            Sload(state, tmp.data(), actual_size);
            if (cart.RAM.store && cart.RAM.store->data) {
                const u64 copy_size = std::min(actual_size, cart.RAM.store->actual_size);
                std::memcpy(cart.RAM.store->data, tmp.data(), copy_size);
                cart.RAM.store->dirty = true;
            }
        }
    }
}

void core::deserialize_dma(serialized_state &state)
{
    for (u32 i = 0; i < 4; i++) {
        auto &ch = dma.channel[i];
#define L(x) Sload(state, &(ch. x), sizeof(ch. x))
        L(io);
        L(latch);
        L(is_sound);
        L(src_add);
        L(dest_add);
        L(word_mask);
#undef L
        ch.bus = this;
        ch.num = i;
    }
    Sload(state, &dma.bit_mask, sizeof(dma.bit_mask));
}

void core::deserialize_timers(serialized_state &state)
{
    for (u32 i = 0; i < 4; i++) {
        auto &tm = timer[i];
#define L(x) Sload(state, &(tm. x), sizeof(tm. x))
        L(divider);
        L(shift);
        L(enable_at);
        L(overflow_at);
        L(cascade);
        L(val_at_stop);
        L(irq_on_overflow);
        L(reload);
        L(reload_ticks);
#undef L
        tm.gba = this;
        tm.num = i;
        tm.sch_id = 0;
        tm.sch_scheduled_still = 0;
    }
}

void core::reschedule_timers()
{
    static constexpr u64 never = 0xFFFFFFFFFFFFFFFFULL;
    for (u32 i = 0; i < 4; i++) {
        auto &tm = timer[i];
        tm.sch_id = 0;
        tm.sch_scheduled_still = 0;
        if (tm.enabled() && !tm.cascade && (tm.overflow_at != never)) {
            tm.sch_id = scheduler.only_add_abs(static_cast<i64>(tm.overflow_at), i, this,
                                               &TIMER::timer_overflow, &tm.sch_scheduled_still);
        }
    }
}

void core::post_deserialize()
{
    set_waitstates();

    cart.gba = this;
    ppu.gba = this;
    apu.bus = this;
    dma.bus = this;
    for (u32 i = 0; i < 4; i++) {
        dma.channel[i].bus = this;
        dma.channel[i].num = i;
        dma.channel[i].word_mask = i < 3 ? 0x3FFF : 0xFFFF;
        dma.channel[i].on_modify_write();
        timer[i].gba = this;
        timer[i].num = i;
        apu.channels[i].number = i;
    }

    EWRAM_cache.reset();
    IWRAM_cache.reset();
    for (auto &bl : BIOS_store) {
        bl.instructions.clear();
        bl.sz = 0;
        bl.page_span = 0;
    }
    for (auto &bl : ROM_store) {
        bl.instructions.clear();
        bl.sz = 0;
        bl.page_span = 0;
    }

    eval_irqs();
    scheduler.clear();
    scheduler.max_block_size = 8;
    cpu.cached_max_block_size = scheduler.max_block_size;
    scheduler.run.ptr = this;
    schedule_audio_events();
    ppu.reschedule_from_state();
    reschedule_timers();

    dma.eval_bit_masks();
    if (dma.bit_mask.normal) set_step_dma();
    else if (io.halted) set_step_halted();
    else set_step_cpu();
}

void core::load_state(serialized_state &state, deserialize_ret &ret)
{
    state.iter.offset = 0;
    assert(state.version == 1);

    for (auto &sec : state.sections) {
        state.iter.offset = sec.offset;
        switch (sec.kind) {
            case SS_console:
                deserialize_console(state);
                break;
            case SS_clock:
                deserialize_clock(state);
                break;
            case SS_cpu:
                deserialize_cpu(state);
                break;
            case SS_ppu:
                deserialize_ppu(state);
                break;
            case SS_apu:
                deserialize_apu(state);
                break;
            case SS_cartridge:
                deserialize_cartridge(state);
                break;
            case SS_dma:
                deserialize_dma(state);
                break;
            case SS_timers:
                deserialize_timers(state);
                break;
            default:
                NOGOHERE;
        }
    }

    post_deserialize();
    ret.success = 1;
    ret.reason[0] = 0;
}

}
