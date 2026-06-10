#include "ay_3_8910.h"

#define REG_AFINE    0x00
#define REG_ACOARSE  0x01
#define REG_BFINE    0x02
#define REG_BCOARSE  0x03
#define REG_CFINE    0x04
#define REG_CCOARSE  0x05
#define REG_NOISEPER 0x06
#define REG_ENABLE   0x07
#define REG_AVOL     0x08
#define REG_BVOL     0x09
#define REG_CVOL     0x0a
#define REG_EAFINE   0x0b
#define REG_EACOARSE 0x0c
#define REG_EASHAPE  0x0d
#define REG_PORTA    0x0e
#define REG_PORTB    0x0f

#define REG_EBFINE   0x10
#define REG_EBCOARSE 0x11
#define REG_ECFINE   0x12
#define REG_ECCOARSE 0x13
#define REG_EBSHAPE  0x14
#define REG_ECSHAPE  0x15
#define REG_ADUTY    0x16
#define REG_BDUTY    0x17
#define REG_CDUTY    0x18
#define REG_NOISEAND 0x19
#define REG_NOISEOR  0x1a
#define REG_TEST     0x1f

namespace AY_3_8910 {
static u8 env_table_done = 0;
static u8 env_table[16][16];

static constexpr i16 AY_voltable[16] = {
        0, 64, 90, 128, 181, 256, 362, 512,
        724, 1024, 1448, 2048, 2896, 4096, 5793, 8191
};

CHIP::CHIP(variants variant_in) : variant(variant_in) {
    init_env_table();
    reset();
}

void CHIP::reset() {
    divider_16 = 0;
    divider_256 = 0;
    io_reg = 0;
    ext_enable = true;
    io_ports.enable_a = io_ports.enable_b = 0;
    io_ports.output_latch_a = io_ports.output_latch_b = 0;
    io_ports.input_a = io_ports.input_b = 0xFF;
    noise.period = 0;
    noise.counter = 0;
    noise.polarity = 1;
    noise.lfsr = 1;
    noise.flip = 0;
    env.kind = ENV_none;
    env.period = 0;
    env.counter = 15;
    env.count_up = 0;
    env.mirror = 0;
    env.e_out = 15;
    env.period_counter = 1;
    env.hold = env.alternate = env.attack = env.econtinue = 0;
    for (auto &s : sw) {
        s.freq = 0;
        s.amplitude = 0;
        s.amplitude_mode = 0;
        s.enable = 1;
        s.enable_noise = 1;
        s.polarity = 1;
        s.counter = 0;
        s.ext_enable = true;
    }
}

void CHIP::select(u8 addr)
{
    io_reg = addr & 0x0F;
}

void CHIP::write_data(u8 val)
{
    write(io_reg, val);
}

u8 CHIP::read_data()
{
    return read(io_reg);
}

void CHIP::write(u8 addr, u8 val) {
    addr &= 0x0F;
    switch(addr) {
        case REG_AFINE:
            sw[0].freq = (sw[0].freq & 0x0F00) | val;
            return;
        case REG_ACOARSE:
            sw[0].freq = (sw[0].freq & 0xFF) | ((val & 0x0F) << 8);
            return;
        case REG_BFINE:
            sw[1].freq = (sw[1].freq & 0x0F00) | val;
            return;
        case REG_BCOARSE:
            sw[1].freq = (sw[1].freq & 0xFF) | ((val & 0x0F) << 8);
            return;
        case REG_CFINE:
            sw[2].freq = (sw[2].freq & 0x0F00) | val;
            return;
        case REG_CCOARSE:
            sw[2].freq = (sw[2].freq & 0xFF) | ((val & 0x0F) << 8);
            return;
        case REG_ENABLE: {
            u32 old_a = io_ports.enable_a;
            u32 old_b = io_ports.enable_b;
            sw[0].enable = (val) & 1;
            sw[1].enable = (val >> 1) & 1;
            sw[2].enable = (val >> 2) & 1;
            sw[0].enable_noise = (val >> 3) & 1;
            sw[1].enable_noise = (val >> 4) & 1;
            sw[2].enable_noise = (val >> 5) & 1;
            io_ports.enable_a = (val >> 6) & 1;
            io_ports.enable_b = (val >> 7) & 1;
            if (!old_a && io_ports.enable_a && io_ports.write_a) io_ports.write_a(io_ports.write_a_ptr, io_ports.output_latch_a);
            if (!old_b && io_ports.enable_b && io_ports.write_b) io_ports.write_b(io_ports.write_b_ptr, io_ports.output_latch_b);
            return;
        }
        case REG_NOISEPER:
            noise.period = val & 0x1F;
            return;
        case REG_AVOL:
            sw[0].amplitude = val & 15;
            sw[0].amplitude_mode = (val >> 4) & 1;
            return;
        case REG_BVOL:
            sw[1].amplitude = val & 15;
            sw[1].amplitude_mode = (val >> 4) & 1;
            return;
        case REG_CVOL:
            sw[2].amplitude = val & 15;
            sw[2].amplitude_mode = (val >> 4) & 1;
            return;
        case REG_EACOARSE:
            env.period = (env.period & 0xFF) | (val << 8);
            return;
        case REG_EAFINE:
            env.period = (env.period & 0xFF00) | val;
            return;
        case REG_EASHAPE:
            env.hold = (val) & 1;
            env.alternate = (val >> 1) & 1;
            env.attack = (val >> 2) & 1;
            env.econtinue = (val >> 3) & 1;
            reset_envelope();
            return;
        case REG_PORTA:
            io_ports.output_latch_a = val;
            if (io_ports.enable_a && io_ports.write_a) io_ports.write_a(io_ports.write_a_ptr, val);
            return;
        case REG_PORTB:
            io_ports.output_latch_b = val;
            if (io_ports.enable_b && io_ports.write_b) io_ports.write_b(io_ports.write_b_ptr, val);
            return;
    }
}

void SW::tick() {
    if (counter > 0) counter--;
    if (counter == 0) {
        counter = freq ? freq : 1;
        polarity ^= 1;
    }
}

void NOISE::tick() {
    if (counter > 0) counter--;
    if (counter == 0) {
        counter = period ? period : 1;
        flip ^= 1;
        if (flip) {
            polarity = (lfsr & 1) == 0;
            lfsr = ((((lfsr >> 0) ^ (lfsr >> 3)) & 1) << 16) | (lfsr >> 1);
            lfsr &= 0x1FFFF;
        }
    }
}

void CHIP::cycle() {
    u32 cdc = divider_16;
    divider_16 = (divider_16 + 1) & 15;

    if (cdc == 0) cycle_div_16();
}

void CHIP::cycle_div_16() {
    u32 d256 = divider_256;
    divider_256 = (divider_256 + 1) & 15;
    if (d256 == 0) {
        env.tick();
    }

    for (auto &s : sw) s.tick();

    noise.tick();
}

void ENV::tick() {
    if (mirror) return;
    if (period_counter > 0) period_counter--;
    if (period_counter > 0) return;
    period_counter = period ? period : 1;

    if (count_up) {
        if (counter != 15) {
            counter++;
            e_out = counter;
            return;
        }
    }
    else {
        if (counter != 0) {
            counter--;
            e_out = counter;
            return;
        }
    }

    if (!econtinue) {
        counter = 0;
        mirror = 1;
    }
    else if (hold) {
        if (alternate) counter ^= 15;
        mirror = 1;
    }
    else if (alternate) {
        count_up ^= 1;
    }
    else {
        counter = count_up ? 0 : 15;
    }
    e_out = counter;
}

u8 CHIP::read(u8 addr) {
    addr &= 0x0F;
    switch(addr) {
        case REG_AFINE:
            return sw[0].freq & 0xFF;
        case REG_ACOARSE:
            return (sw[0].freq >> 8) & 0x0F;
        case REG_BFINE:
            return sw[1].freq & 0xFF;
        case REG_BCOARSE:
            return (sw[1].freq >> 8) & 0x0F;
        case REG_CFINE:
            return sw[2].freq & 0xFF;
        case REG_CCOARSE:
            return (sw[2].freq >> 8) & 0x0F;
        case REG_NOISEPER:
            return noise.period & 0x1F;
        case REG_ENABLE:
            return (sw[0].enable & 1) |
                   ((sw[1].enable & 1) << 1) |
                   ((sw[2].enable & 1) << 2) |
                   ((sw[0].enable_noise & 1) << 3) |
                   ((sw[1].enable_noise & 1) << 4) |
                   ((sw[2].enable_noise & 1) << 5) |
                   ((io_ports.enable_a & 1) << 6) |
                   ((io_ports.enable_b & 1) << 7);
        case REG_AVOL:
            return (sw[0].amplitude & 15) | ((sw[0].amplitude_mode & 1) << 4);
        case REG_BVOL:
            return (sw[1].amplitude & 15) | ((sw[1].amplitude_mode & 1) << 4);
        case REG_CVOL:
            return (sw[2].amplitude & 15) | ((sw[2].amplitude_mode & 1) << 4);
        case REG_EAFINE:
            return env.period & 0xFF;
        case REG_EACOARSE:
            return (env.period >> 8) & 0xFF;
        case REG_EASHAPE:
            return (env.hold & 1) |
                   ((env.alternate & 1) << 1) |
                   ((env.attack & 1) << 2) |
                   ((env.econtinue & 1) << 3);
        case REG_PORTA:
            return io_ports.enable_a ? io_ports.output_latch_a : io_ports.input_a;
        case REG_PORTB:
            return io_ports.enable_b ? io_ports.output_latch_b : io_ports.input_b;
    }
    return 0;
}

void CHIP::init_env_table()
{
    if (env_table_done) return;
    for (u32 shape = 0; shape < 16; shape++) {
        for (u32 i = 0; i < 16; i++) {
            env_table[shape][i] = i;
        }
    }
    env_table_done = 1;
}

void CHIP::reset_envelope()
{
    env.mirror = 0;
    env.count_up = env.attack;
    env.counter = env.count_up ? 0 : 15;
    env.e_out = env.counter;
    env.period_counter = env.period ? env.period : 1;
}

u8 CHIP::channel_level(u32 i) const
{
    return sw[i].amplitude_mode ? env.e_out : sw[i].amplitude;
}

bool CHIP::channel_output(u32 i) const
{
    return (sw[i].polarity || sw[i].enable) && (noise.polarity || sw[i].enable_noise);
}

i16 CHIP::sample_channel(int i)
{
    if ((i < 0) || (i > 2)) return 0;
    u32 ch = static_cast<u32>(i);
    i16 intensity = AY_voltable[channel_level(ch) & 15];
    return channel_output(ch) ? intensity : static_cast<i16>(-intensity);
}

i16 CHIP::mix_sample(bool for_debug)
{
    i16 sample = 0;
    if ((!ext_enable) && (!for_debug)) return 0;
    for (u32 i = 0; i < 3; i++) {
        if (sw[i].ext_enable || for_debug) sample += sample_channel(static_cast<int>(i));
    }
    return sample;
}

void CHIP::serialize(serialized_state &state)
{
#define S(x) Sadd(state, & x, sizeof( x))
    S(divider_16);
    S(divider_256);
    S(io_reg);
    for (auto &s : sw) {
        S(s.freq);
        S(s.amplitude);
        S(s.amplitude_mode);
        S(s.enable);
        S(s.enable_noise);
        S(s.polarity);
        S(s.counter);
    }
    S(noise.period);
    S(noise.counter);
    S(noise.polarity);
    S(noise.lfsr);
    S(noise.flip);
    S(env.kind);
    S(env.period);
    S(env.counter);
    S(env.count_up);
    S(env.mirror);
    S(env.e_out);
    S(env.period_counter);
    S(env.hold);
    S(env.alternate);
    S(env.attack);
    S(env.econtinue);
    S(io_ports.enable_a);
    S(io_ports.enable_b);
    S(io_ports.output_latch_a);
    S(io_ports.output_latch_b);
    S(io_ports.input_a);
    S(io_ports.input_b);
#undef S
}

void CHIP::deserialize(serialized_state &state)
{
#define L(x) Sload(state, & x, sizeof( x))
    L(divider_16);
    L(divider_256);
    L(io_reg);
    for (auto &s : sw) {
        L(s.freq);
        L(s.amplitude);
        L(s.amplitude_mode);
        L(s.enable);
        L(s.enable_noise);
        L(s.polarity);
        L(s.counter);
    }
    L(noise.period);
    L(noise.counter);
    L(noise.polarity);
    L(noise.lfsr);
    L(noise.flip);
    L(env.kind);
    L(env.period);
    L(env.counter);
    L(env.count_up);
    L(env.mirror);
    L(env.e_out);
    L(env.period_counter);
    L(env.hold);
    L(env.alternate);
    L(env.attack);
    L(env.econtinue);
    L(io_ports.enable_a);
    L(io_ports.enable_b);
    L(io_ports.output_latch_a);
    L(io_ports.output_latch_b);
    L(io_ports.input_a);
    L(io_ports.input_b);
    if (io_ports.enable_a && io_ports.write_a) io_ports.write_a(io_ports.write_a_ptr, io_ports.output_latch_a);
    if (io_ports.enable_b && io_ports.write_b) io_ports.write_b(io_ports.write_b_ptr, io_ports.output_latch_b);
#undef L
}

}
