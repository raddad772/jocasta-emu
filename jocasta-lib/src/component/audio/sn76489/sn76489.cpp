#include <cstdio>
#include <cstring>

#include "sn76489.h"

static constexpr i16 SMSGG_voltable[16] = {
        8191, 6507, 5168, 4105, 3261, 2590, 2057, 1642,
        1298, 1031, 819, 650, 516, 410, 326, 0
};

template<bool t6w28>
void SN76489_t<t6w28>::reset()
{
    polarity[0] = polarity[1] = polarity[2] = polarity[3] = 1;
    vol[0] = vol[1] = vol[2] = vol[3] = 0x0F;
    vol_r[0] = vol_r[1] = vol_r[2] = vol_r[3] = 0x0F;

    sw[0].freq = sw[1].freq = sw[2].freq = 0;
    noise.freq = 0;
    noise.lfsr = t6w28 ? 0x4000 : 0x8000;

    io_reg = 0;
}

template<bool t6w28>
void SN76489_t<t6w28>::cycle_squares()
{
    for (u32 i = 0; i < 3; i++) {
        SN76489_SW& tone = sw[i];
        if (tone.counter > 0)
            tone.counter--;

        if (tone.counter <= 0) {
            tone.counter = tone.freq;
            if (tone.freq > 0) {
                polarity[i] ^= 1;
            }
        }
    }
}

template<bool t6w28>
void SN76489_t<t6w28>::cycle_noise()
{
    noise.counter--;
    if (noise.counter > 0) return;

    if constexpr (t6w28) {
        switch (noise.shift_rate) {
            case 0: noise.counter += 0x10; break;
            case 1: noise.counter += 0x20; break;
            case 2: noise.counter += 0x40; break;
            case 3: noise.counter += noise.freq; break;
        }
        noise.countdown ^= 1;
        if (noise.countdown) {
            polarity[3] = static_cast<i16>((noise.lfsr & 1) ^ 1);
            u32 feedback = (noise.lfsr & 1) ^ (((noise.lfsr >> 2) & 1) & (noise.mode ? 1u : 0u));
            noise.lfsr = (feedback << 14) | (noise.lfsr >> 1);
        }
    } else {
        u32 multiplier = 1;
        switch (noise.shift_rate) {
            case 0: noise.counter += 16 * multiplier; break;
            case 1: noise.counter += 2 * 16 * multiplier; break;
            case 2: noise.counter += 4 * 16 * multiplier; break;
            case 3: noise.counter += sw[2].freq * 16; break;
        }
        noise.countdown ^= 1;
        if (noise.countdown) {
            polarity[3] = static_cast<i16>(noise.lfsr & 1);

            if (noise.mode) {
                u32 p = noise.lfsr & 9;
                p ^= (p >> 8);
                p ^= (p >> 4);
                p &= 0xF;
                p = ((0x6996 >> p) & 1) ^ 1;
                noise.lfsr = (noise.lfsr >> 1) | (p << 15);
            } else {
                noise.lfsr = (noise.lfsr >> 1) | ((noise.lfsr & 1) << 15);
            }
        }
    }
}

template<bool t6w28>
void SN76489_t<t6w28>::cycle() {
    cycle_noise();
    cycle_squares();
}

template<bool t6w28>
i16 SN76489_t<t6w28>::sample_channel(i32 i)
{
    i16 sample;
    i16 intensity = SMSGG_voltable[vol[i]];
    if (i < 3) sample = ((polarity[i] * 2) - 1) * intensity;
    else sample = ((polarity[3] * 2) - 1) * intensity;
    return sample;
}

template<bool t6w28>
void SN76489_t<t6w28>::sample_channel_stereo(i32 i, i16 &left, i16 &right)
{
    i16 sign = (polarity[i] * 2) - 1;
    left = sign * SMSGG_voltable[vol[i]];
    right = sign * SMSGG_voltable[vol_r[i]];
}

template<bool t6w28>
i16 SN76489_t<t6w28>::mix_sample(bool for_debug)
{
    i16 sample = 0;
    if ((!ext_enable) && (!for_debug)) return 0;
    for (u32 i = 0; i < 3; i++) {
        i16 intensity = SMSGG_voltable[vol[i]];
        if (sw[i].ext_enable) {
            sample += ((polarity[i] * 2) - 1) * intensity;
        }
    }

    if (noise.ext_enable) {
        i16 intensity = SMSGG_voltable[vol[3]];
        sample += ((polarity[3] * 2) - 1) * intensity;
    }

    return sample;
}

