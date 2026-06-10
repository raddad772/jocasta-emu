#pragma once

#include "helpers/sys_interface.h"
#include "helpers/simplebuf.h"
#include "helpers/elf_helpers.h"
#include "helpers/setbits.h"

#include "dc_debugger.h"
#include "dc_maple.h"
#include "dc_controller.h"
#include "dc_mem.h"
#include "holly/holly.h"
#include "helpers/better_irq_multiplexer.h"
#include "dc_gdrom.h"
#include "dc_g2.h"
#include "aica/aica.h"
#include "component/cpu/sh4/sh4_interpreter.h"

namespace DREAMCAST {


struct core : jsm_system {
    core();
    ~core() override;

    template<u8 sz, bool do_debug>
    u64 mainbus_read(u32 addr) {
        bool success = true;
        u32 full_addr = addr;
        addr &= 0x1FFF'FFFF;
        auto &t = mem.pages[do_debug][sz >> 1][getbits<20, 28>(addr)];
        u64 v = t.read(t.ptr, addr, &success);
        if constexpr(do_debug) {
            dbgloglog(DCD_BUS_READ, DBGLS_TRACE, "RD %08x(%d): %08llx", addr, sz, v);
        }

        if (!success) {
            printf("\nMISSED READ %08x(%d)\n", full_addr, sz);
            static int a = 0;
            if (a++ >= 10) {
                dbg_break("TOO MANY BAD READ", master_cycles);
            }
        }
        return v;
    }

    template<u8 sz, bool do_debug>
    void mainbus_write(u32 addr, u64 val) {
        bool success = true;
        u32 full_addr = addr;
        addr &= 0x1FFF'FFFF;
        auto &t = mem.pages[do_debug][sz >> 1][getbits<20, 28>(addr)];
        t.write(t.ptr, addr, val, &success);
        if constexpr(do_debug) {
            dbgloglog(DCD_BUS_WRITE, DBGLS_TRACE, "WR %08x(%d): %08llx", addr, sz, val);
        }

        if (!success) {
            printf("\nMISSED WRITE %08x(%d):%08llx\n", full_addr, sz, val);
            static int a = 0;
            if (a++ >= 10) {
                dbg_break("TOO MANY BAD WRITE", master_cycles);
            }
        }
    }


    void pause_cpu_for(u32 num);

    void run_block(u64 num);
    i32 cycles_left{};
    i32 cpu_pause_cycles{};

    scheduler_t scheduler;
    SH4::core cpu;
    HOLLY::core holly;
    MAPLE::core maple;
    G2 g2;
    GDROM::DRIVE gdrom;
    AICA::core aica;
    simplebuf8 BIOS{};
    struct {
        BUF exec{};
        BUF IPBIN{};
        bool has{};
        char filename[256]{};
        char path[256]{};
    } sideloaded{};

    MEM mem{};
    void *RAM{};

    struct {
        u32 broadcast{0}; // 0 -> NTSC{}, 1 -> PAL{}, 2 -> PAL/M{}, 3 -> PAL/N{}, 4 -> default
        u32 language{1}; // 0 -> JP{}, 1 -> EN{}, 2 -> DE{}, 3 -> FR{}, 4 -> SP{}, 5 -> IT{}, 6 -> default
        u32 region{1}; // 0 -> JP{}, 1 -> USA{}, 2 -> EU{}, 3 -> default
    } settings{};

    struct {
        u64 frame_start_cycle{};
    } clock{};
    struct {
        simplebuf8 buf{};
    } flash{};

    u64 master_cycles{};

    void new_frame(u64 clockc);
    void schedule_frame(u64 start_clock, bool is_first);

    void vblank(u64 clk, bool status);
    void hblank(u64 clk, bool status);

    u64 read_io(u32 addr, u8 sz, bool *success);
    void write_io(u32 addr, u8 sz, u64 val, bool *success);
    u64 read_io_g1(u32 addr, u8 sz, bool *success);
    void write_io_g1(u32 addr, u8 sz, u64 val, bool *success);
    u64 read_io_g2(u32 addr, u8 sz, bool *success);
    void write_io_g2(u32 addr, u8 sz, u64 val, bool *success);

    bool described_inputs{};
    void do_sideload();

private:
    void setup_mmap();
    void mmap_range(u32 range_start, u32 range_end, u32 do_debug, u32 sz, void *ptr, MEM_READ_FUNC rf, MEM_WRITE_FUNC wf);
    void mmap_io_range(u32 range_start, u32 range_end, u32 do_debug, u32 sz, void *ptr, MEM_READ_FUNC rf, MEM_WRITE_FUNC wf);

