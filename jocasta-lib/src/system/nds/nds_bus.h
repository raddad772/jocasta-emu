//
// Created by . on 12/4/24.
//

//#define TRACE
#pragma once

#include "helpers/sys_interface.h"
#include "helpers/scheduler.h"
#include "helpers/block_cache_better.h"

#include "nds_clock.h"
#include "nds_ppu.h"
#include "nds_controller.h"
#include "cart/nds_cart.h"
#include "nds_ipc.h"
#include "nds_dma.h"
#include "nds_timers.h"
#include "system/nds/3d/nds_ge.h"
#include "system/nds/3d/nds_re.h"
#include "nds_apu.h"
#include "nds_spi.h"

#include "component/cpu/arm32/arm32.h"
#include "nds_serialize.h"

namespace NDS {
struct core;
typedef u32 (core::*rdfunc)(u32 addr, u8 access);
typedef void (core::*wrfunc)(u32 addr, u8 access, u32 val);

#define NDSVRAMSHIFT(nda) (((nda) & 0xFFFFFF) >> 14)
#define NDSVRAMMASK 0x3FF

union reg64 {
    u8 data[8];
    u32 data32[2];
    u64 u{};
};

union reg32 {
    u8 data[4];
    u32 u{};
};
typedef ARM32::core<ARM32::AT_ARM7TDMI, scheduler_t>::cached_block_t arm7_cached_block_t;
typedef ARM32::core<ARM32::AT_ARM946ES, scheduler_t>::cached_block_t arm9_cached_block_t;

struct core : jsm_system {
    core();
    struct {
        u64 current_transaction{};
        u64 current_shift{}; // 1
    } waitstates{};
    CLOCK clock;
    scheduler_t scheduler;

    ARM32::core<ARM32::AT_ARM7TDMI, scheduler_t> arm7;
    ARM32::core<ARM32::AT_ARM946ES, scheduler_t> arm9;
    PPU::core ppu;
    GFX::GE ge;
    GFX::RE re;
    APU::core apu;

    template<u8 sz, bool do_debug, bool peek>
    static u32 mainbus_read7(void *ptr, u32 addr, u8 access) {
        static constexpr u32 maskalign[5] = {0, 0xFFFFFFFF, 0xFFFFFFFE, 0, 0xFFFFFFFC};
        addr &= maskalign[sz];
        auto *th = static_cast<core *>(ptr);
        if constexpr(!peek) th->waitstates.current_transaction++;
        u32 v;

        if (addr < 0x10000000) {
            if constexpr(do_debug) v = (th->*(th->mem.rw[0].read_debug[sz >> 1][(addr >> 24) & 15]))(addr, access);
            else v = (th->*(th->mem.rw[0].read[sz >> 1][(addr >> 24) & 15]))(addr, access);
        }
        // TODO: update these rw's with more...
        else v = th->busrd7_invalid<sz, do_debug>(addr, access);
        return v;
    }

    template<u8 sz, bool do_debug, bool peek>
    static u32 mainbus_read9(void *ptr, u32 addr, u8 access) {
        auto *th = static_cast<core *>(ptr);
        if constexpr (!peek) th->waitstates.current_transaction++;
        u32 v;

        if (addr < 0x1000'0000) {
            if constexpr (do_debug) v = (th->*(th->mem.rw[1].read_debug[sz >> 1][(addr >> 24) & 15]))(addr, access);
            else v = (th->*(th->mem.rw[1].read[sz >> 1][(addr >> 24) & 15]))(addr, access);
        }
        else if ((addr & 0xFFFF0000) == 0xFFFF0000) {
            addr &= 0xFFF;
            if constexpr(sz == 1) return reinterpret_cast<u8 *>(th->mem.bios9)[addr];
            if constexpr(sz == 2) return reinterpret_cast<u16 *>(th->mem.bios9)[addr >> 1];
            if constexpr(sz == 4) return reinterpret_cast<u32 *>(th->mem.bios9)[addr >> 2];
            NOGOHERE;
        }
        else v = th->busrd9_invalid<sz, do_debug>(addr, access);
        return v;
    }

