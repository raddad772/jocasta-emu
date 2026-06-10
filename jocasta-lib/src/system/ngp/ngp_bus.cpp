#include <cstring>
#include <utility>

#include "ngp_bus.h"
#include "tmp95c061/interrupts.h"

namespace NGP {

void core::load_BIOS(multi_file_set& mfs)
{
    if (mfs.files.empty()) return;
    BUF* b = &mfs.files[0].buf;
    u64 n = b->size < sizeof(BIOS) ? b->size : sizeof(BIOS);
    memcpy(BIOS, b->ptr, n);
}

void core::load_cart(multi_file_set& mfs, physical_io_device& pio)
{
    if (mfs.files.empty()) return;
    BUF* b = &mfs.files[0].buf;
    cart.load_rom(&pio.cartridge_port.SRAM, static_cast<const u8*>(b->ptr), static_cast<u32>(b->size));
}

u8 core::sfr_io_read(void *ptr, u8 addr, bool has_effect)
{
    auto *th = static_cast<core *>(ptr);
    switch (addr) {
        case 0xB0: {
            if (!th->controller_ptr.vec) return 0;
            auto &btn = th->controller_ptr.get().controller.digital_buttons;
            bool up = btn.at(0).state, down = btn.at(1).state;
            bool left = btn.at(2).state, right = btn.at(3).state;
            if (has_effect) {
                auto &c = th->io.controls;
                if (!(up && down)) { c.yHold = false; c.upLatch = up; c.downLatch = down; }
                else if (!c.yHold) { c.yHold = true; std::swap(c.upLatch, c.downLatch); }
                if (!(left && right)){ c.xHold = false; c.leftLatch = left; c.rightLatch = right; }
                else if (!c.xHold) { c.xHold = true; std::swap(c.leftLatch, c.rightLatch); }
            }
            auto &c = th->io.controls;
            return (c.upLatch << 0) | (c.downLatch << 1) | (c.leftLatch << 2) | (c.rightLatch << 3)
                 | (btn.at(4).state << 4) | (btn.at(5).state << 5) | (btn.at(6).state << 6);
        }
        case 0xB1:
            return (th->power_hold_frames > 0 ? 0 : 1) | (1 << 1) | (1 << 2);
        case 0xB2: return th->io.misc.rts_disable ? 1 : 0;
        case 0xB3: return th->io.misc.nmi_enable ? (1 << 2) : 0;
        case 0xB4: return th->io.misc.unknown[0];
        case 0xB5: return th->io.misc.unknown[1];
        case 0xB6: return th->io.misc.unknown[2];
        case 0xB7: return th->io.misc.unknown[3];
        case 0xBC: return th->io.z80.comms_byte;
        default: return 0;
    }
}

void core::sfr_io_write(void *ptr, u8 addr, u8 data)
{
    auto *th = static_cast<core *>(ptr);
    switch (addr) {
        case 0xA0: th->apu.write_right(data); return;
        case 0xA1: th->apu.write_left(data); return;
        case 0xA2: th->apu.write_dac_right(data); return;
        case 0xA3: th->apu.write_dac_left(data); return;
        case 0xB8:
            if (data == 0x55) th->apu.set_psg_enable(true);
            else if (data == 0xAA) th->apu.set_psg_enable(false);
            return;
        case 0xB2: th->io.misc.rts_disable = data & 1; return;
        case 0xB3:
            th->io.misc.nmi_enable = (data >> 2) & 1;
            th->cpu.intc.set_nmi_enable(th->io.misc.nmi_enable);
            return;
        case 0xB4: th->io.misc.unknown[0] = data; return;
        case 0xB5: th->io.misc.unknown[1] = data; return;
        case 0xB6: th->io.misc.unknown[2] = data; return;
        case 0xB7: th->io.misc.unknown[3] = data; return;
        case 0xB9:
            if (data == 0x55) th->z80_reset_line(false);
            else if (data == 0xAA) th->z80_reset_line(true);
            return;
        case 0xBA:
                   th->z80.request_NMI(); return;
        case 0xBC:
                   th->io.z80.comms_byte = data; return;
        default: return;
    }
}

u32 read_trace_z80(void *ptr, u32 addr) {
    auto *th = static_cast<core *>(ptr);
    return th->z80_bus_read<false, true>(addr, th->z80.pins.D);
}

u32 read_trace_tlcs900h(void *ptr, u32 addr, u8 sz) {
    auto *th = static_cast<core *>(ptr);
    return th->mainbus_read<false, true>(addr, sz, 0);
}

template<bool do_debug, bool peek>
u32 core::mainbus_read(u32 addr, u8 sz, u32 old)
{
    auto rd8 = [this](u32 a) -> u8 {
        if (a >= 0x004000 && a <= 0x006FFF) return work_ram[a - 0x004000];
        if (a >= 0x007000 && a <= 0x007FFF) {
            if constexpr (!peek) next_z80_memaccess = master_clock + Z80_DIV;
            return work_ram[a - 0x004000];
        }
        if (a >= 0x008000 && a <= 0x00BFFF) return kxge.read(a);
        if (a >= 0xFF0000) return BIOS[a & 0xFFFF];
        int chip = cpu.cs_select(a);
        if (chip == 0 || chip == 1) return cart.read(static_cast<u32>(chip), a);
        return 0xFF;
    };
    u32 v = 0;
    for (u8 i = 0; i < sz; i++) v |= static_cast<u32>(rd8(addr + i)) << (i * 8);
    return v;
}

template<bool do_debug>
u32 core::mainbus_write(u32 addr, u8 sz, u32 val)
{
    auto wr8 = [this](u32 a, u8 d) {
        if (a >= 0x004000 && a <= 0x006FFF) { work_ram[a - 0x004000] = d; return; }
        if (a >= 0x007000 && a <= 0x007FFF) {
            next_z80_memaccess = master_clock + Z80_DIV;
            work_ram[a - 0x004000] = d; return;
        }
        if (a >= 0x008000 && a <= 0x00BFFF) { kxge.write(a, d); return; }
        if (a >= 0xFF0000) { return; }
        int chip = cpu.cs_select(a);
        if (chip == 0 || chip == 1) cart.write(static_cast<u32>(chip), a, d);
    };
    for (u8 i = 0; i < sz; i++) wr8(addr + i, static_cast<u8>((val >> (i * 8))));
    return val;
}

static void timer_ff_out(void *ptr, u32 pair, bool level)
{
    if (pair == 1 && level) { auto*th=static_cast<core *>(ptr); th->z80.notify_IRQ(true); }
}

static u8 ext_r8 (void *p, u32 a) { return static_cast<u8>(static_cast<core *>(p)->mainbus_read<false, false>(a, 1, 0)); }
static u16 ext_r16(void *p, u32 a) { return static_cast<u16>(static_cast<core *>(p)->mainbus_read<false, false>(a, 2, 0)); }
static u32 ext_r32(void *p, u32 a) { return static_cast<core *>(p)->mainbus_read<false, false>(a, 4, 0); }
static void ext_w8 (void *p, u32 a, u8 v) { static_cast<core *>(p)->mainbus_write<false>(a, 1, v); }
static void ext_w16(void *p, u32 a, u16 v) { static_cast<core *>(p)->mainbus_write<false>(a, 2, v); }
static void ext_w32(void *p, u32 a, u32 v) { static_cast<core *>(p)->mainbus_write<false>(a, 4, v); }
static u8 ext_pk8 (void *p, u32 a) { return static_cast<u8>(static_cast<core *>(p)->mainbus_read<false, true>(a, 1, 0)); }
static u16 ext_pk16(void *p, u32 a) { return static_cast<u16>(static_cast<core *>(p)->mainbus_read<false, true>(a, 2, 0)); }
static u32 ext_pk32(void *p, u32 a) { return static_cast<core *>(p)->mainbus_read<false, true>(a, 4, 0); }

core::core(jsm::systems kind) :
    kxge{kind == jsm::NEOGEO_POCKET_COLOR ? KXGE::K2GE : KXGE::K1GE}
{
    has.set_audio_ring = true;
    has.load_BIOS = true;
    has.save_state = false;

    is_color = kind == jsm::systems::NEOGEO_POCKET_COLOR;

    if (is_color)
        snprintf(label, sizeof(label), "Neo Geo Pocket Color");
    else
        snprintf(label, sizeof(label), "Neo Geo Pocket");

    jsm_debug_read_trace dt;
    dt.read_trace = &read_trace_z80;
    dt.read_trace_arm = &read_trace_tlcs900h;
    dt.ptr = static_cast<void *>(this);

    z80.setup_tracing(&dt, &master_clock);
    cpu.setup_tracing(&dt, &master_clock, 2);

    cpu.io_ptr = this;
    cpu.io_read = &sfr_io_read;
    cpu.io_write = &sfr_io_write;

    cpu.ext_ptr = this;
    cpu.ext_read8 = &ext_r8; cpu.ext_read16 = &ext_r16; cpu.ext_read32 = &ext_r32;
    cpu.ext_write8 = &ext_w8; cpu.ext_write16 = &ext_w16; cpu.ext_write32 = &ext_w32;
    cpu.ext_peek8 = &ext_pk8; cpu.ext_peek16 = &ext_pk16; cpu.ext_peek32 = &ext_pk32;

    kxge.scheduler = &scheduler;
    kxge.cpu = &cpu;

    cpu.timer.ff_out_ptr = this;
    cpu.timer.ff_out = &timer_ff_out;
}

template<bool do_debug, bool peek>
u8 core::z80_bus_read(u16 addr, u8 old)
{
    if (addr <= 0x0FFF) return work_ram[SOUND_RAM_OFF + (addr & 0x0FFF)];
    if (addr == 0x8000) return io.z80.comms_byte;
    return 0x00;
}

template<bool do_debug>
void core::z80_bus_write(u16 addr, u8 val)
{
    if (addr <= 0x0FFF) { work_ram[SOUND_RAM_OFF + (addr & 0x0FFF)] = val; return; }
    switch (addr) {
        case 0x4000: apu.write_right(val); return;
        case 0x4001: apu.write_left(val); return;
        case 0x8000:
                     io.z80.comms_byte = val; return;
        case 0xC000: cpu.intc.set_line(TMP95C061::IRQ_INT5, 1); return;
        default: return;
    }
}

template<bool do_debug, bool peek>
u8 core::z80_IO_read(u16 addr, u8 old)
{
    return 0x00;
}

template<bool do_debug>
void core::z80_IO_write(u16 addr, u8 val)
{
    if ((addr & 0xFF) == 0xFF) {
        z80.notify_IRQ(false);
        cpu.intc.set_line(TMP95C061::IRQ_INT5, 0);
    }
}

template u32 core::mainbus_read<false, false>(u32, u8, u32);
template u32 core::mainbus_read<true, false>(u32, u8, u32);
template u32 core::mainbus_read<false, true >(u32, u8, u32);
template u32 core::mainbus_read<true, true >(u32, u8, u32);
template u32 core::mainbus_write<false>(u32, u8, u32);
template u32 core::mainbus_write<true >(u32, u8, u32);
template u8 core::z80_bus_read<false, false>(u16, u8);
template u8 core::z80_bus_read<true, false>(u16, u8);
template u8 core::z80_bus_read<false, true >(u16, u8);
template u8 core::z80_bus_read<true, true >(u16, u8);
template void core::z80_bus_write<false>(u16, u8);
template void core::z80_bus_write<true >(u16, u8);
template u8 core::z80_IO_read<false, false>(u16, u8);
template u8 core::z80_IO_read<true, false>(u16, u8);
template u8 core::z80_IO_read<false, true >(u16, u8);
template u8 core::z80_IO_read<true, true >(u16, u8);
template void core::z80_IO_write<false>(u16, u8);
template void core::z80_IO_write<true >(u16, u8);
}