template<bool t6w28>
void SN76489_t<t6w28>::mix_stereo(i16 &left, i16 &right, bool for_debug)
{
    left = right = 0;
    if ((!ext_enable) && (!for_debug)) return;
    for (u32 i = 0; i < 3; i++) {
        if (!sw[i].ext_enable) continue;
        i16 sign = (polarity[i] * 2) - 1;
        left += sign * SMSGG_voltable[vol[i]];
        right += sign * SMSGG_voltable[vol_r[i]];
    }
    if (noise.ext_enable) {
        i16 sign = (polarity[3] * 2) - 1;
        left += sign * SMSGG_voltable[vol[3]];
        right += sign * SMSGG_voltable[vol_r[3]];
    }
}

template<bool t6w28>
void SN76489_t<t6w28>::write_data(u8 val)
{
    if (val & 0x80) {
        io_reg = (val >> 5) & 3;
        io_kind = (val >> 4) & 1;
        u32 data = val & 0x0F;
        if (io_kind) {
            vol[io_reg] = data;
        }
        else {
            if (io_reg < 3) {
                sw[io_reg].freq = (sw[io_reg].freq & 0x3F0) | data;
            }
            else {
                noise.lfsr = 0x8000;
                noise.shift_rate = data & 3;
                noise.mode = (data >> 2) & 1;
            }
        }
    }
    else {
        u32 data = val & 0x0F;
        if (io_kind) {
            vol[io_reg] = data;
        } else {
            if (io_reg < 3)
                sw[io_reg].freq = (sw[io_reg].freq & 0x0F) | ((val & 0x3F) << 4);
            else {
                noise.lfsr = 0x8000;
                noise.shift_rate = data & 3;
                noise.mode = (data >> 2) & 1;
            }
        }
    }
}

template<bool t6w28>
void SN76489_t<t6w28>::write_left(u8 val)
{
    if (val & 0x80) {
        io_reg = (val >> 5) & 3;
        io_kind = (val >> 4) & 1;
        u32 data = val & 0x0F;
        if (io_kind) {
            vol[io_reg] = data;
        } else if (io_reg < 3) {
            sw[io_reg].freq = (sw[io_reg].freq & 0x3F0) | data;
        }
    } else if (!io_kind && io_reg < 3) {
        sw[io_reg].freq = (sw[io_reg].freq & 0x0F) | ((val & 0x3F) << 4);
    }
}

template<bool t6w28>
void SN76489_t<t6w28>::write_right(u8 val)
{
    if (val & 0x80) {
        io_reg = (val >> 5) & 3;
        io_kind = (val >> 4) & 1;
        u32 data = val & 0x0F;
        if (io_kind) {
            vol_r[io_reg] = data;
        } else if (io_reg == 2) {
            noise.freq = (noise.freq & 0x3F0) | data;
        } else if (io_reg == 3) {
            noise.shift_rate = data & 3;
            noise.mode = (data >> 2) & 1;
            noise.lfsr = 0x4000;
        }
    } else if (!io_kind && io_reg == 2) {
        noise.freq = (noise.freq & 0x0F) | ((val & 0x3F) << 4);
    }
}

template<bool t6w28>
void SN76489_t<t6w28>::serialize(serialized_state &state)
{
    u32 i;
#define S(x) Sadd(state, & x, sizeof( x))
    S(io_reg);
    S(io_kind);

    for (i = 0; i < 4; i++) {
        S(vol[i]);
        S(polarity[i]);
    }

    for (i = 0; i < 3; i++) {
        S(sw[i].counter);
        S(sw[i].freq);
    }

    S(noise.lfsr);
    S(noise.shift_rate);
    S(noise.mode);
    S(noise.counter);
    S(noise.countdown);

    if constexpr (t6w28) {
        for (i = 0; i < 4; i++) S(vol_r[i]);
        S(noise.freq);
    }
#undef S
}

template<bool t6w28>
void SN76489_t<t6w28>::deserialize(serialized_state &state)
{
    u32 i;
#define L(x) Sload(state, & x, sizeof( x))
    L(io_reg);
    L(io_kind);

    for (i = 0; i < 4; i++) {
        L(vol[i]);
        L(polarity[i]);
    }

    for (i = 0; i < 3; i++) {
        L(sw[i].counter);
        L(sw[i].freq);
    }

    L(noise.lfsr);
    L(noise.shift_rate);
    L(noise.mode);
    L(noise.counter);
    L(noise.countdown);

    if constexpr (t6w28) {
        for (i = 0; i < 4; i++) L(vol_r[i]);
        L(noise.freq);
    }
#undef L
}

template struct SN76489_t<false>;
template struct SN76489_t<true>;
