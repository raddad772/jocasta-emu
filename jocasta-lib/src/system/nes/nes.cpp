//
// Created by Dave on 2/4/2024.
//
#include <cassert>
#include <cstdlib>
#include <cstdio>

#include "helpers/physical_io.h"
#include "helpers/sys_interface.h"
#include "helpers/debugger/debugger.h"
#include "component/audio/nes_apu/nes_apu.h"

#include "nes.h"
#include "nes_bus.h"
#include "nes_cart.h"
#include "nes_ppu.h"
#include "nes_cpu.h"
#include "nes_debugger.h"
#include "nes_serialize.h"

#define JSM jsm_system* jsm

static u32 read_trace(void *ptr, u32 addr)
{
    auto *nes= static_cast<NES::core *>(ptr);
    return nes->CPU_read(addr, 0, 0);
}

#define APU_CYCLES_PER_FRAME  29780

void NES::core::set_audio_ring(audio_output_ring *ring)
{
    audio.output_ring = ring;
    // NES APU clock ≈ CPU clock ≈ 1,789,773 Hz (one apu_master_clock tick per CPU cycle).
    // Output one sample per APU cycle so the declared sample_rate is matched exactly.
    audio.master_cycles_per_audio_sample = 1.0;
}

jsm_system *NES_new()
{
    auto *nes = new NES::core();
    nes->apu.master_cycles = &nes->clock.master_clock;

    jsm_debug_read_trace dt;
    dt.read_trace = &read_trace;
    dt.ptr = static_cast<void *>(nes);

    nes->cpu.cpu.setup_tracing(&dt, &nes->clock.master_clock);
    snprintf(nes->label, sizeof(nes->label), "Nintendo Entertainment System");

    nes->described_inputs = 0;
    nes->cycles_left = 0;
    nes->display_enabled = 1;
    return nes;
}


void NES_delete(JSM)
{
    auto *nes = static_cast<NES::core *>(jsm);
    for (physical_io_device &pio : nes->IOs) {
        if (pio.kind == HID_CART_PORT) {
            if (pio.cartridge_port.unload_cart) pio.cartridge_port.unload_cart(jsm);
        }
    }
    nes->IOs.clear();

    delete nes;
}

static void NESIO_load_cart(JSM, multi_file_set &mfs, physical_io_device &pio) {
    auto *nes = dynamic_cast<NES::core *>(jsm);
    BUF* b = &mfs.files[0].buf;
    nes->cart.load_ROM_from_RAM(static_cast<char *>(b->ptr), b->size);
    nes->set_which_mapper(nes->cart.header.mapper_number);
    nes->set_cart(pio);
}

static void NESIO_unload_cart(JSM)
{
}

static void setup_crt(JSM_DISPLAY *d)
{
    d->kind = jsm::display_kinds::CRT;
    d->enabled = 1;

    // NTSC crystal 21.477272 MHz, 341 px × 262 lines × 4 = 357368 ... ≈60.0988 Hz
    d->fps = 21477272.0 / (341.0 * 262.0 * 4.0);

    d->pixelometry.cols.left_hblank = 1;
    d->pixelometry.cols.right_hblank = 85;
    d->pixelometry.cols.visible = 256;
    d->pixelometry.cols.max_visible = 256;
    d->pixelometry.offset.x = 1;

    d->pixelometry.rows.top_vblank = 1;
    d->pixelometry.rows.visible = 240;
    d->pixelometry.rows.max_visible = 240;
    d->pixelometry.rows.bottom_vblank = 21;
    d->pixelometry.offset.y = 1;

    //d->geometry.physical_aspect_ratio.width = 4;
    //d->geometry.physical_aspect_ratio.height = 3;
    d->geometry.physical_aspect_ratio.width = 5;
    d->geometry.physical_aspect_ratio.height = 4;

    d->pixelometry.overscan.left = d->pixelometry.overscan.right = d->pixelometry.overscan.top = d->pixelometry.overscan.bottom = 8;
}

