#include "tg16_serialize.h"
#include "tg16_bus.h"
#include "tg16_cdrom.h"
#include "helpers/serialize/serialize.h"
#include "helpers/present/sys_present.h"
#include "component/gpu/huc6260/huc6260.h"

#define S(x) Sadd (state, &(x), sizeof(x))
#define L(x) Sload(state, &(x), sizeof(x))
#define SB(p,n) Sadd (state, (p), (n))
#define LB(p,n) Sload(state, (p), (n))

static void serialize_console (TG16::core &th, serialized_state &state);
static void deserialize_console(TG16::core &th, serialized_state &state);
static void serialize_clock (TG16::core &th, serialized_state &state);
static void deserialize_clock (TG16::core &th, serialized_state &state);
static void serialize_cpu (TG16::core &th, serialized_state &state);
static void deserialize_cpu (TG16::core &th, serialized_state &state);
static void serialize_vdc (HUC6270::chip &v, serialized_state &state);
static void deserialize_vdc (HUC6270::chip &v, serialized_state &state);
static void serialize_vce (TG16::core &th, serialized_state &state);
static void deserialize_vce (TG16::core &th, serialized_state &state);
static void serialize_cdrom (TG16::core &th, serialized_state &state);
static void deserialize_cdrom (TG16::core &th, serialized_state &state);


static void serialize_console(TG16::core &th, serialized_state &state) {
    state.new_section("console", TG16SS_console, 1);
    SB(th.RAM, sizeof(th.RAM));
    S(th.bram_initialized);
    S(th.audio.next_sample_cycle);
    S(th.audio.next_sample_cycle_min);
    S(th.audio.next_sample_cycle_max);
    S(th.audio.next_debug_cycle);
    S(th.audio.next_cdda_cycle);
    S(th.audio.next_adpcm_cycle);
    S(th.audio.cycles);
    S(th.audio.master_cycles_per_audio_sample);
    S(th.audio.master_cycles_per_min_sample);
    S(th.audio.master_cycles_per_max_sample);
    S(th.audio.master_cycles_per_cdda_sample);
    S(th.audio.master_cycles_per_adpcm_sample);
    if (th.is_cd)
        SB(th.cdrom_ram, sizeof(th.cdrom_ram));
    if (th.is_arcade_card)
        SB(th.ac_ram, sizeof(th.ac_ram));
}

static void deserialize_console(TG16::core &th, serialized_state &state) {
    LB(th.RAM, sizeof(th.RAM));
    L(th.bram_initialized);
    L(th.audio.next_sample_cycle);
    L(th.audio.next_sample_cycle_min);
    L(th.audio.next_sample_cycle_max);
    L(th.audio.next_debug_cycle);
    L(th.audio.next_cdda_cycle);
    L(th.audio.next_adpcm_cycle);
    L(th.audio.cycles);
    L(th.audio.master_cycles_per_audio_sample);
    L(th.audio.master_cycles_per_min_sample);
    L(th.audio.master_cycles_per_max_sample);
    L(th.audio.master_cycles_per_cdda_sample);
    L(th.audio.master_cycles_per_adpcm_sample);
    if (th.is_cd)
        LB(th.cdrom_ram, sizeof(th.cdrom_ram));
    if (th.is_arcade_card)
        LB(th.ac_ram, sizeof(th.ac_ram));
}

static void serialize_clock(TG16::core &th, serialized_state &state) {
    state.new_section("clock", TG16SS_clock, 1);
    auto &c = th.clock;
    S(c.master_cycles);
    S(c.master_frames);
    S(c.vdc.scanline_start);
    S(c.next.cpu);
    S(c.next.vce);
    S(c.next.timer);
    S(c.timing.scanline.cycles);
    S(c.timing.frame.cycles);
    S(c.timing.frame.lines);
    S(c.timing.second.cycles);
    S(c.timing.second.frames);
}