    void CPU_state_after_boot_rom();
    void RAM_state_after_boot_rom();
    elf_symbol_list32 elf_symbols{};


public:
    PERIPHERAL::CONTROLLER controller1{};

    u64 read_flash(u32 addr, u8 sz);
    DBG_START
        cvec_ptr<debugger_view> console_view{};
        DBG_LOG_VIEW_SIMPLE
        DBG_IMAGE_VIEWS_START
            MDBG_IMAGE_VIEW(sysinfo)
        DBG_IMAGE_VIEWS_END

        /*DBG_WAVEFORM2_START1
            DBG_WAVEFORM2_MAIN
            DBG_WAVEFORM2_BRANCH(channels, 24)
            DBG_WAVEFORM2_BRANCH(cd, 2)
            DBG_WAVEFORM2_BRANCH(reverb, 8)
        DBG_WAVEFORM2_END1*/
        DBG_MEMORY_VIEW

    DBG_END


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
    void option_changed(const char* key, i32 value) final;
    void dump_mem();
    //void save_state(serialized_state &state) final;
    //void load_state(serialized_state &state, deserialize_ret &ret) final;
    void setup_debugger_interface(debugger_interface &intf) final;
    void sideload(multi_file_set& mfs) final;

public:
    struct {
        u32 SB_C2DSTAT{};  // 0x005F6800
        u32 SB_C2DLEN{};  // 0x005F6804
        u32 SB_C2DST{};  // 0x005F6808
        u32 SB_SDSTAW{};  // 0x005F6810
        u32 SB_SDBAAW{};  // 0x005F6814
        u32 SB_SDWLT{};  // 0x005F6818
        u32 SB_SDLAS{};  // 0x005F681C
        u32 SB_SDST{};  // 0x005F6820
        u32 SB_DBREQM{};  // 0x005F6840
        u32 SB_BAVLWC{};  // 0x005F6844
        u32 SB_C2DPRYC{};  // 0x005F6848
        u32 SB_C2DMAXL{};  // 0x005F684C
        u32 SB_LMMODE0{};  // 0x005F6884
        u32 SB_LMMODE1{};  // 0x005F6888
        u32 SB_RBSPLT{};  // 0x005F68A0
        u32 SB_UKN5F68A4{};  // 0x005F68A4
        u32 SB_UKN5F68AC{};  // 0x005F68AC
        union {  // SB_ISTNRM
            struct {
                u32 render_end_video : 1; //1
                u32 render_end_isp : 1; // 2
                u32 render_end_tsp : 1; // 4
                u32 vblank_in : 1; // 8
                u32 vblank_out : 1;
                u32 hblank_in : 1;
                u32 end_of_tx_yuv : 1;
                u32 end_of_tx_opaque_list : 1;
                u32 end_of_tx_opaque_modifier_list : 1;
                u32 end_of_tx_translucent_list : 1;
                u32 end_of_tx_translucent_modifier_list : 1;
                u32 end_of_dma_pvr : 1;
                u32 end_of_dma_maple : 1;
                u32 vblank_over_maple : 1;
                u32 end_of_gdrom_dma : 1;
                u32 end_of_aica_dma : 1;
                u32 end_of_e1_dma : 1;
                u32 end_of_e2_dma : 1;
                u32 end_of_dev_dma : 1;
                u32 end_of_ch2_dma : 1;
                u32 end_of_sort_dma : 1;
                u32 end_of_tx_ptl : 1;
            };
            u32 u{};
        } SB_ISTNRM;  // 0x005F6900
        union {  // SB_ISTEXT
            struct {
                u32 gdrom : 1;
                u32 aica : 1;
                u32 modem : 1;
                u32 ext_device : 1;
            };
            u32 u{};
        } SB_ISTEXT;  // 0x005F6904
        union {  // SB_ISTERR
            struct {
                u32 render_isp_out_of_cache : 1;
                u32 render_hazard_processing_strip_buffer : 1;
                u32 ta_isp_tsp_parameter_overflow : 1;
                u32 ta_object_list_pointer_overflow : 1;
                u32 ta_illegal_parameter : 1;
                u32 ta_fifo_overflow : 1;
                u32 pvrif_illegal_addr : 1;
                u32 pvrif_dma_overrun : 1;
                u32 maple_illegal_addr : 1;
                u32 maple_dma_overrun : 1;
                u32 maple_write_fifo_overflow : 1;
                u32 maple_illegal_command : 1;
                u32 g1_illegal_addr : 1;
                u32 g1_gddma_overrun : 1;
                u32 g1_romflash_access_at_gdma : 1;
                u32 g2_aica_dma_illegal_addr : 1;
                u32 g2_e1_dma_illegal_addr : 1;
                u32 g2_e2_dma_illegal_addr : 1;
                u32 g2_dev_dma_illegal_adr : 1;
                u32 g2_aica_dma_overrun : 1;
                u32 g2_e1_dma_overrun : 1;
                u32 g2_e2_dma_overrun : 1;
                u32 g2_dev_dma_overrun : 1;
                u32 g2_aica_dma_timeout : 1;
                u32 g2_e1_dma_timeout : 1;
                u32 g2_e2_dma_timeout : 1;
                u32 g2_dev_dma_timeout : 1;
                u32 g2_cpu_timeout : 1;
                u32 ddt_if : 1;
                u32 : 2;
                u32 sh4_if : 1;
            };
            u32 u{};
        } SB_ISTERR;  // 0x005F6908
        u32 SB_IML2NRM{};  // 0x005F6910
        union {  // SB_IML2EXT
            struct {
                u32 gdrom : 1;
                u32 aica : 1;
                u32 modem : 1;
                u32 ext_device : 1;
            };
            u32 u{};
        } SB_IML2EXT;  // 0x005F6914
        union {  // SB_IML2ERR
            struct {
                u32 render_isp_out_of_cache : 1;
                u32 render_hazard_processing_strip_buffer : 1;
                u32 ta_isp_parameter_overflow : 1;
                u32 ta_object_list_pointer_overflow : 1;
                u32 ta_illegal_parameter : 1;
                u32 ta_fifo_overflow : 1;
                u32 pvrif_illegal_addr : 1;
                u32 pvrif_dma_overrun : 1;
                u32 maple_illegal_addr : 1;
                u32 maple_dma_overrun : 1;
                u32 maple_write_fifo_overflow : 1;
                u32 maple_illegal_cmd : 1;
                u32 g1_illegal_addr : 1;
                u32 g1_gdma_overrun : 1;
                u32 g1_romflash_access_at_gdma : 1;
                u32 g2_aica_dma_illegal_addr_set : 1;
                u32 g2_ext_dma1_illegal_addr_set : 1;
                u32 g2_ext_dma2_illegal_addr_set : 1;
                u32 g2_dev_dma_illegal_addr_set : 1;
                u32 g2_aica_dma_overrun : 1;
                u32 g2_ext_dma1_overrun : 1;
                u32 g2_ext_dma2_overrun : 1;
                u32 g2_dev_dma_overrun : 1;
                u32 g2_aica_dma_timeout : 1;
                u32 g2_ext_dma1_timeout : 1;
                u32 g2_ext_dma2_timeout : 1;
                u32 g2_dev_dma_timeout : 1;
                u32 cpu_acess_timeout : 1;
                u32 sort_dma_cmd_error : 1;
                u32 : 2;
                u32 sh4_if : 1;
            };
            u32 u{};
        } SB_IML2ERR;  // 0x005F6918
        u32 SB_IML4NRM{};  // 0x005F6920
        union {  // SB_IML4EXT
            struct {
                u32 gdrom : 1;
                u32 aica : 1;
                u32 modem : 1;
                u32 ext_device : 1;
            };
            u32 u{};
        } SB_IML4EXT;  // 0x005F6924
        union {  // SB_IML4ERR
            struct {
                u32 render_isp_out_of_cache : 1;
                u32 render_hazard_processing_strip_buffer : 1;
                u32 ta_isp_parameter_overflow : 1;
                u32 ta_object_list_pointer_overflow : 1;
                u32 ta_illegal_parameter : 1;
                u32 ta_fifo_overflow : 1;
                u32 pvrif_illegal_addr : 1;
                u32 pvrif_dma_overrun : 1;
                u32 maple_illegal_addr : 1;
                u32 maple_dma_overrun : 1;
                u32 maple_write_fifo_overflow : 1;
                u32 maple_illegal_cmd : 1;
                u32 g1_illegal_addr : 1;
                u32 g1_gdma_overrun : 1;
                u32 g1_romflash_access_at_gdma : 1;
                u32 g2_aica_dma_illegal_addr_set : 1;
                u32 g2_ext_dma1_illegal_addr_set : 1;
                u32 g2_ext_dma2_illegal_addr_set : 1;
                u32 g2_dev_dma_illegal_addr_set : 1;
                u32 g2_aica_dma_overrun : 1;
                u32 g2_ext_dma1_overrun : 1;
                u32 g2_ext_dma2_overrun : 1;
                u32 g2_dev_dma_overrun : 1;
                u32 g2_aica_dma_timeout : 1;
                u32 g2_ext_dma1_timeout : 1;
                u32 g2_ext_dma2_timeout : 1;
                u32 g2_dev_dma_timeout : 1;
                u32 cpu_acess_timeout : 1;
                u32 sort_dma_cmd_error : 1;
                u32 : 2;
                u32 sh4_if : 1;
            };
            u32 u{};
        } SB_IML4ERR;  // 0x005F6928
        u32 SB_IML6NRM{};  // 0x005F6930
        union {  // SB_IML6EXT
            struct {
                u32 gdrom : 1;
                u32 aica : 1;
                u32 modem : 1;
                u32 ext_device : 1;
            };
            u32 u{};
        } SB_IML6EXT;  // 0x005F6934
        union {  // SB_IML6ERR
            struct {
                u32 render_isp_out_of_cache : 1;
                u32 render_hazard_processing_strip_buffer : 1;
                u32 ta_isp_parameter_overflow : 1;
                u32 ta_object_list_pointer_overflow : 1;
                u32 ta_illegal_parameter : 1;
                u32 ta_fifo_overflow : 1;
                u32 pvrif_illegal_addr : 1;
                u32 pvrif_dma_overrun : 1;
                u32 maple_illegal_addr : 1;
                u32 maple_dma_overrun : 1;
                u32 maple_write_fifo_overflow : 1;
                u32 maple_illegal_cmd : 1;
                u32 g1_illegal_addr : 1;
                u32 g1_gdma_overrun : 1;
                u32 g1_romflash_access_at_gdma : 1;
                u32 g2_aica_dma_illegal_addr_set : 1;
                u32 g2_ext_dma1_illegal_addr_set : 1;
                u32 g2_ext_dma2_illegal_addr_set : 1;
                u32 g2_dev_dma_illegal_addr_set : 1;
                u32 g2_aica_dma_overrun : 1;
                u32 g2_ext_dma1_overrun : 1;
                u32 g2_ext_dma2_overrun : 1;
                u32 g2_dev_dma_overrun : 1;
                u32 g2_aica_dma_timeout : 1;
                u32 g2_ext_dma1_timeout : 1;
                u32 g2_ext_dma2_timeout : 1;
                u32 g2_dev_dma_timeout : 1;
                u32 cpu_acess_timeout : 1;
                u32 sort_dma_cmd_error : 1;
                u32 : 2;
                u32 sh4_if : 1;
            };
            u32 u{};
        } SB_IML6ERR;  // 0x005F6938
        u32 SB_PDTNRM{};  // 0x005F6940
        u32 SB_PDTEXT{};  // 0x005F6944
        u32 SB_G2DTNRM{};  // 0x005F6950
        u32 SB_G2DTEXT{};  // 0x005F6954
        u32 SB_FFST{}, SB_FFST_rc{};
    } io{};
    struct {
        u32 SB_GDSTAR{};  // 0x005F7404
        u32 SB_GDLEN{};  // 0x005F7408
        u32 SB_GDDIR{};  // 0x005F740C
        u32 SB_GDEN{};  // 0x005F7414
        u32 SB_GDST{};  // 0x005F7418
        u32 SB_G1RRC{};  // 0x005F7480
        u32 SB_G1RWC{};  // 0x005F7484
        u32 SB_G1FRC{};  // 0x005F7488
        u32 SB_G1FWC{};  // 0x005F748C
        u32 SB_G1CRC{};  // 0x005F7490
        u32 SB_G1CWC{};  // 0x005F7494
        u32 SB_G1GDRC{};  // 0x005F74A0
        u32 SB_G1GDWC{};  // 0x005F74A4
        u32 SB_G1CRDYC{};  // 0x005F74B4
        union {  // SB_GDAPRO
            struct {
                u32 bottom_address : 8;
                u32 top_address : 7;
            };
            u32 u{};
        } SB_GDAPRO;  // 0x005F74B8
    } g1{};

private:
    void write_C2DST(u32 val);
    void write_SDST(u32 val);
};

}