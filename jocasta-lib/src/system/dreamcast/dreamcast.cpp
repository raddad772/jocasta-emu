//
// Created by Dave on 2/11/2024.
//

// chu chu rocket
// AICA stub
// It'll try to read the 32-bit word @ 0x8000F8 and expect it to have the value 0x43455845, it just hangs if that address contains anything else

#include <cassert>

#include "dreamcast.h"
#include "dc_bus.h"
#include "helpers/elf_helpers.h"
#include "helpers/multisize_memaccess.cpp"

#include "vendor/elf-parser/elf-parser.h"

#define IP_BIN

#define JITTER 448

jsm_system *DC_new()
{
    return new DREAMCAST::core();
}

void DC_delete(jsm_system *sys) {
    delete sys;
}

namespace DREAMCAST {

void core::enable_tracing()
{
    assert(1==0);
}

void core::disable_tracing()
{
    assert(1==0);
}

void core::play()
{
    holly.cur_output = static_cast<u32 *>(holly.display->output[0]);
}

void core::pause()
{

}

void core::stop()
{
    holly.copy_fb();
}

void core::get_framevars(framevars& out)
{
    out.master_cycle = master_cycles;
    out.master_frame = holly.master_frame;
    out.last_used_buffer = holly.display->active_draw_buffer;
}

void core::reset()
{
    cpu.reset();
    aica.reset();
    holly.reset();
    gdrom.reset();
    g2.reset();
    scheduler.clear();
    master_cycles = 0;
    master_cycles = 0;

    schedule_frame(0, true);

    if (sideloaded.has) {
        do_sideload();
    }
}


void core::killall()
{

}

void core::new_frame(u64 clockc)
{
    clock.frame_start_cycle = clockc;
    holly.master_frame++;
    holly.new_frame();
    holly.master_frame++;
}

enum frame_events {
    evt_EMPTY=0,
    evt_FRAME_START,
    evt_VBLANK_IN,
    evt_VBLANK_OUT,
    evt_FRAME_END,
};

void core::vblank(u64 clk, bool status) {
    if (status) holly.vblank_in();
    else holly.vblank_out();
}

void core::hblank(u64 clk, bool status) {

}


static void vblank(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->vblank(clock - jitter, key);
}

static void hblank(void *ptr, u64 key, u64 clock, u32 jitter) {
    auto *th = static_cast<core *>(ptr);
    th->hblank(clock - jitter, key);
}

void do_next_scheduled_frame(void *bound_ptr, u64 key, u64 current_clock, u32 jitter)
{
    auto *th = static_cast<core *>(bound_ptr);
    th->schedule_frame(current_clock-jitter, false);
}


void core::schedule_frame(u64 start_clock, bool is_first)
{
    // events
    // frame start @0.
    // vblank_in_start @?
    // vblank_out_start @?
    // frame end @200mil
    if (!is_first) {
        new_frame(start_clock);
        clock.frame_start_cycle = start_clock;
    }
    else {
        // TODO: audio setup stuff (see PS1)
        holly.recalc_frame_timing();
        aica.schedule_first();
    }

    // Schedule a frame!
    i64 cur_clock = start_clock;

    holly.schedule_frame(&DREAMCAST::vblank, cur_clock);

    scheduler.only_add_abs_w_tag(start_clock+holly.timing.cycles_per_frame, 0, this, &do_next_scheduled_frame, nullptr, 1);
}

void core::dump_mem() {
    FILE *f = fopen("/Users/dave/dreamcast.ram", "wb");
    fwrite(RAM, 16 * 1024 * 1024, 1, f);
    fflush(f);
    fclose(f);
    printf("\nDUMPED RAM to /Users/dave/dreamcast.ram!");
}

u32 core::finish_frame()
{
    //u64 last = master_cycles;
    if (::dbg.do_debug) scheduler.run_til_tag<true>(1);
    else scheduler.run_til_tag<false>(1);
    if (::dbg.do_break) {
        dump_mem();
    }

    //u32 cycles = static_cast<u32>(master_cycles - last);
    //printf("\nNUM CYCLES: %d, *60:%d", cycles, cycles*60);
    return 0;
}

u32 core::finish_scanline()
{
    assert(1==0);
    return 0;
}

u32 core::step_master(u32 howmany)
{
    scheduler.run_for_cycles(howmany);
    holly.copy_fb();
    return 0;
}

void core::load_BIOS(multi_file_set& mfs)
{
    // We expect dc_boot.bin and dc_flash.bin
    u32 found = 0;
    for (u32 i = 0; i < mfs.files.size(); i++) {
        read_file_buf* rfb = &mfs.files[i];
        if (!strcmp(rfb->name, "dc_boot.bin")) {
            BIOS.copy_from_buf(rfb->buf);
            found++;
        }
        else if (!strcmp(rfb->name, "dc_flash.bin")) {
            flash.buf.copy_from_buf(rfb->buf);
            found++;
        }
        else {
            printf("\n UNKNOWN FILE? %s", rfb->name);
        }
    }
    if (found != 2) {
        printf("\nHmmm what?!?!?! DC BIOS LOAD FAILURE!?!?!");
        fflush(stdout);
    }
}

static void DCIO_close_drive(jsm_system *ptr)
{

}

static void DCIO_open_drive(jsm_system *ptr)
{

}

static void DCIO_remove_disc(jsm_system *ptr)
{

}

static void DCIO_insert_disc(jsm_system *ptr, physical_io_device &pio, multi_file_set &mfs)
{
    printf("\nJSM INSERT DISC");
    auto *th = static_cast<core *>(ptr);
    th->gdrom.insert_disc(mfs);
}


static void setup_crt(JSM_DISPLAY *d)
{
    d->kind = jsm::CRT;
    d->enabled = true;

    d->fps = 59.94;  // NTSC standard; software may vary via SPG_LOAD
    // removed: d->fps_override_hint = 60;

    d->pixelometry.cols.left_hblank = 0;
    d->pixelometry.cols.right_hblank = 168;
    d->pixelometry.cols.visible = 640;
    d->pixelometry.cols.max_visible = 640;
    d->pixelometry.offset.x = 0;

    d->pixelometry.rows.top_vblank = 0;
    d->pixelometry.rows.visible = 480;
    d->pixelometry.rows.max_visible = 480;
    d->pixelometry.rows.bottom_vblank = 48;
    d->pixelometry.offset.y = 0;

    d->geometry.physical_aspect_ratio.width = 4;
    d->geometry.physical_aspect_ratio.height = 3;

    d->pixelometry.overscan.left = d->pixelometry.overscan.right = d->pixelometry.overscan.top = d->pixelometry.overscan.bottom = 0;
}

void core::describe_io()
{
    if (described_inputs) return;
    IOs.reserve(15);
    described_inputs = true;

    // power and reset buttons
    auto *chassis = &IOs.emplace_back();
    chassis->init(HID_CHASSIS, true, true, true, true);
    HID_digital_button* b;
    b = &chassis->chassis.digital_buttons.emplace_back();
    snprintf(b->name, sizeof(b->name), "Power");
    b->state = 1;
    b->common_id = DBCID_ch_power;

    b = &chassis->chassis.digital_buttons.emplace_back();
    b->common_id = DBCID_ch_reset;
    snprintf(b->name, sizeof(b->name), "Reset");
    b->state = 0;

    // GDROM
    auto *d = &IOs.emplace_back();
    d->init(HID_DISC_DRIVE, true, true, true, false);
    d->disc_drive.insert_disc = &DCIO_insert_disc;
    d->disc_drive.remove_disc = &DCIO_remove_disc;
    d->disc_drive.open_drive = &DCIO_open_drive;
    d->disc_drive.close_drive = &DCIO_close_drive;

    // screen
    d = &IOs.emplace_back();
    d->init(HID_DISPLAY, true, true, false, true);
    setup_crt(&d->display);
    d->display.allocate_output(0, 640 * 480 * 4);
    d->display.allocate_output(1, 640 * 480 * 4);
    d->display.output_debug_metadata[0] = nullptr;
    d->display.output_debug_metadata[1] = nullptr;
    holly.display_ptr.make(IOs, IOs.size()-1);
    holly.cur_output = static_cast<u32 *>(d->display.output[0]);
    d->display.active_draw_buffer = 0;
    //d->display.last_displayed = 1;

    d = &IOs.emplace_back();
    controller1.setup_pio(d, 0, "Controller 1", true);
    controller1.connect_to_port(&maple.ports[0]);

    holly.display = &holly.display_ptr.get().display;
}


void core::option_changed(const char* key, i32 value)
{
    if (strcmp(key, "aica_cached_interp") == 0) {
        if (value == 1 && !aica.cpu.cached_mode) {
            aica.cpu.enter_cached_mode();
        } else if (value == 0 && aica.cpu.cached_mode) {
            aica.cpu.exit_cached_mode();
        }
    }
}


// Thank you for this Deecey
void core::CPU_state_after_boot_rom()
{
    //SH4_SR_set(sh4, 0x400000F1);
    cpu.regs.SR_set(0x600000f0);
    cpu.regs.FPSCR_set(0x00040001);

    cpu.regs.R[0] = 0x8c010000; //0xAC0005D8;
    cpu.regs.R[1] = 0x00000808; // 0x00000009;
    cpu.regs.R[2] = 0x8c00e070; // 0xAC00940C;
    cpu.regs.R[3] = 0x8c010000; // 0;
    cpu.regs.R[4] = 0x8c010000; //0xAC008300;
    cpu.regs.R[5] = 0xF4000000;
    cpu.regs.R[6] = 0xF4002000;
    cpu.regs.R[7] = 0x00000044;
    cpu.regs.R[8] = 0;
    cpu.regs.R[9] = 0;
    cpu.regs.R[10] = 0;
    cpu.regs.R[11] = 0;
    cpu.regs.R[12] = 0;
    cpu.regs.R[13] = 0;
    cpu.regs.R[14] = 0;
    cpu.regs.R[15] = 0x8c00f400;

    cpu.regs.R_[0] = 0x600000F0; // 0xDFFFFFFF;
    cpu.regs.R_[1] = 0x00000808; // 0x500000F1;
    cpu.regs.R_[2] = 0x8c00e070; // 0;
    cpu.regs.R_[3] = 0;
    cpu.regs.R_[4] = 0;
    cpu.regs.R_[5] = 0;
    cpu.regs.R_[6] = 0;
    cpu.regs.R_[7] = 0;

    cpu.regs.fb[0].U32[4] = 0x3F266666;
    cpu.regs.fb[0].U32[5] = 0x3FE66666;
    cpu.regs.fb[0].U32[6] = 0x41840000;
    cpu.regs.fb[0].U32[7] = 0x3F800000;
    cpu.regs.fb[0].U32[8] = 0x80000000;
    cpu.regs.fb[0].U32[9] = 0x80000000;
    cpu.regs.fb[0].U32[11] = 0x3F800000;

    cpu.regs.GBR = 0x8C000000;
    cpu.regs.SSR = 0x40000001;
    cpu.regs.SPC = 0x8C000776;
    cpu.regs.SGR = 0x8D000000;
    cpu.regs.DBR = 0x8C000010;
    cpu.regs.VBR = 0x8c000000; //0x8C000000;
    cpu.regs.PR = 0x8c00e09c;//0x0C00043C;
    cpu.regs.FPUL.u = 0;

    cpu.regs.PC = 0xAC008300; // IP.bin start address
}

void core::RAM_state_after_boot_rom()
{
    memset(static_cast<u8 *>(RAM), 0, 0x1000000);

    for (u32 i = 0; i < 16; i++) {
        mainbus_write<2, false>(0x8C0000E0 + 2 * i, mainbus_read<2, false>(0x800000FE - 2 * i));
    }
    mainbus_write<4, false>(0xA05F74E4, 0x001FFFFF);

    memcpy(static_cast<u8 *>(RAM) + 0x100, ((u8 *)BIOS.ptr) + 0x100, 0x3F00);
    memcpy(static_cast<u8 *>(RAM) + 0x8000, ((u8 *)BIOS.ptr) + 0x8000, 0x1F800);

    mainbus_write<4, false>(0x8C0000B0, 0x8C003C00);
    mainbus_write<4, false>(0x8C0000B4, 0x8C003D80);
    mainbus_write<4, false>(0x8C0000B8, 0x8C003D00);
    mainbus_write<4, false>(0x8C0000BC, 0x8C001000);
    mainbus_write<4, false>(0x8C0000C0, 0x8C0010F0);
    mainbus_write<4, false>(0x8C0000E0, 0x8C000800);

    mainbus_write<4, false>(0x8C0000AC, 0xA05F7000);
    mainbus_write<4, false>(0x8C0000A8, 0xA0200000);
    mainbus_write<4, false>(0x8C0000A4, 0xA0100000);
    mainbus_write<4, false>(0x8C0000A0, 0);
    mainbus_write<4, false>(0x8C00002C, 0);
    mainbus_write<4, false>(0x8CFFFFF8, 0x8C000128);

    //         // Load IP.bin from disk (16 first sectors of the last track)
    //        // FIXME: Here we assume the last track is the 3rd.
    // TODO
    if (true) {
        printf("\nLoading IP.BIN...");
        memcpy(static_cast<u8 *>(RAM) + 0x8000, sideloaded.IPBIN.ptr, 0x8000);
    }

    // IP.bin patches
    mainbus_write<2, false>(0xAC0090D8, 0x5113);
    mainbus_write<2, false>(0xAC00940A, 0xB);
    mainbus_write<2, false>(0xAC00940C, 0x9);

    mainbus_write<4, false>(0x8C000000, 0x00090009);
    mainbus_write<4, false>(0x8C000004, 0x001B0009);
    mainbus_write<4, false>(0x8C000008, 0x0009AFFD);

    mainbus_write<2, false>(0x8C00000C, 0);
    mainbus_write<2, false>(0x8C00000E, 0);

    mainbus_write<4, false>(0x8C000010, 0x00090009);
    mainbus_write<4, false>(0x8C000014, 0x0009002B);

    mainbus_write<4, false>(0x8C000018, 0x00090009);
    mainbus_write<4, false>(0x8C00001C, 0x0009000B);

    mainbus_write<1, false>(0x8C00002C, 0x16);
    mainbus_write<4, false>(0x8C000064, 0x8C008100);
    mainbus_write<2, false>(0x8C000090, 0);
    mainbus_write<2, false>(0x8C000092, -128);

    // Write some default values to HOLLY for demos etc...
    mainbus_write<4, false>(0x005F8048, 6);          // FB_W_CTRL
    mainbus_write<4, false>(0x005F8060, 0x00600000); // FB_W_SOF1
    mainbus_write<4, false>(0x005F8064, 0x00600000); // FB_W_SOF2
    mainbus_write<4, false>(0x005F8044, 0x0080000D); // FB_R_CTRL
    mainbus_write<4, false>(0x005F8050, 0x00200000); // FB_R_SOF1
    mainbus_write<4, false>(0x005F8054, 0x00200000); // FB_R_SOF2
    mainbus_write<4, false>(0x005F8054, 0x00200000); // FB_R_SIZE
    //mainbus_write<4, false>(0x005F805C, 319 | (239 << 10) | (1 << 20));
    mainbus_write<4, false>(0x005F805C, 639 | (479 << 10) | (1 << 20));
    maple.SB_MDST = 0;
    g2.channel[3].io.ST = 0; // SB_DDST
}


// Thanks to Deecey for values to write
void core::sideload(multi_file_set& mfs) {
    sideloaded.has = true;
    strncpy(sideloaded.filename, mfs.files[0].name, sizeof(sideloaded.filename));
    strncpy(sideloaded.path, mfs.files[0].path, sizeof(sideloaded.path));
    sideloaded.exec.copy(&mfs.files[0].buf);
    sideloaded.IPBIN.copy(&mfs.files[1].buf);
}

void core::do_sideload() {
    CPU_state_after_boot_rom();
    RAM_state_after_boot_rom();

#ifdef DC_SUPPORT_ELF
    if (ends_with(sideloaded.filename, ".elf")) {
        char YOYO[500];
        snprintf(YOYO, sizeof(YOYO), "%s/%s", sideloaded.path, sideloaded.filename);
        printf("\nOPEN ELF %s\n", YOYO);
        Elf32_Ehdr eh;        /* elf-header is fixed size */
        int fd = open(YOYO, O_RDONLY);
        Elf32_Sym* sym_tbl = nullptr;
        char *str_tabl = nullptr;

        read_elf_header(fd, &eh);
        if (!is_ELF(eh)) {
            printf("\nNOT ELFT!");
            close(fd);
            return;
        }
        if (is64Bit(eh)) {
            printf("\nIS 64!");
            return;
        }
        Elf32_Shdr *sh_table=nullptr;    /* section-header table is variable size */
        //print_elf_header(eh);
        cpu.regs.PC = eh.e_entry;
        printf("\nELF ENTRYPOINT %08x", cpu.regs.PC);

        sh_table = static_cast<Elf32_Shdr *>(malloc(eh.e_shentsize * eh.e_shnum));
        read_section_header_table(fd, eh, sh_table);

        // Load to RAM and load symbols
        char *sh_str = read_section(fd, sh_table[eh.e_shstrndx]);
        for (int i = 0; i < eh.e_shnum; i++) {
            u32 addr = sh_table[i].sh_addr;
            if (addr >= 0x8C000000) {
                if (addr >= (0x8C000000+0x1000000)) {
                    printf("\nLoad address too high! %08x", addr);
                    continue;
                }
                if ((addr+sh_table[i].sh_size) >= (0x8C000000+0x1000000)) {
                    printf("\nLoad size too high! %08x", sh_table[i].sh_size);
                    continue;
                }
                //printf("\nELF loading %s to 0x%08x", (sh_str + sh_table[i].sh_name), sh_table[i].sh_addr);
                memcpy(static_cast<u8 *>(RAM) + addr - 0x8C000000, &static_cast<u8 *>(sideloaded.exec.ptr)[sh_table[i].sh_offset], sh_table[i].sh_size);
            }
            if ((sh_table[i].sh_type == SHT_SYMTAB) || (sh_table[i].sh_type==SHT_DYNSYM)) {
                u32 symbol_count;
                sym_tbl = (Elf32_Sym*)read_section(fd, sh_table[i]);
                u32 str_tbl_ndx = sh_table[i].sh_link;
                str_tabl = read_section(fd, sh_table[str_tbl_ndx]);

                symbol_count = (sh_table[i].sh_size/sizeof(Elf32_Sym));
                char *fname = nullptr;

                for(u32 j=0; j < symbol_count; j++) {
                    u32 st_type = ELF32_ST_TYPE(sym_tbl[j].st_info);
                    if (st_type == STT_FILE) {
                        fname = (str_tabl + sym_tbl[j].st_name);
                        continue;
                    }
                    if (st_type == STT_SECTION) continue;
                    if (strlen((str_tabl + sym_tbl[j].st_name)) < 2) continue;

                    if ((st_type == STT_FUNC) || (st_type == STT_OBJECT) || (st_type == STT_NOTYPE)) {
                        elf_symbol32_kind esk;
                        switch (st_type) {
                            case STT_FUNC:
                                esk = esk_function;
                                break;
                            case STT_OBJECT:
                                esk = esk_object;
                                break;
                            case STT_NOTYPE:
                                esk = esk_unknown;
                                break;
                            default:
                                printf("\nUNKNOWN SYMBOL KIND HERE %d", st_type);
                                esk = esk_unknown;
                                break;
                        }
                        elf_symbols.add(sym_tbl[j].st_value, fname, (str_tabl + sym_tbl[j].st_name), esk);
                    }
                }
            }
        }
        printf("\nAlso loaded %ld symbols.", elf_symbols.symbols.size());

        if (str_tabl) free(str_tabl);
        if (sym_tbl) free(sym_tbl);
        if (sh_str) free(sh_str);
        if (sh_table) free(sh_table);
        close(fd);
    }
    else {
        printf("\nNot ELF, loading as .bin...");
#endif
        memcpy(static_cast<u8 *>(RAM) + 0x10000, sideloaded.exec.ptr, sideloaded.exec.size);
        cpu.regs.PC = 0xAC010000; // for like a demo
        //cpu.regs.PC = 0xAC008300; // for IP.BIN


#ifdef DC_SUPPORT_ELF
    }
#endif
}

}
