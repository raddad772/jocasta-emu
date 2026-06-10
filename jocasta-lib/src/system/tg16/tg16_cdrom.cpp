#include <cstring>
#include <cstdio>

#include "helpers/debug.h"
#include "tg16_cdrom.h"
#include "tg16_bus.h"

static constexpr u64 ADPCM_READ_LATENCY_CYCLES = 60ULL;

#define MASTER_HZ 21477272ULL
#define ONEFRAME (MASTER_HZ / 60)
#define CYCLES_PER_SECTOR_1X (MASTER_HZ / 75)

static u32 scsi_cmd_len(u8 cmd) {
    if (cmd == 0x00) return 6;
    if (cmd == 0x08) return 6;
    if ((cmd & 0xF0) == 0xD0) return 10;
    if ((cmd & 0xF0) == 0xE0) return 10;
    return 6;
}

// Thanks for this table Mesen2
static const i32 adpcm_step_size[392] = {
    0x0002,0x0006,0x000A,0x000E,0x0012,0x0016,0x001A,0x001E,
    0x0002,0x0006,0x000A,0x000E,0x0013,0x0017,0x001B,0x001F,
    0x0002,0x0006,0x000B,0x000F,0x0015,0x0019,0x001E,0x0022,
    0x0002,0x0007,0x000C,0x0011,0x0017,0x001C,0x0021,0x0026,
    0x0002,0x0007,0x000D,0x0012,0x0019,0x001E,0x0024,0x0029,
    0x0003,0x0009,0x000F,0x0015,0x001C,0x0022,0x0028,0x002E,
    0x0003,0x000A,0x0011,0x0018,0x001F,0x0026,0x002D,0x0034,
    0x0003,0x000A,0x0012,0x0019,0x0022,0x0029,0x0031,0x0038,
    0x0004,0x000C,0x0015,0x001D,0x0026,0x002E,0x0037,0x003F,
    0x0004,0x000D,0x0016,0x001F,0x0029,0x0032,0x003B,0x0044,
    0x0005,0x000F,0x0019,0x0023,0x002E,0x0038,0x0042,0x004C,
    0x0005,0x0010,0x001B,0x0026,0x0032,0x003D,0x0048,0x0053,
    0x0006,0x0012,0x001F,0x002B,0x0038,0x0044,0x0051,0x005D,
    0x0006,0x0013,0x0021,0x002E,0x003D,0x004A,0x0058,0x0065,
    0x0007,0x0016,0x0025,0x0034,0x0043,0x0052,0x0061,0x0070,
    0x0008,0x0018,0x0029,0x0039,0x004A,0x005A,0x006B,0x007B,
    0x0009,0x001B,0x002D,0x003F,0x0052,0x0064,0x0076,0x0088,
    0x000A,0x001E,0x0032,0x0046,0x005A,0x006E,0x0082,0x0096,
    0x000B,0x0021,0x0037,0x004D,0x0063,0x0079,0x008F,0x00A5,
    0x000C,0x0024,0x003C,0x0054,0x006D,0x0085,0x009D,0x00B5,
    0x000D,0x0027,0x0042,0x005C,0x0078,0x0092,0x00AD,0x00C7,
    0x000E,0x002B,0x0049,0x0066,0x0084,0x00A1,0x00BF,0x00DC,
    0x0010,0x0030,0x0051,0x0071,0x0092,0x00B2,0x00D3,0x00F3,
    0x0011,0x0034,0x0058,0x007B,0x00A0,0x00C3,0x00E7,0x010A,
    0x0013,0x003A,0x0061,0x0088,0x00B0,0x00D7,0x00FE,0x0125,
    0x0015,0x0040,0x006B,0x0096,0x00C2,0x00ED,0x0118,0x0143,
    0x0017,0x0046,0x0076,0x00A5,0x00D5,0x0104,0x0134,0x0163,
    0x001A,0x004E,0x0082,0x00B6,0x00EB,0x011F,0x0153,0x0187,
    0x001C,0x0055,0x008F,0x00C8,0x0102,0x013B,0x0175,0x01AE,
    0x001F,0x005E,0x009D,0x00DC,0x011C,0x015B,0x019A,0x01D9,
    0x0022,0x0067,0x00AD,0x00F2,0x0139,0x017E,0x01C4,0x0209,
    0x0026,0x0072,0x00BF,0x010B,0x0159,0x01A5,0x01F2,0x023E,
    0x002A,0x007E,0x00D2,0x0126,0x017B,0x01CF,0x0223,0x0277,
    0x002E,0x008A,0x00E7,0x0143,0x01A1,0x01FD,0x025A,0x02B6,
    0x0033,0x0099,0x00FF,0x0165,0x01CB,0x0231,0x0297,0x02FD,
    0x0038,0x00A8,0x0118,0x0188,0x01F9,0x0269,0x02D9,0x0349,
    0x003D,0x00B8,0x0134,0x01AF,0x022B,0x02A6,0x0322,0x039D,
    0x0044,0x00CC,0x0154,0x01DC,0x0264,0x02EC,0x0374,0x03FC,
    0x004A,0x00DF,0x0175,0x020A,0x02A0,0x0335,0x03CB,0x0460,
    0x0052,0x00F6,0x019B,0x023F,0x02E4,0x0388,0x042D,0x04D1,
    0x005A,0x010F,0x01C4,0x0279,0x032E,0x03E3,0x0498,0x054D,
    0x0063,0x012A,0x01F1,0x02B8,0x037F,0x0446,0x050D,0x05D4,
    0x006D,0x0148,0x0223,0x02FE,0x03D9,0x04B4,0x058F,0x066A,
    0x0078,0x0168,0x0259,0x0349,0x043B,0x052B,0x061C,0x070C,
    0x0084,0x018D,0x0296,0x039F,0x04A8,0x05B1,0x06BA,0x07C3,
    0x0091,0x01B4,0x02D8,0x03FB,0x051F,0x0642,0x0766,0x0889,
    0x00A0,0x01E0,0x0321,0x0461,0x05A2,0x06E2,0x0823,0x0963,
    0x00B0,0x0210,0x0371,0x04D1,0x0633,0x0793,0x08F4,0x0A54,
    0x00C2,0x0246,0x03CA,0x054E,0x06D2,0x0856,0x09DA,0x0B5E,
};

