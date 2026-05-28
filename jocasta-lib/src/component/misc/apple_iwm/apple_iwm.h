#pragma once

#include <cassert>
#include <cstdio>
#include <vector>

#include "helpers/int.h"
#include "helpers/debug.h"
#include "helpers/cvec.h"
#include "helpers/physical_io.h"

namespace APPLE_IWM {

enum kind {
    mac,
    apple2
};

enum iwm_modes {
    M_IDLE,
    M_MOTOR_STOP_DELAY,
    M_ACTIVE,
    M_READ,
    M_WRITE
};

template<kind model, typename Bus, typename Disc> struct FDD {
    explicit FDD(Bus *bus_in, u32 num_in);
    void calculate_ticks_per_flux();
    bool floppy_inserted() const;
    void write_motor_on(bool onoff);
    void setup_track();
    void do_step();
    u8 read_reg(u8 which);
    void write_reg(u8 which);
    Disc *disc{};
    bool clock();
    void set_pwm_dutycycle(i64 to);

    Bus *bus;
    u32 num;

    u32 RPM{};
    u32 pwm_val{};

    struct HEAD {
        u32 track_num{};
        u8 flux{};
        struct {
            bool is_stepping{};
            u64 start{};
            u64 end{};
        } stepping{};
    } head{};
    u8 step_direction{};
    bool mfm{};

    // Apple II Disk II: 4-phase stepper state
    u8  phases{};         // bitmask of energised phases (bits 0-3)
    u32 quarter_track{};  // head position in quarter-tracks; track = quarter_track/2
    long double next_flux_tick{};
    long double ticks_per_flux{};
    u64 track_pos;

    u64 last_RPM_change_time{};
    float last_RPM_change_pos{};

    cvec_ptr<physical_io_device> ptr{};

    bool motor_on{};
    bool disk_switched{};

    physical_io_device *device{};

    bool connected{};

    u64 input_clock_cnt{};

    struct {
        i64 avg_sum{};
        i64 avg_count{};
        i64 dutycycle{};
    } pwm;

private:
    [[nodiscard]] u64 cycles_per_second() const;
};

template<kind model, typename Bus, typename Disc> struct IWM {
    IWM(Bus *parent, u32 num_drives_in);
    u32 num_drives{};
    void reset();
    void clock();
    void update_pwm(u8 val);

    u16 do_read(u32 addr, u16 mask, u16 old, bool has_effect);
    void do_write(u32 addr, u16 mask, u16 val);
    void write_HEADSEL(u8 what);

    [[nodiscard]] bool floppy_write_protected() const;
    [[nodiscard]] bool floppy_inserted() const;
    Bus *bus;
    FDD<model, Bus, Disc> drive[2];
    std::vector<Disc> my_disks{};
    FDD<model, Bus, Disc> *selected_drive = &drive[0];

    struct {
        u8 HEADSEL{}, CA0{}, CA1{}, CA2{}, LSTRB{}, ENABLE{}, SELECT{}, Q6{}, Q7{};
    } lines{};

    struct {
        i32 pos{};
        i32 buffer{-1};
    } write{};

    struct {
        u8 data{}, shifter{};
        union {
            struct {
                u8 mode : 5;
                u8 enable : 1;
                u8 mz : 1;
                u8 sense : 1;
            };
            u8 u{};
        } status{};

        union {
            struct {
                u8 latch : 1;
                u8 sync : 1;
                u8 onesec : 1;
                u8 fast : 1;
                u8 speed : 1;
                u8 test : 1;
                u8 mzreset : 1;
                u8 reserved: 1;
            };
            u8 u{};
        } mode{};
    } regs{};

private:
    void access(u32 addr);
    u8 get_drive_reg() const;
};

template<kind model, typename Bus, typename Disc>
FDD<model, Bus, Disc>::FDD(Bus *bus_in, u32 num_in) : bus(bus_in), num(num_in) {
}

template<kind model, typename Bus, typename Disc>
u64 FDD<model, Bus, Disc>::cycles_per_second() const
{
    if constexpr (model == mac) return bus->clock.timing.cycles_per_second;
    else return 14318180;
}

template<kind model, typename Bus, typename Disc>
void FDD<model, Bus, Disc>::setup_track()
{
    auto &track = disc->disc.tracks[head.track_num];
    last_RPM_change_time = bus->clock.master_cycles;
    next_flux_tick = bus->clock.master_cycles - 1;
    track_pos = 0;
}

template<kind model, typename Bus, typename Disc>
void FDD<model, Bus, Disc>::calculate_ticks_per_flux()
{
    if (disc == nullptr)
        return;
    if (RPM == 0) { ticks_per_flux = 10000000000; }
    auto &track = disc->disc.tracks[head.track_num];
    long double num_bits = track.encoded_data.num_bits;
    long double bits_per_second = (num_bits * RPM) / 60.0l;
    ticks_per_flux = cycles_per_second() / bits_per_second;
}

template<kind model, typename Bus, typename Disc>
void FDD<model, Bus, Disc>::write_motor_on(bool onoff)
{
    if (disc == nullptr) {
        return;
    }

    if (motor_on == onoff) {
        return;
    }

    if (onoff != motor_on) {
        if (onoff) {
            setup_track();
            if constexpr (model == apple2) {
                RPM = 300;
                calculate_ticks_per_flux();
            }
        } else {

        }
    }
    motor_on = onoff;
    if (ptr.vec) ptr.get().disc_drive.motor_on = onoff;
}

template<kind model, typename Bus, typename Disc>
void FDD<model, Bus, Disc>::do_step()
{
    if (step_direction == 0) {
        if (head.track_num > 0) {
            head.track_num -= 1;
            printf("\nTRACK CHANGE DOWN TO %d", head.track_num);
        }
    }
    else {
        if (disc) {
            if ((head.track_num + 1) < disc->disc.tracks.size()) {
                head.track_num += 1;
                printf("\nTRACK CHANGE UP TO %d", head.track_num);
            }
        }
    }
    printf("\nHead stepped to track %d @%lld", head.track_num, bus->clock.master_cycles);

    head.stepping.start = bus->clock.master_cycles;
    head.stepping.is_stepping = true;
    head.stepping.end = bus->clock.master_cycles + cycles_per_second() / 33;
    setup_track();
}

template<kind model, typename Bus, typename Disc>
bool FDD<model, Bus, Disc>::floppy_inserted() const {
    return !!disc;
}

template<kind model, typename Bus, typename Disc>
void FDD<model, Bus, Disc>::set_pwm_dutycycle(i64 to) {
    if (to != pwm.dutycycle) {
        constexpr i64 DUTY_T0 = 9;
        constexpr i64 SPEED_T0 = (380 + 304) / 2;
        constexpr i64 DUTY_T79 = 91;
        constexpr i64 SPEED_T79 = (625 + 480) / 2;
        if (to < DUTY_T0) {
            RPM = 0;
        }
        else {
            RPM = ((to - DUTY_T0) * (SPEED_T79 * 100 + SPEED_T0 * 100)
                / (DUTY_T79 - DUTY_T0))
                / 100
                + SPEED_T0;
        }
        printf("\nNEW RPM: %d", RPM);
        calculate_ticks_per_flux();
    }
    pwm.dutycycle = to;
}

template<kind model, typename Bus, typename Disc>
u8 FDD<model, Bus, Disc>::read_reg(u8 which) {
    u32 v = 0;

    switch (which) {
        case 0b0000:
            v = step_direction;
            return v;
        case 0b0001:
            v = floppy_inserted() ^ 1;
            return v;
        case 0b0010:
            v = head.stepping.is_stepping == false;
            return v;
        case 0b0011:
            if (!disc) return 1;
            v = !disc->write_protect;
            return v;
        case 0b0100:
            v = motor_on == 0;
            printf("\nSTATUSREG.2: motor power on: %d", v);
            return v;
        case 0b0101:
            v = head.track_num != 0;
            return v;
        case 0b0110:
            v = disk_switched;
            return v;
        case 0b0111: {
            if (!motor_on) return 0;
            if (!disc) return 0;
            if (RPM == 0) return 0;
            u64 edges_per_minute = RPM * 120;
            u64 ticks_per_edge = (8000000 * 60) / edges_per_minute;
            u8 r = (bus->clock.master_cycles/ticks_per_edge) & 1;
            static u8 last_returned = 0;
            return r;
            }
        case 0b1000:
            return head.flux;
        case 0b1001:
            if (motor_on) return 1;
            return 1;
        case 0b1010:
            return 0;
        case 0b1011:
            return mfm;
            return 0;
        case 0b1100:
            return 0;
        case 0b1101:
            return 0;
        case 0b1110:
            return 0;
        case 0b1111:
            return 0;
    }
    NOGOHERE;
    return 0;
}

template<kind model, typename Bus, typename Disc>
void FDD<model, Bus, Disc>::write_reg(u8 which) {
    enum regwrites {
        TRACKUP = 0b0000,
        TRACKDN = 0b1000,
        TRACKSTEP = 0b0010,
        MFMMODE = 0b0011,
        MOTORON = 0b0100,
        CLRSWITCHED = 0b1001,
        GCRMODE = 0b1011,
        MOTOROFF = 0b1100,
        EJECT = 0b1110,
    } whichrw = static_cast<regwrites>(which);
    printf("\nWRITE DRIVE REG %d", which);

    switch (whichrw) {
        case TRACKUP:
            step_direction = 1;
            return;
        case TRACKDN:
            step_direction = -1;
            return;
        case MOTORON:
            write_motor_on(true);
            return;
        case MOTOROFF:
            write_motor_on(false);
            return;
        case TRACKSTEP:
            do_step();
            return;
        case MFMMODE:
            return;
        case GCRMODE:
            return;
        case CLRSWITCHED:
            return;
        case EJECT:
            printf("\nEJECT? NAH!");
            return;
        default:
            printf("\n unknown reg:%d   cyc:%lld", which, bus->clock.master_cycles);
            break;
    }
}

template<kind model, typename Bus, typename Disc>
bool FDD<model, Bus, Disc>::clock() {
    if (!motor_on) return false;
    if (!disc) return false;
    u64 tc = bus->clock.master_cycles;
    if (head.stepping.is_stepping) {
        if (tc >= head.stepping.end) {
            head.stepping.is_stepping = false;
            setup_track();
            //printf("\nHead step finished @%lld", bus->clock.master_cycles);
        }
        else return false;
    }
    if (RPM != 0) {
        while (next_flux_tick <= tc) {
            next_flux_tick += ticks_per_flux;
            auto &track = disc->disc.tracks[head.track_num];
            head.flux = track.encoded_data.data[track_pos];
            track_pos = (track_pos + 1) % track.encoded_data.num_bits;
            return true;
        }
    }
    return false;
}

template<kind model, typename Bus, typename Disc>
IWM<model, Bus, Disc>::IWM(Bus* parent, u32 num_drives_in) :
    num_drives(num_drives_in),
    bus(parent),
    drive{FDD<model, Bus, Disc>(parent, 0), FDD<model, Bus, Disc>(parent, 1)}
{
    my_disks.reserve(10);
    Disc &f = my_disks.emplace_back();
    f.write_protect = false;
    drive[0].disc = &f;
}

template<kind model, typename Bus, typename Disc>
void IWM<model, Bus, Disc>::update_pwm(u8 val) {
    static constexpr u8 VALUE_TO_LEN[64] = {
        0, 1, 59, 2, 60, 40, 54, 3, 61, 32, 49, 41, 55, 19, 35, 4, 62, 52, 30, 33, 50, 12, 14,
        42, 56, 16, 27, 20, 36, 23, 44, 5, 63, 58, 39, 53, 31, 48, 18, 34, 51, 29, 11, 13, 15,
        26, 22, 43, 57, 38, 47, 17, 28, 10, 25, 21, 37, 46, 9, 24, 45, 8, 7, 6
    };
    for (auto &drv : drive) {
        drv.pwm.avg_sum += static_cast<i64>(VALUE_TO_LEN[val]) & 63;
        drv.pwm.avg_count++;
        if (drv.pwm.avg_count >= 100) {
            i64 idx = drv.pwm.avg_sum / (drv.pwm.avg_count / 10) - 11;
            if (idx < 0) idx = 0;
            if (idx > 399) idx = 399;
            drv.set_pwm_dutycycle((idx * 100) / 419);
            drv.pwm.avg_sum = 0;
            drv.pwm.avg_count = 0;
        }
    }
}

template<kind model, typename Bus, typename Disc>
void IWM<model, Bus, Disc>::reset() {
    for (auto & d : drive) {
        d.head.track_num = 0;
        d.quarter_track  = 0;
        d.phases         = 0;
        d.RPM            = 0;
    }
    if (drive[0].connected)
        drive[0].device = &drive[0].ptr.get();
    if (drive[1].connected)
        drive[1].device = &drive[1].ptr.get();
}

template<kind model, typename Bus, typename Disc>
u16 IWM<model, Bus, Disc>::do_read(u32 addr, u16 mask, u16 old, bool has_effect) {
    if (!has_effect) return 0xFFFF & mask;
    access(addr);
    u8 q67 = (lines.Q6 << 1) | lines.Q7;
    switch (q67) {
        case 0b00: {
            if constexpr (model == mac) {
                if (!lines.ENABLE) return 0xFF;
            }
            u8 v = regs.data;
            regs.data = 0;
            /*if (v != 0) {
                static u8 prev1 = 0, prev2 = 0;
                if (prev2 == 0xD5 && prev1 == 0xAA && v == 0x96)
                    printf("\n[IWM] ADDR PROLOGUE found  track=%d pos=%llu", selected_drive->head.track_num, (unsigned long long)selected_drive->track_pos);
                if (prev2 == 0xD5 && prev1 == 0xAA && v == 0xAD)
                    printf("\n[IWM] DATA PROLOGUE found  track=%d pos=%llu", selected_drive->head.track_num, (unsigned long long)selected_drive->track_pos);
                prev2 = prev1; prev1 = v;
            }*/
            return v; }
        case 0b10:
            regs.status.sense = selected_drive->read_reg(get_drive_reg());
            regs.status.mode = regs.mode.u & 0x1F;
            regs.status.enable = lines.ENABLE;

            regs.shifter = 0;
            return regs.status.u;
        case 0b01:
            return ((!(write.pos == 0 && write.buffer == -1) << 6) |
                ((write.buffer == -1) << 7));
        default: break;
    }
    printf("\nIWM unknown read! %04x q6:%d q7:%d", addr, lines.Q6, lines.Q7);
    return 0;
}

template<kind model, typename Bus, typename Disc>
void IWM<model, Bus, Disc>::clock() {
    if (selected_drive->clock() && ((model == apple2) || (regs.mode.latch && lines.HEADSEL == 0))) {
        regs.shifter = (regs.shifter << 1) | selected_drive->head.flux;
        if (regs.shifter & 0x80) {
            regs.data = regs.shifter;
            regs.shifter = 0;
        }
    };
}

template<kind model, typename Bus, typename Disc>
u8 IWM<model, Bus, Disc>::get_drive_reg() const {
    return lines.HEADSEL | (lines.CA0 << 1) | (lines.CA1 << 2) | (lines.CA2 << 3);
}

template<kind model, typename Bus, typename Disc>
void IWM<model, Bus, Disc>::access(u32 addr) {
    u32 reg = model == mac ? ((addr >> 9) & 15) : (addr & 15);
    if constexpr (model == apple2) {
        switch (reg) {
            // ── Phase controls (Disk II 4-phase stepper) ─────────────────────
            // Even regs 0,2,4,6 = PHASE 0-3 OFF
            // Odd  regs 1,3,5,7 = PHASE 0-3 ON
            case 0: case 2: case 4: case 6:
                selected_drive->phases &= ~(1u << (reg >> 1));
                break;
            case 1: case 3: case 5: case 7: {
                u8 p = reg >> 1;                       // phase number 0-3
                if (!(selected_drive->phases & (1u << p))) {
                    // Newly energised phase — compute stepper movement.
                    // Phase p corresponds to quarter-tracks p, p+4, p+8, …
                    // Find the nearest such QT to the current position.
                    int curr_qt = (int)selected_drive->quarter_track;
                    int delta = (int)p - (curr_qt & 3); // raw, may be ±3
                    if (delta > 2) delta -= 4;             // choose shorter path
                    if (delta < -2) delta += 4;             // delta now in {-2,-1,0,+1,+2}

                    int max_qt = selected_drive->disc ? (int)(selected_drive->disc->disc.tracks.size() * 2 - 2) : 68; // 35-track image ... QT 0..68
                    int new_qt = curr_qt + delta;
                    if (new_qt < 0)       new_qt = 0;
                    if (new_qt > max_qt)  new_qt = max_qt;

                    if (new_qt != curr_qt) {
                        selected_drive->quarter_track = (u32)new_qt;
                        u32 new_track = (u32)new_qt >> 1;
                        if (new_track != selected_drive->head.track_num) {
                            //printf("\n[IWM] Step: QT %d...%d  track %d...%d", curr_qt, new_qt, selected_drive->head.track_num, new_track);
                            selected_drive->head.track_num = new_track;
                            if (selected_drive->disc)
                                selected_drive->setup_track();
                        }
                    }
                }
                selected_drive->phases |= (1u << p);
                break;
            }
            // ─────────────────────────────────────────────────────────────────
            case 8: selected_drive->write_motor_on(false); break;
            case 9: selected_drive->write_motor_on(true); break;
            case 10: selected_drive = &drive[0]; break;
            case 11:
                if (num_drives > 1) selected_drive = &drive[1];
                break;
            case 12: lines.Q6 = 0; break;
            case 13: lines.Q6 = 1; break;
            case 14: lines.Q7 = 0; break;
            case 15: lines.Q7 = 1; break;
        }
        return;
    }
    switch (reg) {
        case 0: lines.CA0 = 0; break;
        case 1: lines.CA0 = 1; break;
        case 2: lines.CA1 = 0; break;
        case 3: lines.CA1 = 1; break;
        case 4: lines.CA2 = 0; break;
        case 5: lines.CA2 = 1; break;
        case 6: lines.LSTRB = 0; break;
        case 7: lines.LSTRB = 1; selected_drive->write_reg(get_drive_reg());break;
        case 8: lines.ENABLE = 0; break;
        case 9: lines.ENABLE = 1; break;
        case 10:
            lines.SELECT = 0;
            if constexpr (model == apple2) selected_drive = &drive[0];
            break;
        case 11:
            lines.SELECT = 1;
            if constexpr (model == apple2) {
                if (num_drives > 1) selected_drive = &drive[1];
            }
            break;
        case 12: lines.Q6 = 0; break;
        case 13: lines.Q6 = 1; break;
        case 14: lines.Q7 = 0; break;
        case 15: lines.Q7 = 1; break;
    }
}

template<kind model, typename Bus, typename Disc>
void IWM<model, Bus, Disc>::write_HEADSEL(u8 what) {
    lines.HEADSEL = what;
}

template<kind model, typename Bus, typename Disc>
void IWM<model, Bus, Disc>::do_write(u32 addr, u16 mask, u16 val) {
    access(addr);
    u8 q67e = (lines.Q6 << 2) | (lines.Q7 << 1) | lines.ENABLE;
    switch (q67e) {
        case 0b110:
            regs.mode.u = val;
            return;
        case 0b111:
            if (write.buffer == -1) {
                printf("\nDisk write with data in buffer..");
            }
            write.buffer = val;
            return;
    }
}

}