    template<u8 sz, bool do_debug, bool peek>
    static u32 mainbus_fetchins9(void *ptr, u32 addr, u8 access) {
        auto *th = static_cast<core *>(ptr);
        u32 v = th->mainbus_read9<sz, do_debug, peek>(ptr, addr, access);
        if constexpr(sz == 4)
            th->io.open_bus.arm9 = v;
        else if constexpr(sz == 2)
            th->io.open_bus.arm9 = (v << 16) | v;
        return v;
    }

    template<u8 sz, bool do_debug>
    static void mainbus_write9(void *ptr, u32 addr, u8 access, u32 val) {
        auto *th = static_cast<core *>(ptr);
        th->waitstates.current_transaction++;

        if (addr < 0x10000000) {
            if constexpr(do_debug) return (th->*(th->mem.rw[1].write_debug[sz >> 1][(addr >> 24) & 15]))(addr, access, val);
            else return (th->*(th->mem.rw[1].write[sz >> 1][(addr >> 24) & 15]))(addr, access, val);
        }

        th->buswr9_invalid<sz, do_debug>(addr, access, val);
    }

    template<u8 sz, bool do_debug, bool peek>
    static u32 mainbus_fetchins7(void *ptr, u32 addr, u8 access) {
        auto *th = static_cast<core *>(ptr);
        u32 v = mainbus_read7<sz, do_debug, peek>(ptr, addr, access);
        if constexpr(sz == 4) th->io.open_bus.arm7 = v;
        else th->io.open_bus.arm7 = (v << 16) | v;
        return v;
    }

    template<u8 sz, bool do_debug>
    static void mainbus_write7(void *ptr, u32 addr, u8 access, u32 val) {
        static constexpr u32 maskalign[5] = {0, 0xFFFFFFFF, 0xFFFFFFFE, 0, 0xFFFFFFFC};
        addr &= maskalign[sz];
        auto *th = static_cast<core *>(ptr);
        th->waitstates.current_transaction++;
        if (addr < 0x10000000) {
            if constexpr(do_debug) return (th->*(th->mem.rw[0].write_debug[sz >> 1][(addr >> 24) & 15]))(addr, access, val);
            else return (th->*(th->mem.rw[0].write[sz >> 1][(addr >> 24) & 15]))(addr, access, val);
        }

        th->buswr7_invalid<sz, do_debug>(addr, access, val);
    }

    template<bool do_debug> static void run_block(void *ptr, u64 num_cycles, u64 clock, u32 jitter);
    static void do_next_scheduled_frame(void *bound_ptr, u64 key, u64 current_clock, u32 jitter);
    void schedule_frame(u64 start_clock, bool is_first);

    void sample_audio();

    void populate_opts();
    void update_debug_cam_matrix();
    void read_opts();
    void skip_BIOS();
    void setup_lcd(JSM_DISPLAY &d);
    void setup_audio();

