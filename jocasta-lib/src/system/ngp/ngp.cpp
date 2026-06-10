#include "helpers/physical_io.h"
#include "ngp.h"
#include "ngp_bus.h"

namespace NGP {

void core::setup_lcd(JSM_DISPLAY *d)
{
    d->kind = jsm::LCD;
    d->enabled = true;

    d->fps = static_cast<double>(SYS_CLKSPD) / static_cast<double>(KXGE::FRAME_CYCLES);

    d->pixelometry.cols.left_hblank = 0;
    d->pixelometry.cols.visible = KXGE::DISP_WIDTH;
    d->pixelometry.cols.max_visible = KXGE::DISP_WIDTH;
    d->pixelometry.cols.right_hblank = 0;
    d->pixelometry.offset.x = 0;

    d->pixelometry.rows.top_vblank = 0;
    d->pixelometry.rows.visible = KXGE::DISP_HEIGHT;
    d->pixelometry.rows.max_visible = KXGE::DISP_HEIGHT;
    d->pixelometry.rows.bottom_vblank = KXGE::DISP_TOTAL_HEIGHT - KXGE::DISP_HEIGHT;
    d->pixelometry.offset.y = 0;

    d->geometry.physical_aspect_ratio.width = KXGE::DISP_HEIGHT;
    d->geometry.physical_aspect_ratio.height = KXGE::DISP_HEIGHT;

    d->pixelometry.overscan.left = d->pixelometry.overscan.right = 0;
    d->pixelometry.overscan.top = d->pixelometry.overscan.bottom = 0;
}

template<bool do_debug>
void core::cycle_z80(u64 sched)
{
    if (io.z80.reset_line) {
        io.z80.reset_line_count++;
        if (io.z80.reset_line_count >= 3) return;
    }

    io.z80.reset_line_count = 0;

    if (sched < next_z80_memaccess && z80.pins.MRQ) return;
    z80.cycle<do_debug>();

    if (z80.pins.RD) {
        if (z80.pins.MRQ) {
            z80.pins.D = z80_bus_read<do_debug, false>(z80.pins.Addr, z80.pins.D);
            next_z80_memaccess = sched + Z80_DIV;
        }
        else if (z80.pins.IO && (z80.pins.M1 == 0)) {
            z80.pins.D = z80_IO_read<do_debug, false>(z80.pins.Addr, z80.pins.D);
        }
    }
    else if (z80.pins.WR) {
        if (z80.pins.MRQ) {
            z80_bus_write<do_debug>(z80.pins.Addr, z80.pins.D);
            next_z80_memaccess = sched + Z80_DIV;
        }
        else if (z80.pins.IO) {
            z80_IO_write<do_debug>(z80.pins.Addr, z80.pins.D);
        }
    }
}

template<bool do_debug>
static void do_run_z80(void *ptr, u64 key, u64 clock, u32 jitter)
{
    auto *th = static_cast<core *>(ptr);
    const u64 sched = clock - jitter;
    th->cycle_z80<do_debug>(sched);
    th->scheduler.only_add_abs(static_cast<i64>((sched + Z80_DIV)), 0, th, &do_run_z80<false>, &do_run_z80<true>, nullptr);
}

void core::schedule_first(u64 from) {
    apu.schedule_first();
    cpu.schedule_first(from);
    kxge.schedule_first();
    scheduler.only_add_abs(static_cast<i64>((from + Z80_DIV)), 0, this, &do_run_z80<false>, &do_run_z80<true>, nullptr);
}

void core::play() {}
void core::pause() {}
void core::stop() {}
void core::killall() {}
void core::enable_tracing() {}
void core::disable_tracing() {}

void core::get_framevars(framevars& out)
{
    out.master_frame = kxge.frame_num;
    out.x = 0;
    out.scanline = kxge.cur_line;
    out.master_cycle = master_clock;
}

u32 core::finish_scanline()
{
    if (::dbg.do_debug) scheduler.run_til_tag_tg16<true>(1);
    else scheduler.run_til_tag_tg16<false>(1);
    return 0;
}

u32 core::step_master(u32 howmany)
{
    scheduler.run_for_cycles_tg16<true>(howmany);
    return 0;
}

void core::audio_rings_ready()
{
    for (auto &pio : IOs) {
        if (pio.kind != HID_AUDIO_CHANNEL) continue;
        apu.set_ring(pio.audio_channel.ring);
        break;
    }
}

void core::set_audio_ring(audio_output_ring *ring)
{
    apu.set_ring(ring);
}

void core::setup_audio()
{
    physical_io_device *pio = &IOs.emplace_back();
    pio->init(HID_AUDIO_CHANNEL, true, true, false, true);
    JSM_AUDIO_CHANNEL *chan = &pio->audio_channel;
    chan->sample_rate = apu.source_sample_rate();
    chan->low_pass_filter = 16000;
}

static void NGPIO_load_cart(jsm_system *sm, multi_file_set &mfs, physical_io_device &pio)
{
    auto *th = dynamic_cast<core *>(sm);
    th->load_cart(mfs, pio);
    th->reset();
}

void core::z80_reset_line(bool enabled)
{
    if ((!io.z80.reset_line) && enabled) {
        io.z80.reset_line_count = 0;
    }
    if ((io.z80.reset_line) && (!enabled) && (io.z80.reset_line_count >= 3)) {
        z80.reset();
    }
    io.z80.reset_line = enabled;
}

void core::reset() {
    cpu.reset();
    io.z80.reset_line = true;
    io.z80.reset_line_count = 3;
    apu.reset();
    kxge.reset();
    cart.reset();

    power_pressed = false;
    power_press_hold = 0;
    power_nmi_done = false;
    boot_pending = true;

    scheduler.clear();
    master_clock = 0;
    schedule_first(0);
}

void core::poll_power_button()
{
    const bool bios_halt = cpu.cpu.halted && cpu.cpu.regs.PC >= 0xFF0000;

    if (bios_halt) power_hold_frames = 40;
    else if (power_hold_frames > 0) power_hold_frames--;

    if (!power_nmi_done && bios_halt) power_nmi_done = true;
    cpu.intc.set_line(TMP95C061::IRQ_NMI, power_nmi_done ? 0 : 1);
}

static void NGPIO_unload_cart(jsm_system *sm)
{
}

void core::describe_io()
{
    if (described_inputs) return;
    described_inputs = true;
    IOs.reserve(8);

    physical_io_device *d = &IOs.emplace_back();
    d->init(HID_CONTROLLER, true, true, true, false);
    snprintf(d->controller.name, sizeof(d->controller.name), "%s", "NeoGeo Pocket");
    d->id = 0;
    d->kind = HID_CONTROLLER;
    d->connected = true;
    d->enabled = true;
    JSM_CONTROLLER *cnt = &d->controller;
    controller_ptr.make(IOs, IOs.size() - 1);
    pio_new_button(cnt, "up", DBCID_co_up);
    pio_new_button(cnt, "down", DBCID_co_down);
    pio_new_button(cnt, "left", DBCID_co_left);
    pio_new_button(cnt, "right", DBCID_co_right);
    pio_new_button(cnt, "a", DBCID_co_fire1);
    pio_new_button(cnt, "b", DBCID_co_fire2);
    pio_new_button(cnt, "option", DBCID_co_start);

    physical_io_device *chassis = &IOs.emplace_back();
    chassis->init(HID_CHASSIS, true, true, true, true);
    HID_digital_button *b = &chassis->chassis.digital_buttons.emplace_back();
    snprintf(b->name, sizeof(b->name), "Power");
    b->state = 1;
    b->common_id = DBCID_ch_power;

    d = &IOs.emplace_back();
    d->init(HID_CART_PORT, true, true, true, true);
    d->cartridge_port.load_cart = &NGPIO_load_cart;
    d->cartridge_port.unload_cart = &NGPIO_unload_cart;

    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, 1, 1, 0, 1);
    setup_lcd(&d->display);
    u32 numpx = KXGE::DISP_HEIGHT * KXGE::DISP_WIDTH;
    d->display.allocate_output(0, numpx * 2);
    d->display.allocate_output(1, numpx * 2);
    d->display.output_debug_metadata[0] = malloc(numpx * 2);
    d->display.output_debug_metadata[1] = malloc(numpx * 2);
    kxge.display_ptr.make(IOs, IOs.size() - 1);
    kxge.cur_output = static_cast<u16 *>(d->display.output[0]);
    d->display.active_draw_buffer = 0;