static void deserialize_clock(TG16::core &th, serialized_state &state) {
    auto &c = th.clock;
    L(c.master_cycles);
    L(c.master_frames);
    L(c.vdc.scanline_start);
    L(c.next.cpu);
    L(c.next.vce);
    L(c.next.timer);
    L(c.timing.scanline.cycles);
    L(c.timing.frame.cycles);
    L(c.timing.frame.lines);
    L(c.timing.second.cycles);
    L(c.timing.second.frames);
}

static void serialize_psg(HUC6280::PSG::core &psg, serialized_state &state) {
    S(psg.LMAL);
    S(psg.RMAL);
    S(psg.SEL);
    S(psg.LFO.FREQ);
    S(psg.LFO.TRG);
    S(psg.LFO.CTL);
    for (auto &ch : psg.channels) {
        S(ch.FREQ.u);
        S(ch.ON);
        S(ch.DDA);
        S(ch.AL);
        S(ch.LAL);
        S(ch.RAL);
        SB(ch.WAVEDATA, sizeof(ch.WAVEDATA));
        S(ch.wavectr);
        S(ch.NOISE.E);
        S(ch.NOISE.FREQ);
        S(ch.NOISE.COUNTER);
        S(ch.NOISE.state);
        S(ch.NOISE.output);
        S(ch.counter);
        S(ch.output);
        S(ch.output_l);
        S(ch.output_r);
        S(ch.ext_enable);
    }
    S(psg.ext_enable);
    S(psg.out.l);
    S(psg.out.r);
}

static void deserialize_psg(HUC6280::PSG::core &psg, serialized_state &state) {
    L(psg.LMAL);
    L(psg.RMAL);
    L(psg.SEL);
    L(psg.LFO.FREQ);
    L(psg.LFO.TRG);
    L(psg.LFO.CTL);
    for (auto &ch : psg.channels) {
        L(ch.FREQ.u);
        L(ch.ON);
        L(ch.DDA);
        L(ch.AL);
        L(ch.LAL);
        L(ch.RAL);
        LB(ch.WAVEDATA, sizeof(ch.WAVEDATA));
        L(ch.wavectr);
        L(ch.NOISE.E);
        L(ch.NOISE.FREQ);
        L(ch.NOISE.COUNTER);
        L(ch.NOISE.state);
        L(ch.NOISE.output);
        L(ch.counter);
        L(ch.output);
        L(ch.output_l);
        L(ch.output_r);
        L(ch.ext_enable);
    }
    L(psg.ext_enable);
    L(psg.out.l);
    L(psg.out.r);
}

static void serialize_cpu(TG16::core &th, serialized_state &state) {
    state.new_section("cpu", TG16SS_cpu, 1);
    auto &cpu = th.cpu;
    auto &r = cpu.regs;

    S(r.P.u);
    S(r.A); S(r.X); S(r.Y); S(r.S); S(r.PC);
    SB(r.MPR, sizeof(r.MPR));
    S(r.MPL);
    SB(r.TR, sizeof(r.TR));
    S(r.TA);
    { u32 zero = 0; S(zero); }
    S(r.do_IRQ);
    S(r.IR);
    S(r.timer_startstop);
    S(r.IRQR.u);
    S(r.IRQD.u);
    S(r.IRQR_polled.u);
    S(r.clock_div);

    S(cpu.timer.counter);
    S(cpu.timer.reload);

    S(cpu.pins.IRQ1);
    S(cpu.pins.IRQ2);
    S(cpu.pins.TIQ);
    S(cpu.io.buffer);

    S(cpu.extra_cycles);
    S(cpu.PCO);

    serialize_psg(cpu.psg, state);
}

static void deserialize_cpu(TG16::core &th, serialized_state &state) {
    auto &cpu = th.cpu;
    auto &r = cpu.regs;

    L(r.P.u);
    L(r.A); L(r.X); L(r.Y); L(r.S); L(r.PC);
    LB(r.MPR, sizeof(r.MPR));
    L(r.MPL);
    LB(r.TR, sizeof(r.TR));
    L(r.TA);
    { u32 tcu = 0; L(tcu); r.TCU = tcu; }
    L(r.do_IRQ);
    L(r.IR);
    L(r.timer_startstop);
    L(r.IRQR.u);
    L(r.IRQD.u);
    L(r.IRQR_polled.u);
    L(r.clock_div);

    L(cpu.timer.counter);
    L(cpu.timer.reload);

    L(cpu.pins.IRQ1);
    L(cpu.pins.IRQ2);
    L(cpu.pins.TIQ);
    L(cpu.io.buffer);

    L(cpu.extra_cycles);
    L(cpu.PCO);

    cpu.current_instruction = nullptr;

    deserialize_psg(cpu.psg, state);
}