    template<u8 sz, bool do_debug> u32 busrd7_invalid(u32 addr, u8 access);
    template<u8 sz, bool do_debug> u32 busrd9_invalid(u32 addr, u8 access);
    template<u8 sz, bool do_debug> void buswr7_invalid(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> void buswr9_invalid(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug, bool cached> void buswr7_shared(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd7_shared(u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool cached> void buswr7_vram(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd7_vram(u32 addr, u8 access);
    template<u8 sz, bool do_debug> void buswr7_gba_cart(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd7_gba_cart(u32 addr, u8 access);
    template<u8 sz, bool do_debug> void buswr7_gba_sram(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd7_gba_sram(u32 addr, u8 access);
    template<u8 sz, bool do_debug> u32 busrd9_main(u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool cached> void buswr9_main(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> void buswr9_gba_cart(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd9_gba_cart(u32 addr, u8 access);
    template<u8 sz, bool do_debug> void buswr9_gba_sram(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd9_gba_sram(u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool cached> void buswr9_shared(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd9_shared(u32 addr, u8 access);
    template<u8 sz, bool do_debug> void buswr9_obj_and_palette(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd9_obj_and_palette(u32 addr, u8 access);
    template<u8 sz, bool do_debug, bool cached> void buswr9_vram(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd9_vram(u32 addr, u8 access);
    template<u8 sz, bool do_debug> void buswr9_oam(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd9_oam(u32 addr, u8 access);
    template<u8 sz, bool do_debug> void buswr7_bios7(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd7_bios7(u32 addr, u8 access);
    template<u8 sz, bool do_debug> void buswr7_main(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> u32 busrd7_main(u32 addr, u8 access);

    template<bool do_debug> u32 busrd7_io8(u32 addr, u8 access);
    template<bool do_debug> u32 busrd9_io8(u32 addr, u8 access);
    template<u8 sz, bool do_debug> u32 busrd9_io(u32 addr, u8 access);
    template<u8 sz, bool do_debug> u32 busrd7_wifi(u32 addr, u8 access);
    template<u8 sz, bool do_debug> u32 busrd7_io(u32 addr, u8 access);
    template <u8 sz, bool do_debug> void buswr9_io(u32 addr, u8 access, u32 val);
    template<u8 sz, bool do_debug> void buswr7_wifi(u32 addr, u8 access, u32 val);
    void start_div();
    void start_sqrt();
    void div_calc();
    void sqrt_calc();

    template <bool do_debug> void buswr7_io8(u32 addr, u8 access, u32 val);
    template <bool do_debug> void buswr9_io8(u32 addr, u8 access, u32 val);
    template <u8 sz, bool do_debug> void buswr7_io(u32 addr, u8 access, u32 val);


    bool arm9_ins{}, arm7_ins{};
    CONTROLLER controller{this};
    [[nodiscard]] u32 RTC_days_in_month() const;
    template<bool do_debug> void RTC_set_irq(u32 which);
    void RTC_clear_irq(u32 flag);
    template<bool do_debug> void RTC_process_irqs(u32 kind);
    void RTC_reset();
    void RTC_check_end_of_month();
    void RTC_cmd_read();
    void RTC_reset_state();
    void RTC_write_dt(u32 num, u32 val);
    void RTC_save_dt();
    template<bool do_debug> void RTC_cmd_write(u32 val);
    template<bool do_debug> void RTC_byte_in();
    template<bool do_debug> void write_RTC(u8 sz, u32 val);
    void RTC_day_inc();
    void map_memory();
    template<bool do_cached_arm7, bool do_cached_arm9> void remap_cached_write_funcs_t();
    void remap_cached_write_funcs(bool c7, bool c9);
    void arm7_enter_cached_mode();
    void arm7_exit_cached_mode();
    void arm9_enter_cached_mode();
    void arm9_exit_cached_mode();
    void option_changed(const char* key, i32 value) override;
    template<bool do_debug> static void RTC_tick(void *ptr, u64 key, u64 clock, u32 jitter);

    void SPI_apply_calib_data(tsc_cd &data);
    void SPI_read_and_apply_touchscreen_calibration();
    void SPI_reset();
    void SPI_pwm_transaction(u32 val);
    void SPI_firmware_transaction(u32 val);
    void SPI_touchscreen_transaction(u32 val);
    void SPI_release_hold();
    template<bool do_debug> static void SPI_irq(void *ptr, u64 num_cycles, u64 clock, u32 jitter);
    template<bool do_debug> void SPI_transaction(u32 val);
    u32 SPI_read(u8 sz);
    template<bool do_debug> void SPI_write(u8 sz, u32 val);

    static void hblank(void *ptr, u64 key, u64 clock, u32 jitter);

    [[nodiscard]] u32 VRAM_tex_read(u32 addr, u8 sz) const;
    [[nodiscard]] u32 VRAM_pal_read(u32 addr, u8 sz) const;
    void VRAM_set_bank(u32 bank_num, u32 mst, u32 ofs, u8 *ptr, bool force, bool update_io);
    void VRAM_resetup_banks();

    struct {
        struct { // Only bits 27-24 are needed to distinguish valid endpoints{}, mostly.
            rdfunc read[3][16]{};
            wrfunc write[3][16]{};
            rdfunc read_debug[3][16]{};
            wrfunc write_debug[3][16]{};
        } rw[2]{}; // ARM7{}, ARM9 maps...
        u8 RAM[0x40'0000]{}; // 4MB RAM
        block_cache_better<0x40'0000, 256, 2, 80*4, arm7_cached_block_t, 2, arm9_cached_block_t> RAM_block_cache{};

        u8 WRAM_share[32 * 1024]{};      // 32KB WRAM mappable
        block_cache_better<32 * 1024, 256, 2, 80*4, arm7_cached_block_t, 2, arm9_cached_block_t> WRAM_share_block_cache{};

        u8 WRAM_arm7[64 * 1024]{}; // 64KB of WRAM for ARM7 only
        block_cache_better<64 * 1024, 256, 2, 80*4, arm7_cached_block_t, 2, arm9_cached_block_t> WRAM_arm7_block_cache{};

        u8 oam[2048]{};
        u8 palette[2048]{};
        u8 wifi[8 * 1024]{};
        u8 bios7[16384]{};
        u8 bios9[4096]{};
        block_cache_better<16384, 16384, 2, 80*4, arm7_cached_block_t> bios7_block_cache{};
        block_cache_better<4096, 4096, 2, 80*4, arm9_cached_block_t> bios9_block_cache{};

        u8 firmware[256 * 1024]{};
        struct {
            struct {
                u32 base{}, mask{}, disabled{}, val{};
            } RAM7{}, RAM9{};
        } io{};

        struct NDS_VRAM {
            u8 data[656 * 1024]{};
            struct {
                struct {
                    u32 mst{}, ofs{};
                    bool enable{};
                } bank[9]{};
            } io{};
            struct {
                u8 *arm9[0x400]{}; // 16KB banks from 06000000 to 06FFFFFF
                u8 *arm7[2]{};    // 2 128KB banks at 06000000. addr < 06400000{}, slot[0] or [1] & 128KB
            } map{};
            block_cache_better<1024  * 1024, 1024, 2, 80*4, arm7_cached_block_t, 2, arm9_cached_block_t> block_cache{};
        } vram{};

    } mem{};

    struct {
        struct {
            u8 filldata[16]{};
        } dma{};

        u8 POSTFLG{};

        struct {
            u32 arm7{}, arm9{}, bios{}, dma{};
        } open_bus{};

        struct {
            IPC_FIFO to_arm7{}, to_arm9{};

            struct {
                u32 irq_on_send_fifo_empty{};
                u32 irq_on_recv_fifo_not_empty{};
                u32 error{};
                u32 fifo_enable{};
            } arm7{}, arm9{};
            struct {
                u32 dinput{};
                u32 doutput{};
                u32 enable_irq_from_remote{};

            } arm7sync{}, arm9sync{};
        } ipc{};


        struct {
            u32 BIOSPROT{};
            u32 EXMEM{};
            u32 IF{}, IE{}, IME{}, IE_val{};
            struct {
                u32 buttons{};
                u32 enable{}, condition{};
            } button_irq{};
            u32 POSTFLG{};
        } arm7{};

        struct {
            u32 EXMEM{};
            u32 IF{}, IE{}, IME{};
            struct {
                u32 buttons{};
                u32 enable{}, condition{};
            } button_irq{};
            u32 POSTFLG{};
        } arm9{};

        struct {
            u32 gba_slot{}; // 0=ARM9{}, 1=ARM7
            u32 nds_slot_is7{}; // 0=ARM9{}, 1=ARM7
            u32 main_memory{};
        } rights{};

        struct {
            u64 busy_until{};
            u32 mode{};
            u32 by_zero{};
            bool needs_calc{};

            reg64 numer{}, denom{}, result{}, remainder{};
            // numer & demom are r/w{}, result and remainder are R-only
        } div{};

        struct {
            u64 busy_until{};
            u32 mode{};
            bool needs_calc{};

            reg32 result{};
            reg64 param{}; // r/w
        } sqrt{};

        struct {
            u64 timestamp{};
            u16 data{};

            u8 input{};
            u32 input_bit{}, input_pos{};
            u8 output[8]{};
            u32 output_bit{}, output_pos{};
            u32 cmd{};
            u32 status_reg[2]{0x82, 0};
            u32 date_time[7]{};
            u32 alarm1[3]{}, alarm2[3]{};

            u64 divider{};

            u64 minute_count{};
            u32 irq_flag{};
            u32 clock_adjust{};
            u32 free_register{};
            u32 alarm_date1[3]{};
            u32 alarm_date2[3]{};
            u32 FOUT1{}, FOUT2{};
            u64 sch_id{};
        } rtc{};

        struct {
            u32 rcnt{};
        } sio{};

        struct {
            u32 lcd_enable{};

            u32 speakers{};
            u32 wifi{};
            u32 wifi_waitcnt{};

        } powcnt{};

        u32 sio_data{};
    } io{};

    struct {
        union {
            struct {
                // byte 0
                u32 baudrate : 2;
                u32 _r : 5;
                u32 busy : 1;

                // byte 1
                u32 device : 2;
                u32 transfer_sz : 1;
                u32 chipselect_hold : 1;
                u32 _r2 : 2;
                u32 irq_enable : 1;
                u32 bus_enable : 1;
            };
            u32 u{};
        } cnt{};
        u32 enable{};
        u64 busy_until{};
        u32 input{}, output{};
        u32 chipsel{};

        struct {
            u32 hold{};
            u32 index{}, pos{};
            u32 dir{};
            u8 regs[4]{};
        } pwm{};

        struct {
            u32 write_enable{};
            u32 status{};

            u32 input[8]{};
            u32 output[8]{};
            struct {
                u32 pos{};
                u32 addr{};
                u32 cur{};
            } cmd{};

            u32 num_params{};

            u32 hold{};
        } firmware{};

        struct NDS_SPI_TOUCHSCREEN {
            union {
                struct {
                    u8 penirq : 1;
                    u8 modemsb: 1;
                    u8 ref_select : 1;
                    u8 conversion_mode : 1;
                    u8 chan_select : 3;
                    u8 start : 1;
                };
                u8 u{};
            } cnt{};
            u32 result{};
            u32 pos{};
            u32 touch_x{}, touch_y{};
            u32 hold{};

            physical_io_device *pio{};

            i32 adc_x_top_left{}, adc_y_top_left{};
            i32 adc_x_delta{}, adc_y_delta{};
            i32 screen_x_top_left{}, screen_y_top_left{};
            i32 screen_x_delta{}, screen_y_delta{};

        } touchscr{};

        u64 irq_id{};
        u64 irq_scheduled{};

        u32 hold{};
    } spi{};

    struct {
        bool described_inputs{};
        i64 cycles_left{};
        bool fast_boot{true};
    } jsm{};

    void DMA_init();
    template <bool do_debug> void dma7_go_ch(DMA_ch &ch);
    template <bool do_debug> void dma7_start(DMA_ch &ch, u32 i);
    template <bool do_debug> static void dma9_irq(void *ptr, u64 key, u64 cur_time, u32 jitter);
    template <bool do_debug>void dma9_go_ch(DMA_ch &ch);
    template <bool do_debug>void dma9_start(DMA_ch &ch, u32 i);
    template <bool do_debug>void check_dma7_at_vblank();
    template <bool do_debug>void check_dma9_at_vblank();
    template <bool do_debug>void check_dma9_at_hblank();
    template <bool do_debug>void trigger_dma7_if(u32 start_timing);
    template <bool do_debug>void trigger_dma9_if(u32 start_timing);
    DMA_ch dma7[4]{}, dma9[4]{};

    template<bool do_debug> void eval_irqs_7();
    template<bool do_debug> void eval_irqs_9();
    void eval_irqs();
    template <bool do_debug> void update_IF7(u32 bitnum);
    template <bool do_debug> void update_IF9(u32 bitnum);
    template <bool do_debug> void update_IFs_card(u32 bitnum);
    template <bool do_debug> void update_IFs(u32 bitnum);

    timer7_t timer7[4]{ timer7_t(this, 0), timer7_t(this, 1), timer7_t(this, 2), timer7_t(this, 3) };
    timer9_t timer9[4]{ timer9_t(this, 0), timer9_t(this, 1), timer9_t(this, 2), timer9_t(this, 3) };

    struct {
        double master_cycles_per_audio_sample{};
        double next_sample_cycle{};
        // Debug waveform scheduling
        double master_cycles_per_max_sample{};  // main waveform stride
        double master_cycles_per_min_sample{};  // channel waveform stride
        double next_sample_cycle_max{};
        double next_sample_cycle_min{};
    } audio{};

    struct {
        PPU::DBG_eng eng[2];
        struct NDS_DBG_tilemap_line_bg {
            u8 lines[1024 * 128]{}; // 4{}, 128*8 1-bit "was rendered or not" values
        } bg_scrolls[4]{};
        struct {
            u32 enable{};
            char str[257]{};
        } mgba{};
    } dbg_info{};

    CART::ridge cart;

    DBG_START
        DBG_EVENT_VIEW

        DBG_CPU_REG_START(arm7)
            *R[16]
        DBG_CPU_REG_END(arm7)

        DBG_CPU_REG_START(arm9)
                    *R[16]
        DBG_CPU_REG_END(arm9)

        DBG_IMAGE_VIEWS_START
            MDBG_IMAGE_VIEW(palettes)
            MDBG_IMAGE_VIEW(a_window0)
            MDBG_IMAGE_VIEW(a_window1)
            MDBG_IMAGE_VIEW(a_window2)
            MDBG_IMAGE_VIEW(a_window3)
            MDBG_IMAGE_VIEW(a_bg0)
            MDBG_IMAGE_VIEW(a_bg1)
            MDBG_IMAGE_VIEW(a_bg2)
            MDBG_IMAGE_VIEW(a_bg3)
            /*MDBG_IMAGE_VIEW(a_bg0map)
            MDBG_IMAGE_VIEW(a_bg1map)
            MDBG_IMAGE_VIEW(a_bg2map)
            MDBG_IMAGE_VIEW(a_bg3map)*/
            MDBG_IMAGE_VIEW(a_sprites)
            MDBG_IMAGE_VIEW(a_tiles)
            /*MDBG_IMAGE_VIEW(b_window0)
            MDBG_IMAGE_VIEW(b_window1)
            MDBG_IMAGE_VIEW(b_window2)
            MDBG_IMAGE_VIEW(b_window3)*/
            MDBG_IMAGE_VIEW(b_bg0)
            MDBG_IMAGE_VIEW(b_bg1)
            MDBG_IMAGE_VIEW(b_bg2)
            MDBG_IMAGE_VIEW(b_bg3)
            /*MDBG_IMAGE_VIEW(b_bg0map)
            MDBG_IMAGE_VIEW(b_bg1map)
            MDBG_IMAGE_VIEW(b_bg2map)
            MDBG_IMAGE_VIEW(b_bg3map)*/
            MDBG_IMAGE_VIEW(b_sprites)
            MDBG_IMAGE_VIEW(b_tiles)
            MDBG_IMAGE_VIEW(re_wireframe)
            MDBG_IMAGE_VIEW(re_attr)
            MDBG_IMAGE_VIEW(ppu_info)
            MDBG_IMAGE_VIEW(ppu_layers)
            MDBG_IMAGE_VIEW(dispcap)
            MDBG_IMAGE_VIEW(re_output)
            MDBG_IMAGE_VIEW(debug_cam)
        DBG_IMAGE_VIEWS_END
        DBG_WAVEFORM_START1
            DBG_WAVEFORM_MAIN
            DBG_WAVEFORM_CHANS(6)
        DBG_WAVEFORM_END1
        DBG_LOG_VIEW_SIMPLE
    DBG_END
public:
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
    void set_audio_ring(audio_output_ring *ring) final;
    void setup_debugger_interface(debugger_interface &intf) final;
    //void sideload(multi_file_set& mfs) final;

private:
    // Save-state helpers — implemented in nds_serialize.cpp
    void serialize_memory(serialized_state &state);
    void serialize_clock(serialized_state &state);
    void serialize_arm7(serialized_state &state);
    void serialize_arm9(serialized_state &state);
    void serialize_io(serialized_state &state);
    void serialize_spi(serialized_state &state);
    void serialize_dma(serialized_state &state);
    void serialize_timers(serialized_state &state);
    void serialize_apu(serialized_state &state);
    void serialize_ppu(serialized_state &state);
    void serialize_ge(serialized_state &state);
    void serialize_re(serialized_state &state);
    void serialize_cart(serialized_state &state);
    void serialize_audio(serialized_state &state);

    void deserialize_memory(serialized_state &state);
    void deserialize_clock(serialized_state &state);
    void deserialize_arm7(serialized_state &state);
    void deserialize_arm9(serialized_state &state);
    void deserialize_io(serialized_state &state);
    void deserialize_spi(serialized_state &state);
    void deserialize_dma(serialized_state &state);
    void deserialize_timers(serialized_state &state);
    void deserialize_apu(serialized_state &state);
    void deserialize_ppu(serialized_state &state);
    void deserialize_ge(serialized_state &state);
    void deserialize_re(serialized_state &state);
    void deserialize_cart(serialized_state &state);
    void deserialize_audio(serialized_state &state);

    void post_deserialize();
};
}