static const i32 adpcm_step_factor[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

namespace TG16::CDROM {

static void sch_exec_tramp(void *ptr, u64 key, u64 clock, u32 jitter) {
    static_cast<core *>(ptr)->sch_exec(key, clock - jitter);
}

static void sch_read_tramp(void *ptr, u64 key, u64 clock, u32 jitter) {
    static_cast<core *>(ptr)->sch_read(key, clock - jitter);
}

static void sch_ack_tramp(void *ptr, u64 key, u64 clock, u32 jitter) {
    static_cast<core *>(ptr)->sch_ack(key, clock - jitter);
}

static void sch_dma_tramp(void *ptr, u64 key, u64 clock, u32 jitter) {
    static_cast<core *>(ptr)->sch_dma(key, clock - jitter);
}

static void sch_adpcm_read_tramp(void *ptr, u64 key, u64 clock, u32 jitter) {
    static_cast<core *>(ptr)->sch_adpcm_read(key, clock - jitter);
}

core::core(TG16::core *parent) : sys(parent) {}

void core::reset() {
    scsi = {};
    cdda = {};
    seek = {};
    irqc = {};
    adpcm = {};
    adpcm.current_output = 2048;
    bram_unlocked = false;
    fader = {};
    exec_slot = {};
    read_slot = {};
    ack_slot = {};
    dma_slot = {};
    adpcm_read_slot = {};
}

void core::update_irq() {
    bool fire = (irqc.enabled_irqs & irqc.active_irqs) != 0;
    if (set_irq2) set_irq2(irq_ptr, fire);
}

void core::set_irq_source(u8 bit) {
    if (irqc.active_irqs & bit) return;
    irqc.active_irqs |= bit;
    update_irq();
}

void core::clear_irq_source(u8 bit) {
    if (!(irqc.active_irqs & bit)) return;
    irqc.active_irqs &= ~bit;
    update_irq();
}

void core::update_scsi_irq() {
    bool data_in = scsi.REQ && scsi.IO && scsi.BSY && !scsi.CD;
    bool status_msg = scsi.REQ && scsi.IO && scsi.BSY && scsi.CD;
    if (data_in) set_irq_source(0x40); else clear_irq_source(0x40);
    if (status_msg) set_irq_source(0x20); else clear_irq_source(0x20);
}

u8 core::read(u32 addr, u8 old) {
    if (sys->is_arcade_card && (addr & 0xFF) >= 0x10)
        return ac_read(addr, old);
    switch (addr & 0xFF) {
        case 0x0: {
            u8 v = 0;
            if (scsi.BSY) v |= 0x80;
            if (scsi.REQ) v |= 0x40;
            if (scsi.MSG) v |= 0x20;
            if (scsi.CD) v |= 0x10;
            if (scsi.IO) v |= 0x08;
            return v;
        }
        case 0x1: {
            u8 v = 0;
            if (scsi.phase == DATA_IN)
                v = (scsi.buf_pos < scsi.buf_len) ? scsi.buf[scsi.buf_pos] : 0;
            else
                v = scsi_pop();
            return v;
        }
        case 0x2: {
            u8 v = irqc.enabled_irqs;
            return v;
        }
        case 0x3: {
            u8 v = irqc.active_irqs;
            return v;
        }
        case 0x4: return reset_reg;
        case 0x5: return 0;
        case 0x6: return 0;
        case 0x7: return 0;

        case 0x8: {
            if (scsi.phase == DATA_IN && scsi.REQ) {
                u8 v = (scsi.buf_pos < scsi.buf_len) ? scsi.buf[scsi.buf_pos] : 0;
                scsi.buf_pos++;
                if (scsi.buf_pos >= scsi.buf_len) {
                    scsi.REQ = false;
                    update_scsi_irq();
                    schedule_exec(500);
                }
                return v;
            }
            return 0;
        }
        case 0x9: return fader.reg;

        case 0xA: {
            u64 now = static_cast<u64>(sys->scheduler.current_time());
            adpcm_complete_read_if_due(now);
            u8 result = adpcm.read_buffer;
            adpcm.read_pending = true;
            adpcm.read_done_clock = now + ADPCM_READ_LATENCY_CYCLES;
            schedule_adpcm_read(adpcm.read_done_clock);
            return result;
        }
        case 0xB: return adpcm.dma_control;
        case 0xC: {
            u64 now = static_cast<u64>(sys->scheduler.current_time());
            adpcm_complete_read_if_due(now);
            bool rp = adpcm.read_pending;
            u8 v = (adpcm.end_reached ? 0x01 : 0) |
                   (adpcm.playing ? 0x08 : 0) |
                   (rp ? 0x80 : 0);
            return v;
        }
        case 0xD: return adpcm.control;
        case 0xE: return adpcm.rate;

        case 0xC0: return 0x00;
        case 0xC1: return 0xAA;
        case 0xC2: return 0x55;
        case 0xC3: return 0x03;
        case 0xC5: return 0xFF;

        default: return old;
    }
}

u8 core::scsi_pop() {
    if (!scsi.REQ) return 0;
    if (scsi.phase == DATA_IN) {
        u8 v = (scsi.buf_pos < scsi.buf_len) ? scsi.buf[scsi.buf_pos] : 0;
        scsi.buf_pos++;
        scsi.REQ = false;
        update_scsi_irq();
        if (scsi.buf_pos >= scsi.buf_len) {
            schedule_exec(500);
        } else {
            schedule_ack(2000);
        }
        return v;
    }
    if (scsi.phase == STATUS) {
        u8 v = scsi.status_byte;
        scsi.REQ = false;
        update_scsi_irq();
        schedule_exec(500);
        update_irq();
        return v;
    }
    if (scsi.phase == MESSAGE_IN) {
        scsi.REQ = false;
        update_scsi_irq();
        schedule_exec(500);
        update_irq();
        return 0x00;
    }
    return 0;
}

void core::write(u32 addr, u8 val) {
    if (sys->is_arcade_card && (addr & 0xFF) >= 0x10) {
        ac_write(addr, val);
        return;
    }
    switch (addr & 0xFF) {
        case 0x0:
            if (val & 0x80) {
                scsi = {};
                irqc.active_irqs = 0;
                update_irq();
            }
            if (val & 0x01) {
                if (!scsi.BSY) {
                    scsi.phase = COMMAND;
                    scsi.BSY = true;
                    scsi.CD = true;
                    scsi.IO = false;
                    scsi.REQ = true;
                    scsi.MSG = false;
                    scsi.cmd_len = 0;
                    scsi.cmd_pos = 0;
                    update_scsi_irq();
                    update_irq();
                }
            }
            if (val & 0x04) {
                scsi_ack();
            }
            return;

        case 0x1:
            if (scsi.phase == COMMAND) {
                if (scsi.cmd_pos == 0)
                    scsi.cmd_len = scsi_cmd_len(val);
                scsi.cmd[scsi.cmd_pos++] = val;
                scsi.REQ = false;
                update_scsi_irq();
                if (scsi.cmd_pos >= scsi.cmd_len) {
                    schedule_exec(3000);
                } else {
                    schedule_ack(2000);
                }
            }
            return;

        case 0x2: {
            irqc.enabled_irqs = val & 0x7F;
            bool ack = (val & 0x80) != 0;
            if (ack) {
                if (scsi.REQ && (scsi.phase == DATA_IN || scsi.phase == STATUS || scsi.phase == MESSAGE_IN)) {
                    scsi.REQ = false;
                    update_scsi_irq();
                }
            } else {
                if (!scsi.REQ) {
                    if (scsi.phase == DATA_IN) {
                        scsi.buf_pos++;
                        if (scsi.buf_pos >= scsi.buf_len) {
                            schedule_exec(500);
                        } else {
                            scsi.REQ = true;
                            update_scsi_irq();
                        }
                    } else if (scsi.phase == STATUS) {
                        schedule_exec(500);
                    } else if (scsi.phase == MESSAGE_IN) {
                        schedule_exec(500);
                    }
                }
            }
            update_irq();
            return;
        }

        case 0x3:
            return;

        case 0x4: {
            bool rst_now = (val & 0x02) != 0;
            bool rst_prev = (reset_reg & 0x02) != 0;
            reset_reg = val & 0x0F;
            if (rst_now && !rst_prev) {
                irqc.active_irqs = 0;
                update_irq();
            }
            return;
        }
        case 0x5:
            return;
        case 0x6:
            return;
        case 0x7:
            bram_unlocked = !(val & 0x80);
            return;

        case 0x8:
            adpcm.address_port = (adpcm.address_port & 0xFF00) | val;
            if (adpcm.control & 0x10) adpcm.length = adpcm.address_port;
            return;
        case 0x9:
            adpcm.address_port = (adpcm.address_port & 0x00FF) | (static_cast<u16>(val) << 8);
            if (adpcm.control & 0x10) adpcm.length = adpcm.address_port;
            return;
        case 0xA:
            adpcm.ram[adpcm.write_addr] = val;
            adpcm.write_addr = (adpcm.write_addr + 1) & 0xFFFF;
            adpcm_set_half_reached(adpcm.length < 0x8000);
            if (!(adpcm.control & 0x10) && adpcm.length < 0xFFFF) adpcm.length++;
            return;
        case 0xB:
            adpcm.dma_control = val;
            if (val & 0x03) {
                if (scsi.phase == DATA_IN) {
                    adpcm_set_half_reached(false);
                    if (exec_slot.active) { sys->scheduler.delete_if_exist(exec_slot.id); exec_slot.active = 0; }
                    if (read_slot.active) { sys->scheduler.delete_if_exist(read_slot.id); read_slot.active = 0; }
                    schedule_dma(0);
                } else if (adpcm.length < 0x8000) {
                    adpcm_set_half_reached(true);
                }
            } else {
                if (dma_slot.active) {
                    sys->scheduler.delete_if_exist(dma_slot.id);
                    dma_slot.active = 0;
                }
            }
            return;
        case 0xD:
            adpcm_set_control(val);
            return;
        case 0xE:
            adpcm.rate = val & 0x0F;
            adpcm.frac = 0;
            return;
        case 0xF: {
            bool was_enabled = fader.enabled;
            bool now_enabled = (val & 0x08) != 0;
            fader.reg = val;
            fader.enabled = now_enabled;
            fader.adpcm_target = (val & 0x02) != 0;
            fader.fast = (val & 0x04) != 0;
            if (now_enabled && !was_enabled)
                fader.start_clock = static_cast<u64>(sys->scheduler.current_time());
            return;
        }

        default: return;
    }
}

void core::scsi_ack() {
    if (scsi.phase == DATA_IN && scsi.REQ) {
        scsi.REQ = false;
        update_scsi_irq();
        scsi.buf_pos++;
        if (scsi.buf_pos >= scsi.buf_len)
            advance_phase();
        else {
            scsi.REQ = true;
            update_scsi_irq();
        }
    } else if (scsi.phase == STATUS) {
        advance_phase();
    } else if (scsi.phase == MESSAGE_IN) {
        advance_phase();
    }
}

void core::advance_phase() {
    switch (scsi.phase) {
        case COMMAND:
            scsi.phase = STATUS;
            scsi.CD = true;
            scsi.IO = true;
            scsi.MSG = false;
            scsi.REQ = true;
            break;
        case DATA_IN:
            scsi.phase = STATUS;
            scsi.CD = true;
            scsi.IO = true;
            scsi.MSG = false;
            scsi.REQ = true;
            break;
        case STATUS:
            scsi.phase = MESSAGE_IN;
            scsi.CD = true;
            scsi.IO = true;
            scsi.MSG = true;
            scsi.REQ = true;
            break;
        case MESSAGE_IN:
            scsi.phase = BUS_FREE;
            scsi.BSY = scsi.REQ = scsi.MSG = scsi.CD = scsi.IO = false;
            break;
        default:
            break;
    }
    update_scsi_irq();
    update_irq();
}

void core::begin_response(const u8 *data, u32 len) {
    scsi.phase = DATA_IN;
    scsi.IO = true;
    scsi.CD = false;
    scsi.MSG = false;
    scsi.REQ = true;
    scsi.buf_len = len;
    scsi.buf_pos = 0;
    scsi.status_byte = 0x00;
    if (data)
        memcpy(scsi.buf, data, len);
    update_scsi_irq();
    update_irq();
}

void core::sch_exec(u64 key, u64 clock) {
    exec_slot.active = 0;
    if (scsi.phase == DATA_IN) {
        if (scsi.remaining_sectors > 0 && --scsi.remaining_sectors > 0) {
            schedule_read(static_cast<u64>(sys->scheduler.current_time()));
        } else {
            advance_phase();
        }
        return;
    }
    if (scsi.phase == STATUS) {
        scsi.phase = MESSAGE_IN;
        scsi.MSG = true;
        scsi.REQ = true;
        update_irq();
        return;
    }
    if (scsi.phase == MESSAGE_IN) {
        scsi.phase = BUS_FREE;
        scsi.BSY = scsi.REQ = scsi.MSG = scsi.CD = scsi.IO = false;
        update_scsi_irq();
        update_irq();
        return;
    }
    exec_scsi_cmd();
}

u32 core::get_audio_lba_pos(const u8 *cmd) const {
    u8 mode = cmd[9] & 0xC0;
    if (mode == 0x00) {
        u32 logical_lba = (static_cast<u32>(cmd[3]) << 16) | (static_cast<u32>(cmd[4]) << 8) | cmd[5];
        return CD::logical_to_abs_lba(logical_lba);
    } else if (mode == 0x40) {
        u8 mm = bcd_dec(cmd[2]);
        u8 ss = bcd_dec(cmd[3]);
        u8 ff = bcd_dec(cmd[4]);
        return (static_cast<u32>(mm) * 60u + ss) * 75u + ff;
    } else {
        u8 track = bcd_dec(cmd[2]);
        if (disc.valid && track >= 1 && static_cast<u32>(track) <= disc.num_tracks)
            return disc.tracks[track - 1].idx1_lba;
        return 0;
    }
}

void core::exec_scsi_cmd() {
    const u8 *cmd = scsi.cmd;
    switch (cmd[0]) {

        case 0x00: {
            scsi.status_byte = drive.inserted ? 0x00 : 0x02;
            advance_phase();
            return;
        }

        case 0x08: {
            u32 logical_lba = (static_cast<u32>(cmd[1] & 0x1F) << 16) | (static_cast<u32>(cmd[2]) << 8) | cmd[3];
            u32 abs_lba = CD::logical_to_abs_lba(logical_lba);
            u32 count = cmd[4] ? cmd[4] : 256;
            cdda.playing = false;
            scsi.remaining_sectors = count;
            seek.target_LBA = abs_lba;
            seek.needs_seek = (abs_lba != read_LBA);
            schedule_read(sys->clock.master_cycles +
                          (seek.needs_seek ? seek_cycles() : 5000));
            scsi.status_byte = 0x00;
            return;
        }

        case 0xD3: {
            u8 status;
            if (!drive.inserted) status = 0x04;
            else if (cdda.playing) status = 0x00;
            else status = 0x01;
            begin_response(&status, 1);
            return;
        }

        case 0xD8: {
            u32 lba = get_audio_lba_pos(cmd);
            cdda.LBA = lba;
            cdda.loop_start_lba = lba;
            cdda.loop_end_lba = 0xFFFFFFFFu;
            cdda.end_behavior = 0;
            cdda.sample_idx = 0;
            cdda.playing = true;
            cdda.muted = false;
            scsi.status_byte = 0x00;
            advance_phase();
            return;
        }

        case 0xD9: {
            u32 end_lba = get_audio_lba_pos(cmd);
            u8 mode = cmd[1];
            if (mode == 0) {
                cdda.playing = false;
            } else {
                cdda.loop_end_lba = end_lba;
                cdda.end_behavior = mode;
            }
            scsi.status_byte = 0x00;
            advance_phase();
            return;
        }

        case 0xDA: {
            cdda.playing = false;
            scsi.status_byte = 0x00;
            advance_phase();
            return;
        }

        case 0xDD: {
            u8 resp[10]{};
            resp[0] = cdda.playing ? 0x00 : 0x01;
            u32 abs_mm = 0, abs_ss = 0, abs_ff = 0;
            u32 t_num = 0, t_idx = 0, t_mm = 0, t_ss = 0, t_ff = 0;
            bool is_data = false;
            if (disc.valid) {
                disc.global_CD_time(cdda.LBA, abs_mm, abs_ss, abs_ff);
                disc.track_CD_time(cdda.LBA, t_num, t_idx, t_mm, t_ss, t_ff);
                is_data = (t_num < disc.num_tracks && disc.tracks[t_num].mode != CDMODE_AUDIO);
            }
            resp[1] = 0x01u | (is_data ? 0x40u : 0x00u);
            resp[2] = bcd_enc(static_cast<u8>(t_num + 1));
            resp[3] = 1;
            resp[4] = bcd_enc(static_cast<u8>(t_mm));
            resp[5] = bcd_enc(static_cast<u8>(t_ss));
            resp[6] = bcd_enc(static_cast<u8>(t_ff));
            resp[7] = bcd_enc(static_cast<u8>(abs_mm));
            resp[8] = bcd_enc(static_cast<u8>(abs_ss));
            resp[9] = bcd_enc(static_cast<u8>(abs_ff));
            begin_response(resp, 10);
            return;
        }

        case 0xDE: {
            if (!disc.valid || disc.num_tracks == 0) {
                scsi.status_byte = 0x02;
                advance_phase();
                return;
            }
            u8 sub = scsi.cmd[1];
            u8 resp[4] = {};
            switch (sub) {
                case 0:
                    resp[0] = bcd_enc(1);
                    resp[1] = bcd_enc(static_cast<u8>(disc.num_tracks));
                    resp[2] = 0x00;
                    resp[3] = 0x00;
                    break;
                case 1: {
                    u8 mm, ss, ff;
                    CD::lba_to_msf(disc.end_of_last_track, mm, ss, ff);
                    resp[0] = bcd_enc(mm);
                    resp[1] = bcd_enc(ss);
                    resp[2] = bcd_enc(ff);
                    resp[3] = 0x00;
                    break;
                }
                case 2: {
                    u8 track_n = bcd_dec(scsi.cmd[2]);
                    if (track_n == 0 || track_n > disc.num_tracks) track_n = 1;
                    auto &trk = disc.tracks[track_n - 1];
                    bool is_audio = (trk.mode == CDMODE_AUDIO);
                    u8 mm, ss, ff;
                    CD::lba_to_msf(trk.idx1_lba, mm, ss, ff);
                    resp[0] = bcd_enc(mm);
                    resp[1] = bcd_enc(ss);
                    resp[2] = bcd_enc(ff);
                    resp[3] = is_audio ? 0x00 : 0x04;
                    break;
                }
                default:
                    break;
            }
            begin_response(resp, 4);
            return;
        }

        default:
            printf("\n[TG16CD] Unhandled SCSI cmd %02x", cmd[0]);
            scsi.status_byte = 0x02;
            advance_phase();
            return;
    }
}

void core::sch_read(u64 key, u64 clock) {
    read_slot.active = 0;
    read_sector();
}

void core::read_sector() {
    if (seek.needs_seek) {
        read_LBA = seek.target_LBA;
        seek.needs_seek = false;
    }

    if (!disc.valid) {
        return;
    }

    u8 *raw = disc.ptr_to_data_abs(read_LBA);
    if (!raw) {
        return;
    }

    u8 *data = raw + 16;

    read_LBA++;
    begin_response(data, 2048);
}

void core::next_sector() {
    cdda.LBA++;
    if (cdda.LBA >= 333000u) { cdda.LBA = 150; return; }

    if (cdda.loop_end_lba != 0xFFFFFFFFu && cdda.LBA >= cdda.loop_end_lba) {
        switch (cdda.end_behavior) {
            case 1:
                cdda.LBA = cdda.loop_start_lba;
                cdda.sample_idx = 0;
                break;
            case 2:
            case 3:
                cdda.playing = false;
                break;
            default:
                cdda.playing = false;
                break;
        }
    }
}

i64 core::seek_cycles() const {
    return static_cast<i64>(ONEFRAME * 20);
}

void core::schedule_read(u64 clock) {
    u64 tm = clock + CYCLES_PER_SECTOR_1X;
    read_slot.id = sys->scheduler.only_add_abs(
        static_cast<i64>(tm), 0, this, &sch_read_tramp, &read_slot.active);
}

void core::schedule_exec(u64 delay) {
    if (exec_slot.active)
        sys->scheduler.delete_if_exist(exec_slot.id);
    u64 tm = static_cast<u64>(sys->scheduler.current_time()) + delay;
    exec_slot.id = sys->scheduler.only_add_abs(
        static_cast<i64>(tm), 0, this, &sch_exec_tramp, &exec_slot.active);
}

void core::sch_ack(u64 key, u64 clock) {
    ack_slot.active = 0;
    if (scsi.phase != COMMAND && scsi.phase != DATA_IN) return;
    scsi.REQ = true;
    update_scsi_irq();
    update_irq();
}

void core::schedule_ack(u64 delay) {
    if (ack_slot.active)
        sys->scheduler.delete_if_exist(ack_slot.id);
    u64 tm = static_cast<u64>(sys->scheduler.current_time()) + delay;
    ack_slot.id = sys->scheduler.only_add_abs(
        static_cast<i64>(tm), 0, this, &sch_ack_tramp, &ack_slot.active);
}


static constexpr u64 DMA_CYCLES_PER_BYTE = 30ULL;

void core::sch_dma(u64 key, u64 clock) {
    dma_slot.active = 0;
    if (scsi.phase != DATA_IN) return;

    while (scsi.buf_pos < scsi.buf_len) {
        adpcm_set_half_reached(adpcm.length < 0x8000);
        u8 byte = scsi.buf[scsi.buf_pos++];
        adpcm.ram[adpcm.write_addr] = byte;
        adpcm.write_addr = (adpcm.write_addr + 1) & 0xFFFF;
        if (!(adpcm.control & 0x10) && adpcm.length < 0xFFFF) adpcm.length++;
    }

    if (scsi.remaining_sectors > 1) {
        scsi.remaining_sectors--;
        read_sector();
        schedule_dma(2048ULL * DMA_CYCLES_PER_BYTE);
    } else {
        scsi.remaining_sectors = 0;
        scsi.status_byte = 0x00;
        schedule_exec(1000);
    }
}

void core::schedule_dma(u64 delay) {
    if (dma_slot.active)
        sys->scheduler.delete_if_exist(dma_slot.id);
    u64 tm = static_cast<u64>(sys->scheduler.current_time()) + delay;
    dma_slot.id = sys->scheduler.only_add_abs(
        static_cast<i64>(tm), 0, this, &sch_dma_tramp, &dma_slot.active);
}

void core::sch_adpcm_read(u64 key, u64 clock) {
    adpcm_read_slot.active = 0;
    adpcm_finish_pending_read();
}

void core::schedule_adpcm_read(u64 clock) {
    if (adpcm_read_slot.active)
        sys->scheduler.delete_if_exist(adpcm_read_slot.id);
    adpcm_read_slot.id = sys->scheduler.only_add_abs(
        static_cast<i64>(clock), 0, this, &sch_adpcm_read_tramp, &adpcm_read_slot.active);
}

void core::reschedule_pending_events() {
    if (adpcm.read_pending)
        schedule_adpcm_read(adpcm.read_done_clock);
}

void core::insert_disc(multi_file_set &mfs) {
    disc.parse_cue(mfs);
    drive.inserted = true;
    drive.open = false;
}

void core::remove_disc() {
    drive.inserted = false;
    cdda.playing = false;
}

void core::open_drive() {
    drive.open = true;
    cdda.playing = false;
}

void core::close_drive() {
    drive.open = false;
}

void core::adpcm_set_half_reached(bool value) {
    if (adpcm.half_reached == value) return;
    adpcm.half_reached = value;
    if (value) set_irq_source(0x04); else clear_irq_source(0x04);
}

void core::adpcm_set_end_reached(bool value) {
    if (adpcm.end_reached == value) return;
    adpcm.end_reached = value;
    if (value) set_irq_source(0x08); else clear_irq_source(0x08);
}

void core::adpcm_finish_pending_read() {
    if (!adpcm.read_pending) return;

    adpcm.read_pending = false;
    adpcm.read_done_clock = 0;
    adpcm.read_buffer = adpcm.ram[adpcm.read_addr];
    adpcm.read_addr = (adpcm.read_addr + 1) & 0xFFFF;

    adpcm_set_half_reached(adpcm.length < 0x8000);
    if (!(adpcm.control & 0x10)) {
        if (adpcm.length) {
            adpcm.length = (adpcm.length - 1) & 0xFFFF;
        } else {
            adpcm_set_half_reached(false);
            adpcm_set_end_reached(true);
            if (adpcm.control & 0x40) {
                adpcm.playing = false;
                adpcm.last_sample = 0.0f;
            }
        }
    }
}

void core::adpcm_complete_read_if_due(u64 clock) {
    if (!adpcm.read_pending || clock < adpcm.read_done_clock) return;
    if (adpcm_read_slot.active) {
        sys->scheduler.delete_if_exist(adpcm_read_slot.id);
        adpcm_read_slot.active = 0;
    }
    adpcm_finish_pending_read();
}

void core::adpcm_set_control(u8 value) {
    if ((value & 0x02) && !(adpcm.control & 0x02)) {
        adpcm.write_addr = adpcm.address_port - ((value & 0x01) ? 0 : 1);
    }
    if ((value & 0x08) && !(adpcm.control & 0x08)) {
        adpcm.read_addr = adpcm.address_port - ((value & 0x04) ? 0 : 1);
        if (adpcm_read_slot.active) {
            sys->scheduler.delete_if_exist(adpcm_read_slot.id);
            adpcm_read_slot.active = 0;
        }
        adpcm.read_pending = false;
        adpcm.read_done_clock = 0;
    }
    if (!adpcm.playing && (value & 0x20) && !(adpcm.control & 0x20)) {
        adpcm.play_request = true;
    }

    adpcm.control = value;

    if (value & 0x10) {
        adpcm.length = adpcm.address_port;
        adpcm_set_end_reached(false);
    }
    if (value & 0x80) {
        if (adpcm_read_slot.active) {
            sys->scheduler.delete_if_exist(adpcm_read_slot.id);
            adpcm_read_slot.active = 0;
        }
        adpcm.read_pending = false;
        adpcm.read_done_clock = 0;
        adpcm.playing = false;
        adpcm.play_request = false;
        adpcm.nibble = false;
        adpcm.current_output = 2048;
        adpcm.magnitude = 0;
        adpcm.frac = 0;
        adpcm_set_half_reached(false);
        adpcm_set_end_reached(false);
        adpcm.length = 0;
    }
}

void core::tick_adpcm() {
    u32 freq = 32000u / (16u - (adpcm.rate & 0xFu));
    adpcm.frac += freq;
    bool decode_this_tick = (adpcm.frac >= 128000u);
    if (decode_this_tick) adpcm.frac -= 128000u;

    if (decode_this_tick && (adpcm.playing || adpcm.play_request)) {
        if (adpcm.play_request && !(adpcm.control & 0x80)) {
            adpcm.play_request = false;
            adpcm.playing = true;
            adpcm.current_output = 2048;
            adpcm.magnitude = 0;
        }

        if (adpcm.playing) {
            bool play_bit_clear = !(adpcm.control & 0x20);
            if (play_bit_clear) {
                adpcm.playing = false;
                adpcm.last_sample = 0.0f;
            } else if (adpcm.length == 0) {
                adpcm_set_half_reached(false);
                adpcm_set_end_reached(true);
                if (adpcm.control & 0x40) {
                    adpcm.playing = false;
                    adpcm.last_sample = 0.0f;
                }
            } else {
                u8 data;
                if (!adpcm.nibble) {
                    adpcm.nibble = true;
                    data = (adpcm.ram[adpcm.read_addr] >> 4) & 0x0F;
                } else {
                    adpcm.nibble = false;
                    data = adpcm.ram[adpcm.read_addr] & 0x0F;
                    adpcm.read_addr++;
                    adpcm.length = (adpcm.length - 1) & 0xFFFF;
                }

                u8 value = data & 0x07;
                i32 sign = (data & 0x08) ? -1 : 1;
                i32 adj = adpcm_step_size[(static_cast<u32>(adpcm.magnitude) << 3) | value] * sign;
                adpcm.current_output = (adpcm.current_output + adj) & 0xFFF;

                i32 new_mag = static_cast<i32>(adpcm.magnitude) + adpcm_step_factor[value];
                if (new_mag < 0) new_mag = 0;
                if (new_mag > 48) new_mag = 48;
                adpcm.magnitude = static_cast<u8>(new_mag);

                adpcm_set_half_reached(adpcm.length < 0x8000);
                if (adpcm.length == 0) adpcm_set_end_reached(true);

                i16 out = static_cast<i16>((adpcm.current_output - 2048) * 10);
                float s = static_cast<float>(out) / 32768.0f;
                if (s > 1.0f) s = 1.0f;
                if (s < -1.0f) s = -1.0f;
                adpcm.last_sample = s;
            }
        }
    }

    if (adpcm_ring) adpcm_ring->push(adpcm.last_sample, adpcm.last_sample);
}

void core::get_cdda(i16 &l, i16 &r) {
    if (!cdda.playing || cdda.muted || !disc.valid) {
        l = r = 0;
        return;
    }

    u8 *raw = disc.ptr_to_data_abs(cdda.LBA);
    if (!raw) {
        l = r = 0;
        return;
    }

    auto *samples = reinterpret_cast<i16 *>(raw);
    l = samples[cdda.sample_idx * 2 + 0];
    r = samples[cdda.sample_idx * 2 + 1];
    cdda.last_l = l;
    cdda.last_r = r;

    if (fader.enabled && !fader.adpcm_target) {
        f64 elapsed = static_cast<f64>(static_cast<u64>(sys->scheduler.current_time()) - fader.start_clock)
                         / 21477272.0;
        f64 fade_time = fader.fast ? 2.5 : 6.0;
        float fvol = static_cast<float>(1.0 - elapsed / fade_time);
        if (fvol < 0.0f) fvol = 0.0f;
        l = static_cast<i16>(static_cast<i32>(l) * fvol);
        r = static_cast<i16>(static_cast<i32>(r) * fvol);
    }

    cdda.sample_idx++;
    if (cdda.sample_idx >= 588) {
        cdda.sample_idx = 0;
        next_sector();
    }
}

void core::get_audio(i16 &left, i16 &right) {
    get_cdda(left, right);
}

u8 core::ac_read(u32 addr, u8 old) {
    u32 off = (addr & 0xFF) - 0x10;
    u32 ch = off >> 3;
    u32 reg = off & 7;
    if (ch >= 4) return old;

    ACChannel &c = ac[ch];
    switch (reg) {
        case 0: return c.addr & 0xFF;
        case 1: return (c.addr >> 8) & 0xFF;
        case 2: return (c.addr >> 16) & 0x1F;
        case 3: return c.offset;
        case 4: return c.ctrl;
        case 5: {
            u8 v = sys->ac_ram[c.eff()];
            if (c.ctrl & 0x01) c.increment();
            return v;
        }
        default: return old;
    }
}

void core::ac_write(u32 addr, u8 val) {
    u32 off = (addr & 0xFF) - 0x10;
    u32 ch = off >> 3;
    u32 reg = off & 7;
    if (ch >= 4) return;

    ACChannel &c = ac[ch];
    switch (reg) {
        case 0: c.addr = (c.addr & 0x1FFF00) | val; break;
        case 1: c.addr = (c.addr & 0x1F00FF) | (static_cast<u32>(val) << 8); break;
        case 2: c.addr = (c.addr & 0x00FFFF) | ((static_cast<u32>(val) & 0x1F) << 16); break;
        case 3: c.offset = val; break;
        case 4: c.ctrl = val; break;
        case 5:
            sys->ac_ram[c.eff()] = val;
            if (c.ctrl & 0x02) c.increment();
            break;
        default: break;
    }
}

}