static void serialize_vdc(HUC6270::chip &v, serialized_state &state) {
    SB(v.VRAM, sizeof(v.VRAM));
    SB(v.SAT, sizeof(v.SAT));

    S(v.timing.vblank_in_y);
    S(v.timing.vblank_out_y);
    S(v.timing.px_width);
    S(v.timing.px_height);
    { u32 hs = static_cast<u32>(v.timing.h.state); S(hs); }
    S(v.timing.h.counter);
    { u32 vs = static_cast<u32>(v.timing.v.state); S(vs); }
    S(v.timing.v.counter);

    S(v.io.RXR.u);
    S(v.io.BXR.u);
    S(v.io.BYR.u);
    S(v.io.MAWR.u);
    S(v.io.MARR.u);
    S(v.io.VWR.u);
    S(v.io.VRR.u);
    S(v.io.SOUR.u);
    S(v.io.DESR.u);
    S(v.io.LENR.u);
    S(v.io.DVSSR.u);
    S(v.io.RCR.u);
    S(v.io.CR.u);
    S(v.io.ADDR);
    S(v.io.STATUS.u);
    S(v.io.HSW);
    S(v.io.HDS);
    S(v.io.HDW);
    S(v.io.HDE);
    S(v.io.VSW);
    S(v.io.VDS);
    S(v.io.VDW.u);
    S(v.io.VCR);
    S(v.io.bg.x_tiles);
    S(v.io.bg.y_tiles);
    S(v.io.bg.x_tiles_mask);
    S(v.io.bg.y_tiles_mask);
    S(v.io.DCR.u);

    S(v.irq.line);

    S(v.latch.sprites_on);
    S(v.latch.bg_on);
    S(v.latch.BXR.u);
    S(v.latch.BYR.u);
    S(v.latch.VDW.u);
    S(v.latch.HSW);
    S(v.latch.HDS);
    S(v.latch.HDW);
    S(v.latch.HDE);
    S(v.latch.VSW);
    S(v.latch.VDS);
    S(v.latch.VCR);
    S(v.latch.bg.x_tiles);
    S(v.latch.bg.y_tiles);
    S(v.latch.bg.x_tiles_mask);
    S(v.latch.bg.y_tiles_mask);

    S(v.bg.x_tiles); S(v.bg.y_tiles);
    S(v.bg.x_tiles_mask); S(v.bg.y_tiles_mask);
    S(v.bg.y_compare);
    S(v.bg.x_tile); S(v.bg.y_tile);

    S(v.sprites.y_compare);
    S(v.sprites.num_tiles_on_line);
    for (auto &sp : v.sprites.tiles) {
        S(sp.pattern_shifter);
        S(sp.num_left);
        S(sp.triggered);
        S(sp.original_num);
        S(sp.x);
        S(sp.palette);
        S(sp.priority);
    }

    S(v.pixel_shifter.num);
    S(v.pixel_shifter.pattern_shifter);
    S(v.pixel_shifter.palette);

    S(v.regs.yscroll);
    S(v.regs.next_yscroll);
    S(v.regs.vram_inc);
    S(v.regs.vram_open_bus);
    S(v.regs.BAT_size);
    S(v.regs.px_out);
    S(v.regs.y_counter);
    S(v.regs.blank_line);
    S(v.regs.ignore_hsync);
    S(v.regs.ignore_vsync);
    S(v.regs.first_render);
    S(v.regs.draw_clock);
    S(v.regs.vram_satb_pending);
    S(v.regs.in_vblank);
    S(v.regs.IE);
    S(v.regs.x_counter);
    S(v.regs.HDW);
    S(v.regs.divisor);
    S(v.regs.yscroll_pending);
}