    d = &IOs.emplace_back();
    d->init(HID_NVRAM, 1, 1, 0, 0);
    snprintf(d->nvram.label, sizeof(d->nvram.label), "work ram");
    {
        persistent_store &ps = d->nvram.store;
        ps.requested_size = WORK_RAM_SIZE;
        ps.fill_value = 0x00;
        ps.persistent = true;
        ps.ready_to_use = false;
        snprintf(ps.ext, sizeof(ps.ext), "ram");
    }
    nvram_ptr.make(IOs, IOs.size() - 1);

    d = &IOs.emplace_back();
    d->init(HID_RTC, 1, 1, 0, 0);
    d->rtc.present = true;
    d->rtc.has_date = true;
    snprintf(d->rtc.label, sizeof(d->rtc.label), "TMP95C061 RTC");
    rtc_ptr.make(IOs, IOs.size() - 1);

    setup_audio();
}

namespace {
    inline u8 to_bcd(int v) { return static_cast<u8>((((v / 10) << 4) | (v % 10))); }
    inline int from_bcd(u8 v) { return ((v >> 4) & 0xF) * 10 + (v & 0xF); }
}

void core::sync_rtc_pio()
{
    if (!rtc_ptr.vec) return;
    JSM_RTC &p = rtc_ptr.get().rtc;
    auto &rtc = cpu.rtc;

    if (p.reseed) {
        const rtc_datetime &s = (p.source == RTC_SOURCE_USER) ? p.user : p.wall;
        if (p.source == RTC_SOURCE_USER || p.wall_valid) {
            rtc.year = to_bcd(s.year % 100);
            rtc.month = to_bcd(s.month);
            rtc.day = to_bcd(s.day);
            rtc.hour = to_bcd(s.hour);
            rtc.minute = to_bcd(s.minute);
            rtc.second = to_bcd(s.second);
            rtc.weekday = s.weekday & 7;
            rtc.enable = 1;
        }
        p.reseed = false;
    }

    p.game.year = static_cast<u16>((2000 + from_bcd(rtc.year)));
    p.game.month = static_cast<u8>(from_bcd(rtc.month));
    p.game.day = static_cast<u8>(from_bcd(rtc.day));
    p.game.hour = static_cast<u8>(from_bcd(rtc.hour));
    p.game.minute = static_cast<u8>(from_bcd(rtc.minute));
    p.game.second = static_cast<u8>(from_bcd(rtc.second));
    p.game.weekday = rtc.weekday & 7;
}