void NES::core::setup_audio(std::vector<physical_io_device> &inIOs)
{
    physical_io_device &pio = inIOs.emplace_back();
    pio.init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio.audio_channel;
    // APU clock ≈ CPU clock = 21477272 / 12 = 1,789,772.67 Hz; approximate via frames
    chan->sample_rate = (u32)((double)APU_CYCLES_PER_FRAME * (21477272.0 / (341.0 * 262.0 * 4.0)));
    chan->low_pass_filter = 14000;
}

void NES::core::describe_io()
{
    if (described_inputs) return;
    described_inputs = 1;

    IOs.reserve(15);

    // controllers
    physical_io_device &c1 = IOs.emplace_back(); //0
    c1.init(HID_CONTROLLER, true, true, true, true);
    //physical_io_device *c2 = cvec_push_back(IOs); //1
    NES_joypad::setup_pio(c1, 0, "Player 1", 1);
    //NES_joypad_setup_pio(c2, 1, "Player 2", 0);

    // power and reset buttons
    physical_io_device* chassis = &IOs.emplace_back(); //2
    chassis->init(HID_CHASSIS, 1, 1, 1, 1);
    HID_digital_button* b;

    b = &chassis->chassis.digital_buttons.emplace_back();
    snprintf(b->name, sizeof(b->name), "Power");
    b->state = 1;
    b->common_id = DBCID_ch_power;

    b = &chassis->chassis.digital_buttons.emplace_back();
    b->common_id = DBCID_ch_reset;
    snprintf(b->name, sizeof(b->name), "Reset");
    b->state = 0;

    // cartridge port
    physical_io_device *d = &IOs.emplace_back(); //4
    d->init(HID_CART_PORT, 1, 1, 1, 0);
    d->cartridge_port.load_cart = &NESIO_load_cart;
    d->cartridge_port.unload_cart = &NESIO_unload_cart;

    // screen
    d = &IOs.emplace_back(); //4
    d->init(HID_DISPLAY, 1, 1, 0, 1); //5
    d->display.allocate_output(0, 256 * 240 * 2);
    d->display.allocate_output(1, 256 * 240 * 2);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    ppu.display_ptr = cvec_ptr(IOs, IOs.size()-1);
    ppu.cur_output = static_cast<u16 *>(d->display.output[0]);
    setup_crt(&d->display);
    d->display.active_draw_buffer = 0;
    //d->display.last_displayed = 1;

    setup_audio(IOs);

    cpu.joypad1.devices = &IOs;
    cpu.joypad1.device_index = NES_INPUTS_PLAYER1;
    cpu.joypad2.devices = &IOs;
    cpu.joypad2.device_index = NES_INPUTS_PLAYER2;

    ppu.display = &ppu.display_ptr.get().display;
}

void NES::core::enable_tracing()
{
    // TODO
    assert(1==0);
}

void NES::core::disable_tracing()
{
    // TODO
    assert(1==0);
}

void NES::core::play()
{
}

void NES::core::pause()
{
}

void NES::core::stop()
{
}

void NES::core::get_framevars(framevars& out)
{
    out.master_frame = clock.master_frame;
    out.x = ppu.line_cycle;
    out.scanline = clock.ppu_y;
}

void NES::core::reset()
{
    clock.reset();
    audio.next_sample_cycle = 0;
    cpu.reset();
    ppu.reset();
    apu.reset();
     do_reset();
}


void NES::core::killall()
{

}