static void deserialize_vdc(HUC6270::chip &v, serialized_state &state) {
    LB(v.VRAM, sizeof(v.VRAM));
    LB(v.SAT, sizeof(v.SAT));

    L(v.timing.vblank_in_y);
    L(v.timing.vblank_out_y);
    L(v.timing.px_width);
    L(v.timing.px_height);
    { u32 hs; L(hs); v.timing.h.state = (HUC6270::states)hs; }
    L(v.timing.h.counter);
    { u32 vs; L(vs); v.timing.v.state = (HUC6270::states)vs; }
    L(v.timing.v.counter);

    L(v.io.RXR.u);
    L(v.io.BXR.u);
    L(v.io.BYR.u);
    L(v.io.MAWR.u);
    L(v.io.MARR.u);
    L(v.io.VWR.u);
    L(v.io.VRR.u);
    L(v.io.SOUR.u);
    L(v.io.DESR.u);
    L(v.io.LENR.u);
    L(v.io.DVSSR.u);
    L(v.io.RCR.u);
    L(v.io.CR.u);
    L(v.io.ADDR);
    L(v.io.STATUS.u);
    L(v.io.HSW);
    L(v.io.HDS);
    L(v.io.HDW);
    L(v.io.HDE);
    L(v.io.VSW);
    L(v.io.VDS);
    L(v.io.VDW.u);
    L(v.io.VCR);
    L(v.io.bg.x_tiles);
    L(v.io.bg.y_tiles);
    L(v.io.bg.x_tiles_mask);
    L(v.io.bg.y_tiles_mask);
    L(v.io.DCR.u);

    L(v.irq.line);

    L(v.latch.sprites_on);
    L(v.latch.bg_on);
    L(v.latch.BXR.u);
    L(v.latch.BYR.u);
    L(v.latch.VDW.u);
    L(v.latch.HSW);
    L(v.latch.HDS);
    L(v.latch.HDW);
    L(v.latch.HDE);
    L(v.latch.VSW);
    L(v.latch.VDS);
    L(v.latch.VCR);
    L(v.latch.bg.x_tiles);
    L(v.latch.bg.y_tiles);
    L(v.latch.bg.x_tiles_mask);
    L(v.latch.bg.y_tiles_mask);

    L(v.bg.x_tiles); L(v.bg.y_tiles);
    L(v.bg.x_tiles_mask); L(v.bg.y_tiles_mask);
    L(v.bg.y_compare);
    L(v.bg.x_tile); L(v.bg.y_tile);

    L(v.sprites.y_compare);
    L(v.sprites.num_tiles_on_line);
    for (auto &sp : v.sprites.tiles) {
        L(sp.pattern_shifter);
        L(sp.num_left);
        L(sp.triggered);
        L(sp.original_num);
        L(sp.x);
        L(sp.palette);
        L(sp.priority);
    }

    L(v.pixel_shifter.num);
    L(v.pixel_shifter.pattern_shifter);
    L(v.pixel_shifter.palette);

    L(v.regs.yscroll);
    L(v.regs.next_yscroll);
    L(v.regs.vram_inc);
    L(v.regs.vram_open_bus);
    L(v.regs.BAT_size);
    L(v.regs.px_out);
    L(v.regs.y_counter);
    L(v.regs.blank_line);
    L(v.regs.ignore_hsync);
    L(v.regs.ignore_vsync);
    L(v.regs.first_render);
    L(v.regs.draw_clock);
    L(v.regs.vram_satb_pending);
    L(v.regs.in_vblank);
    L(v.regs.IE);
    L(v.regs.x_counter);
    L(v.regs.HDW);
    L(v.regs.divisor);
    L(v.regs.yscroll_pending);
}

static void serialize_vce(TG16::core &th, serialized_state &state) {
    state.new_section("vce", TG16SS_vce, 1);
    auto &v = th.vce;
    SB(v.CRAM, sizeof(v.CRAM));
    S(v.master_frame);
    S(v.regs.clock_div);
    S(v.regs.num_lines);
    S(v.regs.y);
    S(v.regs.line_start);
    S(v.regs.hsync);
    S(v.regs.vsync);
    S(v.regs.frame_height);
    S(v.regs.next_frame_height);
    S(v.regs.bw);
    S(v.regs.cycles_per_frame);
    S(v.io.DCC);
    S(v.io.CTA.u);
    S(v.io.CTW.u);
}