void core::ensure_work_ram_bound()
{
    if (!nvram_ptr.vec) return;
    persistent_store &ps = nvram_ptr.get().nvram.store;
    if (!ps.ready_to_use || !ps.data) return;
    if (work_ram != static_cast<u8 *>(ps.data))
        work_ram = static_cast<u8 *>(ps.data);

    if (boot_pending) {
        boot_pending = false;
        if (work_ram[0x2c7a] != 0) {
            cpu.cpu.regs.PC = static_cast<u32>(BIOS[0xFE00]) | (static_cast<u32>(BIOS[0xFE01]) << 8) | (static_cast<u32>(BIOS[0xFE02]) << 16);
            cpu.cpu.regs.R[TLCS900H::XSP].dw = 0x6c00;
            cpu.cpu.PIQ_size = 0;
            cpu.cpu.PIC = 0;
        }
    }
    ps.dirty = true;
}

}

jsm_system *ngp_new(jsm::systems variant) {
    return new NGP::core(variant);
}

void ngp_delete(jsm_system *sys) {
    auto *th = dynamic_cast<NGP::core *>(sys);
    for (auto &pio : th->IOs) {
        if (pio.kind == HID_CART_PORT) {
            if (pio.cartridge_port.unload_cart) pio.cartridge_port.unload_cart(sys);
        }
    }
}
