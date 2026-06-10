//
// Created by . on 12/4/24.
//

#pragma once
//#define GBA_STATS
#include "helpers/sys_interface.h"
#include "helpers/block_cache_better.h"

#include "gba_clock.h"
#include "gba_ppu.h"
#include "gba_dma.h"
#include "gba_apu.h"
#include "gba_controller.h"
#include "cart/gba_cart.h"
#include "gba_timers.h"
#include "component/cpu/arm32/arm32.h"

namespace GBA {
typedef u32 (*rdfunc)(core *, u32 addr, u8 access);
typedef void (*wrfunc)(core *, u32 addr, u8 access, u32 val);
#define GBA_CACHED_INTERPRETER 0
struct core : jsm_system {
    core();

    struct {
        u64 current_transaction{};
        struct {
            u32 sram{};
            u32 ws0_n, ws0_s{};
            u32 ws1_n, ws1_s{};
            u32 ws2_n, ws2_s{};
            u32 empty_bit{};
            u32 phi_term{};
        } io{};
        u32 sram{};
        u32 timing16[2][16]{}; // 0=nonsequential, 1=sequential
        u32 timing32[2][16]{}; // 0=nonsequential, 1=sequential
    } waitstates{};

    CLOCK clock{};
    ARM32::core<ARM32::AT_ARM7TDMI, scheduler_t> cpu;
    CART::core cart;
    PPU::core ppu;
    CONTROLLER controller{};
    APU::core apu;
    scheduler_t scheduler;

    [[nodiscard]] u64 clock_current() const { return clock.master_cycle_count + waitstates.current_transaction; }
    void eval_irqs();
    void process_button_IRQ();

    struct { // Only bits 27-24 are needed to distinguish valid endpoints
        rdfunc read[3][16];
        rdfunc read_debug[3][16];
        rdfunc read_peek[3][16];
        rdfunc read_peek_debug[3][16];
        wrfunc write[3][16];
        wrfunc write_debug[3][16];
    } mem{};

    template<u8 sz, bool do_debug, bool peek>
    static u32 mainbus_read(void *ptr, u32 addr, u8 access) {
        auto *th = static_cast<core *>(ptr);
        u32 v;

        if (addr < 0x10000000) {
            if constexpr(peek) {
                if constexpr (do_debug) v = th->mem.read_peek_debug[sz >> 1][(addr >> 24) & 15](th, addr, access);
                else v = th->mem.read_peek[sz >> 1][(addr >> 24) & 15](th, addr, access);
            }
            else {
                if constexpr (do_debug) v = th->mem.read_debug[sz >> 1][(addr >> 24) & 15](th, addr, access);
                else v = th->mem.read[sz >> 1][(addr >> 24) & 15](th, addr, access);
            }
        }
        else {
            v = busrd_invalid<sz, do_debug, peek>(th, addr, access);
        }
        if constexpr (do_debug) {
            if (::dbg.do_debug) th->trace_read(addr, sz, v);
        }
        return v;
    }

    template<u8 sz> u32 ins_timing(u32 addr, u8 access);

    template<u8 sz, bool do_debug, bool peek>
    static u32 mainbus_fetchins(void *ptr, u32 addr, u8 access) {
        auto *th = static_cast<core *>(ptr);
        u32 v = mainbus_read<sz, do_debug, peek>(th, addr, access);
        if constexpr(!peek) {
            if constexpr (sz == 4) th->io.cpu.open_bus_data = v;
            else th->io.cpu.open_bus_data = (v << 16) | v;
        }
        return v;
    }

    template<u8 sz, bool do_debug>
    static void mainbus_write(void *ptr, u32 addr, u8 access, u32 val) {
        auto *th = static_cast<core *>(ptr);
        if (addr < 0x10000000) {
            //printf("\nWRITE addr:%08x sz:%d val:%08x", addr, sz, val);
            if constexpr (do_debug)  th->mem.write_debug[sz >> 1][(addr >> 24) & 15](th, addr, access, val);
            else  th->mem.write[sz >> 1][(addr >> 24) & 15](th, addr, access, val);
            return;
        }

        buswr_invalid<sz, do_debug>(th, addr, access, val);
    }

    void enable_prefetch();

    block_cache_better<256 * 1024, 4096, 2, 320, ARM32::core<ARM32::AT_ARM7TDMI, scheduler_t>::cached_block_t> EWRAM_cache{};
    block_cache_better<32 * 1024, 4096, 2, 320, ARM32::core<ARM32::AT_ARM7TDMI, scheduler_t>::cached_block_t> IWRAM_cache{};
    std::vector<ARM32::core<ARM32::AT_ARM7TDMI, scheduler_t>::cached_block_t> BIOS_store{};
    std::vector<ARM32::core<ARM32::AT_ARM7TDMI, scheduler_t>::cached_block_t> ROM_store{};


    void set_step_cpu();
    void set_step_dma();
    void set_step_halted();

private:
    void pre_run();
    void post_run();
    void skip_BIOS();
    void trace_read(u32 addr, u8 sz, u32 val) const;
    void trace_write(u32 addr, u8 sz, u32 val) const;