void NES::core::sample_audio()
{
    clock.apu_master_clock++;
    if (audio.output_ring && (clock.apu_master_clock >= static_cast<u64>(audio.next_sample_cycle))) {
        audio.next_sample_cycle += audio.master_cycles_per_audio_sample;
        float s = apu.mix_sample(0);
        audio.output_ring->push(s, s);
    }

    if (!::dbg.do_debug) return;

    debug_waveform *dw = &dbg.waveforms.main.get();
    if (clock.master_clock >= dw->user.next_sample_cycle) {
        if (dw->user.buf_pos < dw->samples_requested) {
            dw->user.next_sample_cycle += dw->user.cycle_stride;
            static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = apu.mix_sample(1);
            dw->user.buf_pos++;
        }
    }

    dw = &dbg.waveforms.chan[0].get();
    if (clock.master_clock >= dw->user.next_sample_cycle) {
        for (int j = 0; j < 5; j++) {
            dw = &dbg.waveforms.chan[j].get();
            if (dw->user.buf_pos < dw->samples_requested) {
                dw->user.next_sample_cycle += dw->user.cycle_stride;
                float sv = apu.sample_channel(j);
                static_cast<float *>(dw->buf.ptr)[dw->user.buf_pos] = sv;
                dw->user.buf_pos++;
            }
        }
    }
}


u32 NES::core::finish_frame()
{
    if (::dbg.do_debug && dbg.waveforms.main.vec != nullptr) {
        dbg.waveforms.main.get().setup(APU_CYCLES_PER_FRAME);
        apu.ext_enable = dbg.waveforms.main.get().ch_output_enabled;
        for (u32 i = 0; i < 4; i++) {
            debug_waveform &wf = dbg.waveforms.chan[i].get();
            wf.setup(APU_CYCLES_PER_FRAME);
            apu.channels[i].ext_enable = wf.ch_output_enabled;
        }
        debug_waveform &wf_dmc = dbg.waveforms.chan[4].get();
        wf_dmc.setup(APU_CYCLES_PER_FRAME);
        apu.dmc.ext_enable = wf_dmc.ch_output_enabled;
    }

    if (fake_PRG_RAM.ptr == nullptr && SRAM)
        fake_PRG_RAM.ptr = static_cast<u8 *>(SRAM->data);
    u64 current_frame = clock.master_frame;
    while (clock.master_frame == current_frame) {
        finish_scanline();
        if (::dbg.do_break) break;
    }
    return ppu.display->active_draw_buffer;
}

u32 NES::core::finish_scanline()
{
    i32 cpu_step = static_cast<i32>(clock.timing.cpu_divisor);
    i64 ppu_step = clock.timing.ppu_divisor;
    i32 done = 0;
    i32 start_y = clock.ppu_y;
    while (clock.ppu_y == start_y) {
        clock.master_clock += cpu_step;
        //apu.cycle(clock.master_clock);
        cpu.run_cycle();
        apu.cycle();
        sample_audio();
        if (mapper) mapper->cpu_cycle();
        clock.cpu_frame_cycle++;
        clock.cpu_master_clock += cpu_step;
        i64 ppu_left = static_cast<i64>(clock.master_clock) - static_cast<i64>(clock.ppu_master_clock);
        done = 0;
        while (ppu_left >= ppu_step) {
            ppu_left -= ppu_step;
            done++;
        }
        ppu.cycle(done);
        cycles_left -= cpu_step;
        if (::dbg.do_break) break;
    }
    return 0;
}


u32 NES::core::step_master(u32 howmany)
{
    cycles_left += howmany;
    i64 cpu_step = clock.timing.cpu_divisor;
    i64 ppu_step = clock.timing.ppu_divisor;
    u32 done = 0;
    while (cycles_left >= cpu_step) {
        //apu.cycle(clock.master_clock);
        clock.master_clock += cpu_step;
        cpu.run_cycle();
        apu.cycle();
        sample_audio();
        if (mapper) mapper->cpu_cycle();
        clock.cpu_frame_cycle++;
        clock.cpu_master_clock += cpu_step;
        i64 ppu_left = static_cast<i64>(clock.master_clock) - static_cast<i64>(clock.ppu_master_clock);
        done = 0;
        while (ppu_left >= ppu_step) {
            ppu_left -= ppu_step;
            done++;
        }
        ppu.cycle(done);
        cycles_left -= cpu_step;
        if (::dbg.do_break) break;
    }
    return 0;
}

/*void NES::core::load_BIOS(multi_file_set* mfs)
{
    printf("\nNES doesn't have a BIOS...?");
}*/