static void deserialize_vce(TG16::core &th, serialized_state &state) {
    auto &v = th.vce;
    LB(v.CRAM, sizeof(v.CRAM));
    L(v.master_frame);
    L(v.regs.clock_div);
    L(v.regs.num_lines);
    L(v.regs.y);
    L(v.regs.line_start);
    L(v.regs.hsync);
    L(v.regs.vsync);
    L(v.regs.frame_height);
    L(v.regs.next_frame_height);
    L(v.regs.bw);
    L(v.regs.cycles_per_frame);
    L(v.io.DCC);
    L(v.io.CTA.u);
    L(v.io.CTW.u);
}

static void serialize_cdrom(TG16::core &th, serialized_state &state) {
    state.new_section("cdrom", TG16SS_cdrom, 1);
    auto &cd = th.cdrom;

    { u32 ph = static_cast<u32>(cd.scsi.phase); S(ph); }
    S(cd.scsi.BSY); S(cd.scsi.REQ); S(cd.scsi.MSG);
    S(cd.scsi.CD); S(cd.scsi.IO);
    SB(cd.scsi.cmd, sizeof(cd.scsi.cmd));
    S(cd.scsi.cmd_len); S(cd.scsi.cmd_pos);
    SB(cd.scsi.buf, sizeof(cd.scsi.buf));
    S(cd.scsi.buf_len); S(cd.scsi.buf_pos);
    S(cd.scsi.status_byte);
    S(cd.scsi.remaining_sectors);
    S(cd.scsi.buf_peeked);

    S(cd.cdda.LBA);
    S(cd.cdda.loop_start_lba);
    S(cd.cdda.loop_end_lba);
    S(cd.cdda.end_behavior);
    S(cd.cdda.sample_idx);
    S(cd.cdda.playing);
    S(cd.cdda.muted);
    S(cd.cdda.last_l);
    S(cd.cdda.last_r);

    S(cd.read_LBA);
    S(cd.seek.target_LBA);
    S(cd.seek.needs_seek);

    S(cd.irqc.active_irqs);
    S(cd.irqc.enabled_irqs);

    S(cd.bram_unlocked);
    S(cd.reset_reg);

    S(cd.fader.reg);
    S(cd.fader.enabled);
    S(cd.fader.adpcm_target);
    S(cd.fader.fast);
    S(cd.fader.start_clock);

    SB(cd.adpcm.ram, sizeof(cd.adpcm.ram));
    S(cd.adpcm.address_port);
    S(cd.adpcm.write_addr);
    S(cd.adpcm.read_addr);
    S(cd.adpcm.length);
    S(cd.adpcm.playing);
    S(cd.adpcm.play_request);
    S(cd.adpcm.nibble);
    S(cd.adpcm.half_reached);
    S(cd.adpcm.end_reached);
    S(cd.adpcm.control);
    S(cd.adpcm.dma_control);
    S(cd.adpcm.rate);
    S(cd.adpcm.read_buffer);
    S(cd.adpcm.read_pending);
    S(cd.adpcm.read_done_clock);
    S(cd.adpcm.current_output);
    S(cd.adpcm.magnitude);
    S(cd.adpcm.frac);
    S(cd.adpcm.ring_frac);
    S(cd.adpcm.last_sample);

    for (auto &ac : cd.ac) {
        S(ac.addr);
        S(ac.offset);
        S(ac.ctrl);
    }
}

