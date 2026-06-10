#include "tmp95c061.h"

namespace TMP95C061 {

static u8 rd8(void *p, u32 a) { return static_cast<u8>(static_cast<core *>(p)->bus_read<false>(1, a)); }
static u16 rd16(void *p, u32 a) { return static_cast<u16>(static_cast<core *>(p)->bus_read<false>(2, a)); }
static u32 rd32(void *p, u32 a) { return static_cast<core *>(p)->bus_read<false>(4, a); }
static void wr8(void *p, u32 a, u8 v) { static_cast<core *>(p)->bus_write<false>(1, a, v); }
static void wr16(void *p, u32 a, u16 v) { static_cast<core *>(p)->bus_write<false>(2, a, v); }
static void wr32(void *p, u32 a, u32 v) { static_cast<core *>(p)->bus_write<false>(4, a, v); }
static u8 pk8(void *p, u32 a) { return static_cast<u8>(static_cast<core *>(p)->bus_read<true>(1, a)); }
static u16 pk16(void *p, u32 a) { return static_cast<u16>(static_cast<core *>(p)->bus_read<true>(2, a)); }
static u32 pk32(void *p, u32 a) { return static_cast<core *>(p)->bus_read<true>(4, a); }

core::core(scheduler_t *scheduler_in, u64 master_clock_freq, u32 divider)
    : scheduler(scheduler_in),
    clocks_per_second(master_clock_freq),
    cpu(scheduler_in, master_clock_freq, divider),
      intc(&cpu),
      timer(scheduler_in, this, &timer_raise),
      adc(scheduler_in, this, &adc_raise),
      rtc(scheduler_in, master_clock_freq),
      wdt(scheduler_in, this, &wd_raise)
{
    cpu.mem_ptr = this;
    cpu.read8 = &rd8; cpu.read16 = &rd16; cpu.read32 = &rd32;
    cpu.write8 = &wr8; cpu.write16 = &wr16; cpu.write32 = &wr32;
    cpu.read8_debug = &rd8; cpu.read16_debug = &rd16; cpu.read32_debug = &rd32;
    cpu.write8_debug = &wr8; cpu.write16_debug = &wr16; cpu.write32_debug = &wr32;
    cpu.read8_peek = &pk8; cpu.read16_peek = &pk16; cpu.read32_peek = &pk32;
}

void core::timer_raise(void *ptr, u32 timer_index)
{
    auto *th = static_cast<core *>(ptr);
    th->intc.raise(static_cast<TMP95C061::irq_source>(TMP95C061::IRQ_INTT0 + timer_index));
}

void core::adc_raise(void *ptr)
{
    static_cast<core *>(ptr)->intc.raise(TMP95C061::IRQ_INTAD);
}

void core::wd_raise(void *ptr)
{
    static_cast<core *>(ptr)->intc.raise(TMP95C061::IRQ_INTWD);
}

void core::reset()
{
    cpu.reset();
    intc.reset();
    timer.reset();
    adc.reset();
    rtc.reset();
    sio.reset();
    cs.reset();
    wdt.reset();
}

void core::schedule_first(u64 start)
{
    cpu.schedule_first(start);
    rtc.schedule_first();
}

void core::setup_tracing(jsm_debug_read_trace *strct, u64 *trace_cycle_ptr, i32 source_id)
{
    cpu.setup_tracing(strct, trace_cycle_ptr, source_id);
}

u8 core::reg_read(u32 addr, bool has_effect)
{
    if (addr == 0x20 || (addr >= 0x22 && addr <= 0x29)) return timer.read(static_cast<u8>(addr));
    if ((addr >= 0x3c && addr <= 0x3f) ||
        (addr >= 0x5c && addr <= 0x5f) ||
        (addr >= 0x68 && addr <= 0x6c)) return cs.read(static_cast<u8>(addr));
    if (addr >= 0x50 && addr <= 0x57) return sio.read(static_cast<u8>(addr));
    if ((addr >= 0x60 && addr <= 0x67) || addr == 0x6d) return adc.read(static_cast<u8>(addr));
    if (addr == 0x6e || addr == 0x6f) return wdt.read(static_cast<u8>(addr));
    if (addr >= 0x70 && addr <= 0x7f) return intc.read_inte(static_cast<u8>(addr));
    if (addr >= 0x90 && addr <= 0x97) return rtc.read(static_cast<u8>(addr));
    return io_read ? io_read(io_ptr, static_cast<u8>(addr), has_effect) : 0;
}

void core::reg_write(u32 addr, u8 val)
{
    if (addr == 0x20 || (addr >= 0x22 && addr <= 0x29)) { timer.write(static_cast<u8>(addr), val); return; }
    if ((addr >= 0x3c && addr <= 0x3f) ||
        (addr >= 0x5c && addr <= 0x5f) ||
        (addr >= 0x68 && addr <= 0x6c)) { cs.write(static_cast<u8>(addr), val); return; }
    if (addr >= 0x50 && addr <= 0x57) {
        sio.write(static_cast<u8>(addr), val);
        if (addr == 0x50) { intc.raise(IRQ_INTTX0); intc.raise(IRQ_INTRX0); }
        else if (addr == 0x54) { intc.raise(IRQ_INTTX1); intc.raise(IRQ_INTRX1); }
        return;
    }
    if (addr == 0x6d) { adc.write(static_cast<u8>(addr), val); return; }
    if (addr == 0x6e || addr == 0x6f) { wdt.write(static_cast<u8>(addr), val); return; }
    if (addr >= 0x70 && addr <= 0x7f) { intc.write_inte(static_cast<u8>(addr), val); return; }
    if (addr >= 0x90 && addr <= 0x97) { rtc.write(static_cast<u8>(addr), val); return; }
    if (io_write) io_write(io_ptr, static_cast<u8>(addr), val);
}

template<bool peek>
u32 core::bus_read(u8 sz, u32 addr)
{
    if (addr < SFR_END) {
        u32 v = reg_read(addr, !peek);
        if (sz >= 2) v |= static_cast<u32>(reg_read(addr + 1, !peek)) << 8;
        if (sz == 4) v |= (static_cast<u32>(reg_read(addr + 2, !peek)) << 16) | (static_cast<u32>(reg_read(addr + 3, !peek)) << 24);
        return v;
    }
    if constexpr (peek) {
        switch (sz) {
            case 1: return ext_peek8 ? ext_peek8(ext_ptr, addr) : 0xFF;
            case 2: return ext_peek16 ? ext_peek16(ext_ptr, addr) : 0xFFFF;
            default:return ext_peek32 ? ext_peek32(ext_ptr, addr) : 0xFFFFFFFF;
        }
    } else {
        switch (sz) {
            case 1: return ext_read8 ? ext_read8(ext_ptr, addr) : 0xFF;
            case 2: return ext_read16 ? ext_read16(ext_ptr, addr) : 0xFFFF;
            default:return ext_read32 ? ext_read32(ext_ptr, addr) : 0xFFFFFFFF;
        }
    }
}

template<bool peek>
void core::bus_write(u8 sz, u32 addr, u32 val)
{
    if (addr < SFR_END) {
        reg_write(addr, static_cast<u8>(val));
        if (sz >= 2) reg_write(addr + 1, static_cast<u8>((val >> 8)));
        if (sz == 4) { reg_write(addr + 2, static_cast<u8>((val >> 16))); reg_write(addr + 3, static_cast<u8>((val >> 24))); }
        return;
    }
    switch (sz) {
        case 1: if (ext_write8) ext_write8(ext_ptr, addr, static_cast<u8>(val)); return;
        case 2: if (ext_write16) ext_write16(ext_ptr, addr, static_cast<u16>(val)); return;
        default:if (ext_write32) ext_write32(ext_ptr, addr, val); return;
    }
}

template u32 core::bus_read<false>(u8, u32);
template u32 core::bus_read<true>(u8, u32);
template void core::bus_write<false>(u8, u32, u32);

}