    template<u8 sz, bool do_debug, bool peek> static u32 busrd_invalid(core *th, u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool peek> static u32 busrd_bios(core *th, u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool peek> static u32 busrd_WRAM_slow(core *th, u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool peek> static u32 busrd_WRAM_fast(core *th, u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool peek> static u32 busrd_IO(core *th, u32 addr, u8 access);
    template<u8 sz, bool do_debug> static void buswr_invalid(core *th, u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> static void buswr_bios(core *th, u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> static void buswr_WRAM_slow(core *th, u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> static void buswr_WRAM_fast(core *th, u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> static void buswr_IO(core *th, u32 addr, u8 access, u32 val);
    void schedule_first();
    void schedule_audio_events();
    void reset_audio_schedule(u64 now);
    void reschedule_timers();
    void post_deserialize();

    template<bool do_debug> u32 busrd_IO8(u32 addr, u8 access);
    void set_waitstates();
    void setup_lcd(JSM_DISPLAY &d);
    void setup_audio();

public:
    template<bool do_debug> void buswr_IO8(u32 addr, u8 access, u32 val);
    void check_dma_at_hblank();
    void check_dma_at_vblank();
    template<u8 sz> [[nodiscard]] u32 open_bus(u32 addr) const;

    u8 WRAM_slow[256 * 1024]{};
    u8 WRAM_fast[32 * 1024]{};

    struct {
        bool has{};
        u8 data[16384]{};
    } BIOS{};

    struct {
        struct {
            u32 open_bus_data{};
        } cpu{};
        struct {
            u32 buttons{};
            u32 enable, condition{};
        } button_irq{};
        u32 IE, IF, IME{};
        bool halted{};

        // Unsupported-yet stub stuff

        struct {
            u32 general_purpose_data{};
            u32 control{};
            u32 send{};
            u32 multi[4]{};
        } SIO{};

        u8 POSTFLG{};

        u32 bios_open_bus{};
        u32 dma_open_bus{};
    } io{};

    struct {
        bool described_inputs{false};
        i64 cycles_left{};
        bool fast_boot{true};
    } jsm{};

#ifdef GBA_STATS
    struct {
        u64 arm_cycles{};
        u64 timer0_cycles{};
    } timing{};
#endif

    i32 scanline_cycles_to_execute{};

    DMA dma;


    struct {
        double master_cycles_per_audio_sample, master_cycles_per_min_sample, master_cycles_per_max_sample{};
        double next_sample_cycle_max, next_sample_cycle_min, next_sample_cycle{};
        debug_waveform *waveforms[6]{};
        debug_waveform *main_waveform{};
        audio_output_ring* output_ring{nullptr};
    } audio{};

    TIMER timer[4];

    struct {
        struct GBA_DBG_line {
            u8 window_coverage[240]{}; // 240 4-bit values, bit 1 = on, bit 0 = off
            struct GBA_DBG_line_bg {
                u32 hscroll, vscroll{};
                i32 hpos, vpos{};
                i32 x_lerp, y_lerp{};
                i32 pa, pb, pc, pd{};
                u32 reset_x, reset_y{};
                u32 htiles, vtiles{};
                u32 display_overflow{};
                u32 screen_base_block, character_base_block{};
                u32 priority{};
                 bool bpp8{};
            } bg[4]{};
            u32 bg_mode{};
        } line[160]{};
        struct GBA_DBG_tilemap_line_bg {
            u8 lines[1024 * 128]; // 4, 128*8 1-bit "was rendered or not" values
        } bg_scrolls[4]{};
        struct {
            u32 enable{};
            char str[257]{};
        } mgba{};
    } dbg_info{};

    DBG_START
        DBG_EVENT_VIEW

        DBG_CPU_REG_START1
            *R[16]
        DBG_CPU_REG_END1

        DBG_IMAGE_VIEWS_START
            MDBG_IMAGE_VIEW(window0)
            MDBG_IMAGE_VIEW(window1)
            MDBG_IMAGE_VIEW(window2)
            MDBG_IMAGE_VIEW(window3)
            MDBG_IMAGE_VIEW(bg0)
            MDBG_IMAGE_VIEW(bg1)
            MDBG_IMAGE_VIEW(bg2)
            MDBG_IMAGE_VIEW(bg3)
            MDBG_IMAGE_VIEW(bg0map)
            MDBG_IMAGE_VIEW(bg1map)
            MDBG_IMAGE_VIEW(bg2map)
            MDBG_IMAGE_VIEW(bg3map)
            MDBG_IMAGE_VIEW(sprites)
            MDBG_IMAGE_VIEW(palettes)
            MDBG_IMAGE_VIEW(tiles)
            MDBG_IMAGE_VIEW(sys_info)
        DBG_IMAGE_VIEWS_END
        DBG_WAVEFORM_START1
            DBG_WAVEFORM_MAIN
            DBG_WAVEFORM_CHANS(6)
        DBG_WAVEFORM_END1
        DBG_LOG_VIEW_SIMPLE
    DBG_END

    void option_changed(const char* key, i32 value) override;
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
    void save_state(serialized_state &state) final;
    void load_state(serialized_state &state, deserialize_ret &ret) final;
    void serialize_console(serialized_state &state);
    void serialize_clock(serialized_state &state);
    void serialize_cpu(serialized_state &state);
    void serialize_ppu(serialized_state &state);
    void serialize_apu(serialized_state &state);
    void serialize_cartridge(serialized_state &state);
    void serialize_dma(serialized_state &state);
    void serialize_timers(serialized_state &state);
    void deserialize_console(serialized_state &state);
    void deserialize_clock(serialized_state &state);
    void deserialize_cpu(serialized_state &state);
    void deserialize_ppu(serialized_state &state);
    void deserialize_apu(serialized_state &state);
    void deserialize_cartridge(serialized_state &state);
    void deserialize_dma(serialized_state &state);
    void deserialize_timers(serialized_state &state);
    void setup_debugger_interface(debugger_interface &intf) final;
    void set_audio_ring(audio_output_ring *ring) final;

};

}