static void deserialize_cdrom(TG16::core &th, serialized_state &state) {
    auto &cd = th.cdrom;

    { u32 ph; L(ph); cd.scsi.phase = (TG16::CDROM::core::PHASE)ph; }
    L(cd.scsi.BSY); L(cd.scsi.REQ); L(cd.scsi.MSG);
    L(cd.scsi.CD); L(cd.scsi.IO);
    LB(cd.scsi.cmd, sizeof(cd.scsi.cmd));
    L(cd.scsi.cmd_len); L(cd.scsi.cmd_pos);
    LB(cd.scsi.buf, sizeof(cd.scsi.buf));
    L(cd.scsi.buf_len); L(cd.scsi.buf_pos);
    L(cd.scsi.status_byte);
    L(cd.scsi.remaining_sectors);
    L(cd.scsi.buf_peeked);

    L(cd.cdda.LBA);
    L(cd.cdda.loop_start_lba);
    L(cd.cdda.loop_end_lba);
    L(cd.cdda.end_behavior);
    L(cd.cdda.sample_idx);
    L(cd.cdda.playing);
    L(cd.cdda.muted);
    L(cd.cdda.last_l);
    L(cd.cdda.last_r);

    L(cd.read_LBA);
    L(cd.seek.target_LBA);
    L(cd.seek.needs_seek);

    L(cd.irqc.active_irqs);
    L(cd.irqc.enabled_irqs);

    L(cd.bram_unlocked);
    L(cd.reset_reg);

    L(cd.fader.reg);
    L(cd.fader.enabled);
    L(cd.fader.adpcm_target);
    L(cd.fader.fast);
    L(cd.fader.start_clock);

    LB(cd.adpcm.ram, sizeof(cd.adpcm.ram));
    L(cd.adpcm.address_port);
    L(cd.adpcm.write_addr);
    L(cd.adpcm.read_addr);
    L(cd.adpcm.length);
    L(cd.adpcm.playing);
    L(cd.adpcm.play_request);
    L(cd.adpcm.nibble);
    L(cd.adpcm.half_reached);
    L(cd.adpcm.end_reached);
    L(cd.adpcm.control);
    L(cd.adpcm.dma_control);
    L(cd.adpcm.rate);
    L(cd.adpcm.read_buffer);
    L(cd.adpcm.read_pending);
    L(cd.adpcm.read_done_clock);
    L(cd.adpcm.current_output);
    L(cd.adpcm.magnitude);
    L(cd.adpcm.frac);
    L(cd.adpcm.ring_frac);
    L(cd.adpcm.last_sample);

    for (auto &ac : cd.ac) {
        L(ac.addr);
        L(ac.offset);
        L(ac.ctrl);
    }
}

void TG16::core::save_state(serialized_state &state) {
    state.version = 1;
    state.opt.len = 0;

    state.has_screenshot = 1;
    state.screenshot.allocate(HUC6260::CYCLE_PER_LINE, 242);
    state.screenshot.clear();
    tg16_present(vce.display_ptr.get(), state.screenshot.data.ptr,
                 HUC6260::CYCLE_PER_LINE, 242, 0, nullptr);

    serialize_console(*this, state);
    serialize_clock (*this, state);
    serialize_cpu (*this, state);
    state.new_section("vdc0", TG16SS_vdc0, 1); serialize_vdc(vdc0, state);
    state.new_section("vdc1", TG16SS_vdc1, 1); serialize_vdc(vdc1, state);
    serialize_vce (*this, state);
    if (is_cd)
        serialize_cdrom(*this, state);
}

void TG16::core::load_state(serialized_state &state, deserialize_ret &ret) {
    state.iter.offset = 0;

    for (auto &sec : state.sections) {
        state.iter.offset = sec.offset;
        switch (sec.kind) {
            case TG16SS_console: deserialize_console(*this, state); break;
            case TG16SS_clock: deserialize_clock (*this, state); break;
            case TG16SS_cpu: deserialize_cpu (*this, state); break;
            case TG16SS_vdc0: deserialize_vdc (vdc0, state); break;
            case TG16SS_vdc1: deserialize_vdc (vdc1, state); break;
            case TG16SS_vce: deserialize_vce (*this, state); break;
            case TG16SS_cdrom: if (is_cd) deserialize_cdrom(*this, state); break;
            default: break;
        }
    }

    cdrom.exec_slot = {};
    cdrom.read_slot = {};
    cdrom.ack_slot = {};
    cdrom.dma_slot = {};
    cdrom.adpcm_read_slot = {};
    killall();
}
