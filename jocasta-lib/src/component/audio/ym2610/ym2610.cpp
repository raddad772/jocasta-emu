//
// Created by . on 5/11/26.
//

#include <cmath>
#include <cassert>

#ifdef _MSC_VER
#define _USE_MATH_DEFINES
#endif

#include "helpers/debug.h"
#include "helpers/setbits.h"
#include "helpers/minmax.h"

#include "ym2610.h"

namespace YM2610 {

#define SIGNe14to32(x) (((((x) >> 13) & 1) * 0xFFFFC000) | ((x) & 0x3FFF))

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

static u16 sine[0x200];
static u16 pow2[0x100];

static constexpr i32 detune_table[32][4] = {
    {0,  0,  1,  2},  {0,  0,  1,  2},  {0,  0,  1,  2},  {0,  0,  1,  2},
    {0,  1,  2,  2},  {0,  1,  2,  3},  {0,  1,  2,  3},  {0,  1,  2,  3},
    {0,  1,  2,  4},  {0,  1,  3,  4},  {0,  1,  3,  4},  {0,  1,  3,  5},
    {0,  2,  4,  5},  {0,  2,  4,  6},  {0,  2,  4,  6},  {0,  2,  5,  7},
    {0,  2,  5,  8},  {0,  3,  6,  8},  {0,  3,  6,  9},  {0,  3,  7, 10},
    {0,  4,  8, 11},  {0,  4,  8, 12},  {0,  4,  9, 13},  {0,  5, 10, 14},
    {0,  5, 11, 16},  {0,  6, 12, 17},  {0,  6, 13, 19},  {0,  7, 14, 20},
    {0,  8, 16, 22},  {0,  8, 16, 22},  {0,  8, 16, 22},  {0,  8, 16, 22},
};

static constexpr u8 eg_stephi[4][4] = {
    { 0, 0, 0, 0 },
    { 1, 0, 0, 0 },
    { 1, 0, 1, 0 },
    { 1, 1, 1, 0 }
};

static constexpr u32 pg_lfo_sh1[8][8] = {
    { 7, 7, 7, 7, 7, 7, 7, 7 },
    { 7, 7, 7, 7, 7, 7, 7, 7 },
    { 7, 7, 7, 7, 7, 7, 1, 1 },
    { 7, 7, 7, 7, 1, 1, 1, 1 },
    { 7, 7, 7, 1, 1, 1, 1, 0 },
    { 7, 7, 1, 1, 0, 0, 0, 0 },
    { 7, 7, 1, 1, 0, 0, 0, 0 },
    { 7, 7, 1, 1, 0, 0, 0, 0 }
};

static constexpr u32 pg_lfo_sh2[8][8] = {
    { 7, 7, 7, 7, 7, 7, 7, 7 },
    { 7, 7, 7, 7, 2, 2, 2, 2 },
    { 7, 7, 7, 2, 2, 2, 7, 7 },
    { 7, 7, 2, 2, 7, 7, 2, 2 },
    { 7, 7, 2, 7, 7, 7, 2, 7 },
    { 7, 7, 7, 2, 7, 7, 2, 1 },
    { 7, 7, 7, 2, 7, 7, 2, 1 },
    { 7, 7, 7, 2, 7, 7, 2, 1 }
};

static bool math_done = false;

static void do_math()
{
    math_done = true;
    for (u32 mn = 0; mn < 0x200; mn++) {
        u32 i = ((mn & 0x100) ? (~mn) : mn) & 0xFF;
        double n = (double)((i << 1) | 1);
        double s = sin((n / 512.0 * M_PI / 2.0));
        double attenuation = -log2(s);
        sine[mn] = static_cast<u16>(round((attenuation * 256.0)));
    }
    for (u32 i = 0; i < 256; i++) {
        double exponent = -static_cast<double>(i + 1) / 256.0;
        double value = pow(2.0, exponent);
        pow2[i] = static_cast<i32>(static_cast<u16>(round((value * 2048.0))));
    }
}

core::core(u64 *master_cycle_count_in, u32 wait_cycles_in) :
    master_cycle_count(master_cycle_count_in),
    wait_cycles(wait_cycles_in),
    channel{CHANNEL_FM(this), CHANNEL_FM(this), CHANNEL_FM(this), CHANNEL_FM(this)}
{
    if (!math_done) do_math();

    for (u32 i = 0; i < NUM_FM_CHANNELS; i++) {
        CHANNEL_FM *ch = &channel[i];
        ch->num = i;
        ch->left_enable = ch->right_enable = 1;
        for (u32 j = 0; j < 4; j++) {
            OPERATOR *op = &ch->op[j];
            op->num = j;
            op->ch = ch;
            op->envelope.attenuation = 0x3FF;
        }
    }
}

void core::reset() {
    ssg.reset();
    io = {};
    lfo = {};
    eg_global = {};
    mix = {};
    timer_a = {};
    timer_b = {};
    adpcm_b = {};
    adpcm_a = {};
    eos_status = 0;
    eos_flag_mask = 0xFF;
    for (auto &ch : channel) {
        ch.mode = CHANNEL_FM::FM_single;
        ch.output = 0;
        ch.left_enable = ch.right_enable = 1;
        for (auto &op : ch.op) {
            op.keyon = 0;
            op.envelope.state = EP_release;
            op.envelope.attenuation = 0x3FF;
            op.phase.counter = 0;
        }
    }
}

void LFO::enable(bool enabled_in) {
    enabled = enabled_in;
    if (!enabled) counter = 0;
}

void LFO::freq(u8 val) {
    static const int dividers[8] = { 108, 77, 71, 67, 62, 44, 8, 5 };
    period = dividers[val];
}

void LFO::run() {
    divider++;
    if (divider >= period) {
        divider = 0;
        if (enabled) counter = (counter + 1) & 0x7F;
    }
}

u16 LFO::fm(const u8 lfo_counter, const u8 pms, const u16 f_num) {
    if (pms == 0) return f_num << 1;
    u32 fnum = f_num;
    u32 fnum_h = fnum >> 4;
    fnum <<= 1;
    u8 lfo_l = (lfo_counter >> 2) & 0x0f;
    if (lfo_l & 0x08) lfo_l ^= 0x0f;
    u32 fm = (fnum_h >> pg_lfo_sh1[pms][lfo_l]) + (fnum_h >> pg_lfo_sh2[pms][lfo_l]);
    if (pms > 5) fm <<= (pms - 5);
    fm >>= 2;
    if (lfo_counter & 0x40)
        return (fnum - fm) & 0xFFF;
    else
        return (fnum + fm) & 0xFFF;
}

u16 LFO::am(const u8 lfo_counter, const u8 ams) {
    u16 ama = (lfo_counter & 0x40) ? (lfo_counter & 0x3F) : (0x3F - lfo_counter);
    ama <<= 1;
    switch (ams) {
        case 0: return 0;
        case 1: return ama >> 3;
        case 2: return ama >> 1;
        case 3: return ama;
        default: NOGOHERE;
    }
}

void OPERATOR::key_on(u32 val) {
    if (val) {
        if ((!keyon) || (envelope.state == EP_release)) {
            keyon = 1;
            phase.counter = 0;
            u32 rate = (2 * envelope.attack_rate) + envelope.key_scale_rate;
            if (rate >= 62) {
                envelope.state = EP_decay;
                envelope.attenuation = 0;
            } else {
                envelope.state = EP_attack;
            }
            envelope.ssg.invert_output = 0;
        }
    } else {
        if (envelope.ssg.enabled && (envelope.state != EP_release) &&
            (envelope.ssg.invert_output != envelope.ssg.attack)) {
            envelope.attenuation = (0x200 - envelope.attenuation) & 0x3FF;
        }
        envelope.state = EP_release;
        keyon = 0;
    }
}

static inline u8 scale_key_code(u16 f_num, u8 block) {
    u32 f11 = (f_num >> 10) & 1;
    u32 f10 = (f_num >> 9) & 1;
    u32 f9 = (f_num >> 8) & 1;
    u32 f8 = (f_num >> 7) & 1;
    return (block << 2) | (f11 << 1) | ((f11 && (f10 || f9 || f8)) || (!f11 && f10 && f9 && f8));
}

i32 core::phase_compute_increment(const OPERATOR &op) const {
    const u32 mfn = lfo.fm(lfo.counter, op.ch->pms, op.phase.f_num);
    const u32 shifted = (mfn << op.phase.block) >> 2;
    const u32 key_code = scale_key_code(op.phase.f_num, op.phase.block);
    const u32 detune_mag = op.phase.detune & 3;
    const u32 detune_inc_mag = detune_table[key_code][detune_mag];
    const u32 detuned_f = ((op.phase.detune & 4) ? shifted - detune_inc_mag : shifted + detune_inc_mag) & 0x1FFFF;
    if (!op.phase.multiple) return detuned_f >> 1;
    return detuned_f * op.phase.multiple;
}

void OPERATOR::run_phase() {
    i32 inc = ch->ym2610->phase_compute_increment(*this);
    phase.counter = (phase.counter + inc) & 0xFFFFF;
    phase.output = phase.counter >> 10;
}

void OPERATOR::run_env_ssg() {
    if (envelope.attenuation < 0x200) return;

    if (envelope.ssg.alternate) {
        if (envelope.ssg.hold)
            envelope.ssg.invert_output = 1;
        else
            envelope.ssg.invert_output ^= 1;
    } else if (!envelope.ssg.hold) {
        phase.counter = 0;
    }

    if (!envelope.ssg.hold && ((envelope.state == EP_decay) || (envelope.state == EP_sustain))) {
        if (((2 * envelope.attack_rate) + envelope.key_scale_rate) >= 62) {
            envelope.attenuation = 0;
            envelope.state = EP_decay;
        } else {
            envelope.state = EP_attack;
        }
    } else if ((envelope.state == EP_release) ||
               ((envelope.state != EP_attack) && (envelope.ssg.invert_output == envelope.ssg.attack))) {
        envelope.attenuation = 0x3FF;
    }
}

void OPERATOR::run_env() {
    if (envelope.ssg.enabled) run_env_ssg();

    if (!ch->ym2610->eg_global.tick) return;

    i32 sustain_level = envelope.prev_sustain_level == 15 ? 0x3E0 : (envelope.prev_sustain_level << 5);
    envelope.prev_sustain_level = envelope.sustain_level;

    if (envelope.state == EP_attack && envelope.attenuation == 0) envelope.state = EP_decay;
    if (envelope.state == EP_decay && (envelope.attenuation & ~0xF) == sustain_level) envelope.state = EP_sustain;

    u32 r;
    switch (envelope.state) {
        case EP_attack: r = envelope.attack_rate; break;
        case EP_decay: r = envelope.decay_rate; break;
        case EP_sustain: r = envelope.sustain_rate; break;
        case EP_release: r = (envelope.release_rate << 1) + 1; break;
        default: NOGOHERE;
    }

    i32 rate = r == 0 ? 0 : (2 * r) + envelope.key_scale_rate;
    if (rate > 63) rate = 63;

    u8 eg_inc = 0;
    if (rate > 0) {
        const u8 sl = ch->ym2610->eg_global.shift_lock;
        const u8 tl = ch->ym2610->eg_global.timer_low;
        if (rate < 48) {
            const u8 sum = ((rate >> 2) + sl) & 0x0f;
            if      (sum == 12) eg_inc = 1;
            else if (sum == 13) eg_inc = (rate >> 1) & 1;
            else if (sum == 14) eg_inc = rate & 1;
        } else {
            eg_inc = eg_stephi[rate & 3][tl] + (rate >> 2) - 11;
            if (eg_inc > 4) eg_inc = 4;
        }
    }

    if (eg_inc == 0) return;
    const i32 increment = 1 << (eg_inc - 1);

    switch (envelope.state) {
        case EP_attack:
            envelope.attenuation += ((~envelope.attenuation) * increment) >> 4;
            if (envelope.attenuation < 0) envelope.attenuation = 0;
            if (envelope.attenuation > 0x3FF) envelope.attenuation = 0x3FF;
            break;
        case EP_release:
        case EP_sustain:
        case EP_decay:
            envelope.attenuation += increment;
            if (envelope.attenuation < 0) envelope.attenuation = 0;
            if (envelope.attenuation > 0x3FF) envelope.attenuation = 0x3FF;
            break;
    }
}

static u16 attenuation_to_amplitude(u16 attenuation) {
    int int_part = (attenuation >> 8) & 0x1F;
    if (int_part > 12) return 0;
    u32 fp2 = pow2[attenuation & 0xFF];
    return (fp2 << 2) >> int_part;
}

u16 OPERATOR::attenuation() const {
    u16 mattenuation;
    if (envelope.ssg.enabled && (envelope.state != EP_release) &&
        (envelope.ssg.invert_output != envelope.ssg.attack)) {
        mattenuation = (0x200 - envelope.attenuation) & 0x3FF;
    } else {
        mattenuation = envelope.attenuation;
    }
    mattenuation += envelope.total_level << 3;
    return MIN(0x3FF, mattenuation);
}

i16 OPERATOR::run_output(i32 mod_input) {
    u16 nphase = (phase.output + ((mod_input & 0xFFFFFFFE) >> 1)) & 0x3FF;
    u32 sign = (nphase >> 9) & 1;
    i32 sine_attenuation = sine[nphase & 0x1FF];
    i32 env_am_attenuation;
    if (am_enable) {
        i32 amat = ch->ym2610->lfo.am(lfo_counter, ams);
        env_am_attenuation = attenuation() + amat;
        if (env_am_attenuation < 0) env_am_attenuation = 0;
        if (env_am_attenuation > 0x3FF) env_am_attenuation = 0x3FF;
    } else {
        env_am_attenuation = attenuation();
    }
    i32 amplitude = attenuation_to_amplitude(sine_attenuation + (env_am_attenuation << 2)) & 0x7FFF;
    output = CLAMP(output, -32768, 32767);
    output = sign ? (-amplitude) : amplitude;
    return output;
}

void OPERATOR::update_env_key_scale_rate(u16 f_num_in, u16 block_in) {
    envelope.key_scale_rate = scale_key_code(f_num_in, block_in) >> (3 - envelope.key_scale);
}

void OPERATOR::update_freq(u16 f_num_in, u16 block_in) {
    phase.f_num = f_num_in;
    phase.block = block_in;
    update_env_key_scale_rate(f_num_in, block_in);
}

void OPERATOR::update_key_scale(u8 val) {
    envelope.key_scale = val;
    update_env_key_scale_rate(phase.f_num, phase.block);
}

void CHANNEL_FM::update_phase_generators() {
    if ((num == 2) && (mode == FM_multiple)) {
        for (u32 opn = 0; opn < 3; opn++) {
            OPERATOR &mop = op[opn];
            mop.update_freq(mop.f_num.value, mop.block.value);
        }
        op[3].update_freq(f_num.value, block.value);
        return;
    }
    for (u32 opn = 0; opn < 4; opn++)
        op[opn].update_freq(f_num.value, block.value);
}

void CHANNEL_FM::run() {
    for (u32 opn = 0; opn < 4; opn++) {
        OPERATOR &mop = op[opn];
        mop.run_phase();
        mop.run_env();
        mop.lfo_counter = ym2610->lfo.counter;
        mop.ams = ams;
    }

    i32 input0 = 0;
    if (feedback)
        input0 = (op0_prior[0] + op0_prior[1]) >> (10 - feedback);

    i32 out = 0;
    switch (algorithm) {
        case 0:
            op[0].run_output(input0);
            op[2].run_output(op[1].output);
            op[1].run_output(op[0].output);
            out = op[3].run_output(op[2].output);
            break;
        case 1:
            op[2].run_output(op[0].output + op[1].output);
            op[0].run_output(input0);
            op[1].run_output(0);
            out = op[3].run_output(op[2].output);
            break;
        case 2:
            op[0].run_output(input0);
            op[2].run_output(op[1].output);
            op[1].run_output(0);
            out = op[3].run_output(op[0].output + op[2].output);
            break;
        case 3:
            op[0].run_output(input0);
            op[2].run_output(0);
            out = op[3].run_output(op[1].output + op[2].output);
            op[1].run_output(op[0].output);
            break;
        case 4:
            op[0].run_output(input0);
            out = op[1].run_output(op[0].output);
            op[2].run_output(0);
            out += op[3].run_output(op[2].output);
            break;
        case 5:
            out = op[2].run_output(op[0].output);
            op[0].run_output(input0);
            out += op[1].run_output(op[0].output);
            out += op[3].run_output(op[0].output);
            break;
        case 6:
            op[0].run_output(input0);
            out = op[1].run_output(op[0].output);
            out += op[2].run_output(0);
            out += op[3].run_output(0);
            break;
        case 7:
            out = op[0].run_output(input0);
            out += op[2].run_output(0);
            out += op[1].run_output(0);
            out += op[3].run_output(0);
            break;
        default: NOGOHERE;
    }
    output = CLAMP(out, -8192, 8191);
    op0_prior[0] = op0_prior[1];
    op0_prior[1] = op[0].output;
}

void core::mix_sample() {
    i64 left = 0, right = 0;
    for (u32 i = 0; i < NUM_FM_CHANNELS; i++) {
        CHANNEL_FM &ch = channel[i];
        if (!ch.ext_enable) continue;
        const i32 smp = ch.output;
        if (ch.left_enable)  left  += smp;
        if (ch.right_enable) right += smp;
    }

    mix.left_output = static_cast<i32>((left + mix.filter.sample.left) * 0x250C + mix.filter.output.left + 0xB5E8) >> 16;
    mix.filter.sample.left = left;
    mix.filter.output.left = mix.left_output;

    mix.right_output = static_cast<i32>((right + mix.filter.sample.right) * 0x250C + mix.filter.output.right + 0xB5E8) >> 16;
    mix.filter.sample.right = right;
    mix.filter.output.right = mix.right_output;

    if (adpcm_b.ext_enable && (adpcm_b.playing || adpcm_b.prev_accum || adpcm_b.accumulator)) {
        i32 pos = static_cast<i32>(adpcm_b.position);
        i32 result = (adpcm_b.prev_accum * ((pos ^ 0xFFFF) + 1) + adpcm_b.accumulator * pos) >> 16;
        result = (result * static_cast<i32>(adpcm_b.level)) >> 9;
        if (adpcm_b.ctrl2 & 0x80) mix.left_output  += result;
        if (adpcm_b.ctrl2 & 0x40) mix.right_output += result;
    }

    for (u32 i = 0; i < 6; i++) {
        const auto &ach = adpcm_a.ch[i];
        if (!ach.ext_enable) continue;
        if (ach.accumulator == 0) continue;
        int vol = ((ach.pan_vol & 0x1F) ^ 0x1F) + (adpcm_a.master_vol ^ 0x3F);
        if (vol >= 63) continue;
        i8  mul = static_cast<i8>(15 - (vol & 7));
        u8  shift = static_cast<u8>(5 + (vol >> 3));
        i16 value = static_cast<i16>((static_cast<i16>(ach.accumulator << 4) * mul) >> shift) & ~3;
        if (ach.pan_vol & 0x80) mix.left_output  += value;
        if (ach.pan_vol & 0x40) mix.right_output += value;
    }

    mix.fm_left = mix.left_output;
    mix.fm_right = mix.right_output;

    i32 ssg_out = ssg.mix_sample(false);
    mix.left_output  += ssg_out;
    mix.right_output += ssg_out;

    mix.left_output = CLAMP(mix.left_output, -32768, 32767);
    mix.right_output = CLAMP(mix.right_output, -32768, 32767);

    mix.mono_output = (mix.left_output + mix.right_output) >> 1;
}

i16 core::sample_channel(u32 ch) const {
    return channel[ch].output;
}

void core::push_samples() {
    if (!output_ring) return;
    float l = ext_enable ? static_cast<float>(mix.fm_left)  / 32768.0f : 0.0f;
    float r = ext_enable ? static_cast<float>(mix.fm_right) / 32768.0f : 0.0f;
    output_ring->push(l, r);
}

void core::cycle() {
    eg_global.quotient = (eg_global.quotient + 1) % 3;
    eg_global.tick = (eg_global.quotient == 0);
    if (eg_global.tick) {
        eg_global.timer_low = eg_global.timer & 3;
        eg_global.timer = (eg_global.timer + 1) & 0xFFF;
        if (!eg_global.timer)
            eg_global.shift_lock = 0;
        else {
            u16 t = eg_global.timer;
            eg_global.shift_lock = 1;
            while (!(t & 1)) { eg_global.shift_lock++; t >>= 1; }
        }
    }

    lfo.run();

    for (u32 i = 0; i < NUM_FM_CHANNELS; i++)
        channel[i].run();

    adpcm_b_clock();

    adpcm_a.env_counter = (adpcm_a.env_counter + 1) & 3;
    if (adpcm_a.env_counter == 0)
        adpcm_a_clock();

    mix_sample();
}

void core::eval_IRQs() {
    update_IRQs(irq_ptr, timer_a.line | timer_b.line);
}

bool core::cycle_timers() {
    bool retval = false;
    if (timer_a.enable) {
        timer_a.counter = (timer_a.counter + 1) & 0x3FF;
        if (!timer_a.counter) {
            u32 old_line = timer_a.line | timer_b.line;
            retval = true;
            timer_a.counter = timer_a.period;
            if (timer_a.irq) timer_a.line = 1;
            if ((timer_a.line | timer_b.line) != old_line) eval_IRQs();
        }
    }

    timer_b.divider = (timer_b.divider + 1) & 15;
    if (!timer_b.divider) {
        if (timer_b.enable) {
            timer_b.counter = (timer_b.counter + 1) & 0xFF;
            if (!timer_b.counter) {
                u32 old_line = timer_a.line | timer_b.line;
                timer_b.counter = timer_b.period;
                if (timer_b.irq) timer_b.line = 1;
                if ((timer_a.line | timer_b.line) != old_line) eval_IRQs();
            }
        }
    }
    return retval;
}

void core::write_ch_reg(u8 val, u32 bch, u8 addr) {
    u32 chn = bch + (addr & 3);
    if (chn >= NUM_FM_CHANNELS) return;
    CHANNEL_FM &ch = channel[chn];

    switch (addr) {
        case 0xA0: case 0xA1: case 0xA2:
            ch.f_num.value = ch.f_num.latch | val;
            ch.block.value = ch.block.latch;
            ch.update_phase_generators();
            return;
        case 0xA4: case 0xA5: case 0xA6:
            ch.f_num.latch = (val & 7) << 8;
            ch.block.latch = (val >> 3) & 7;
            return;

        case 0xA8: case 0xA9: case 0xAA: {
            u32 chi = bch + 2;
            if (chi >= NUM_FM_CHANNELS) return;
            u32 oi = 0;
            switch (addr) {
                case 0xA8: oi = 2; break;
                case 0xA9: oi = 0; break;
                case 0xAA: oi = 1; break;
                default: break;
            }
            CHANNEL_FM &che = channel[chi];
            OPERATOR &op = che.op[oi];
            op.f_num.value = op.f_num.latch | val;
            op.block.value = op.block.latch;
            if (che.mode == CHANNEL_FM::FM_multiple)
                che.update_phase_generators();
            return; }

        case 0xAC: case 0xAD: case 0xAE: {
            u32 chi = bch + 2;
            if (chi >= NUM_FM_CHANNELS) return;
            u32 oi = 0;
            switch (addr) {
                case 0xAC: oi = 2; break;
                case 0xAD: oi = 0; break;
                case 0xAE: oi = 1; break;
                default: break;
            }
            CHANNEL_FM &che = channel[chi];
            OPERATOR &op = che.op[oi];
            op.f_num.latch = (val & 7) << 8;
            op.block.latch = (val >> 3) & 7;
            return; }

        case 0xB0: case 0xB1: case 0xB2:
            ch.algorithm = val & 7;
            ch.feedback = (val >> 3) & 7;
            return;

        case 0xB4: case 0xB5: case 0xB6:
            ch.pms = val & 7;
            ch.ams = (val >> 4) & 3;
            ch.right_enable = (val >> 6) & 1;
            ch.left_enable = (val >> 7) & 1;
            return;

        default: break;
    }
}

void core::write_op_reg(u8 val, u32 bch, u8 addr) {
    u32 offset = addr & 3;
    if (offset == 3) return;
    u32 chn = bch + offset;
    if (chn >= NUM_FM_CHANNELS) return;
    u32 opn = ((addr & 8) >> 3) | ((addr & 4) >> 1);
    CHANNEL_FM &ch = channel[chn];
    OPERATOR &op = ch.op[opn];

    switch (addr >> 4) {
        case 3:
            op.phase.multiple = val & 15;
            op.phase.detune = (val >> 4) & 7;
            return;
        case 4:
            op.envelope.total_level = val & 0x7F;
            return;
        case 5:
            op.envelope.attack_rate = val & 0x1F;
            op.update_key_scale(val >> 6);
            return;
        case 6:
            op.envelope.decay_rate = val & 0x1F;
            op.am_enable = (val >> 7) & 1;
            return;
        case 7:
            op.envelope.sustain_rate = val & 0x1F;
            return;
        case 8:
            op.envelope.release_rate = val & 15;
            op.envelope.sustain_level = val >> 4;
            return;
        case 9:
            op.envelope.ssg.hold = val & 1;
            op.envelope.ssg.alternate = (val >> 1) & 1;
            op.envelope.ssg.attack = (val >> 2) & 1;
            op.envelope.ssg.enabled = (val >> 3) & 1;
            return;
        default: break;
    }
}

template u8 core::read<false, false>(u8 addr);
template u8 core::read<false, true>(u8 addr);
template u8 core::read<true, false>(u8 addr);
template u8 core::read<true, true>(u8 addr);

template<bool do_debug, bool peek>
u8 core::read(u8 addr) {
    switch (addr) {
        case 0:
            return (((*master_cycle_count) < status.busy_until) << 7) | (timer_b.line << 1) | timer_a.line;
        case 1:
            return ssg.read_data();
        case 2:
            return eos_status & eos_flag_mask;
        case 3:
            return 0;
        default: NOGOHERE;
    }
    return 0;
}

template void core::write_group1<false>(u8 val);
template void core::write_group1<true>(u8 val);

template<bool do_debug>
void core::write_group1(u8 val) {
    status.busy_until = (*master_cycle_count) + wait_cycles;

    const u8 addr1 = static_cast<u8>(io.address);
    if (addr1 < 0x10) {
        ssg.write_data(val);
        return;
    }
    if (addr1 < 0x1C) {
        adpcm_b_write(addr1 - 0x10, val);
        return;
    }
    if (addr1 == 0x1C) {
        eos_flag_mask = ~val & 0xFF;
        eos_status   &= ~val;
        return;
    }

    switch (addr1) {
        case 0x22:
            lfo.enable((val >> 3) & 1);
            lfo.freq(val & 7);
            return;
        case 0x24:
            timer_a.period = (timer_a.period & 3) | (val << 2);
            return;
        case 0x25:
            timer_a.period = (timer_a.period & 0x3FC) | (val & 3);
            return;
        case 0x26:
            timer_b.period = val;
            return;
        case 0x27: {
            u32 old_line = timer_a.line | timer_b.line;
            if (!timer_a.enable && (val & 1)) timer_a.counter = timer_a.period;
            if (!timer_b.enable && (val & 2)) {
                timer_b.counter = timer_b.period;
                timer_b.divider = 0;
            }

            u32 olda = timer_a.enable;
            u32 oldb = timer_b.enable;
            timer_a.enable = getbit<0>(val);
            timer_b.enable = getbit<1>(val);
            if (!timer_a.enable) timer_a.counter = 0;
            if (!timer_b.enable) timer_b.counter = 0;
            if (olda != timer_a.enable && timer_a.enable) timer_a.counter = timer_a.period;
            if (oldb != timer_b.enable && timer_b.enable) timer_b.counter = timer_b.period;

            timer_a.irq = getbit<2>(val);
            timer_b.irq = getbit<3>(val);
            if (getbit<4>(val)) timer_a.line = 0;
            if (getbit<5>(val)) timer_b.line = 0;
            if ((timer_a.line | timer_b.line) != old_line) eval_IRQs();

            io.csm_enabled = (val >> 6) == 2;

            CHANNEL_FM &ch2 = channel[2];
            ch2.mode = (val & 0xC0) ? CHANNEL_FM::FM_multiple : CHANNEL_FM::FM_single;
            ch2.update_phase_generators();
            return; }

        case 0x28: {
            const u32 bchn = (val & 4) ? 3 : 0;
            const u32 offset = val & 3;
            if (offset < 3) {
                const u32 chn = bchn + offset;
                if (chn < NUM_FM_CHANNELS) {
                    CHANNEL_FM &ch = channel[chn];
                    ch.op[0].key_on((val >> 4) & 1);
                    ch.op[1].key_on((val >> 5) & 1);
                    ch.op[2].key_on((val >> 6) & 1);
                    ch.op[3].key_on((val >> 7) & 1);
                }
            }
            return; }
    }

    if (addr1 >= 0x30 && addr1 < 0xA0) {
        write_op_reg(val, 0, addr1);
        return;
    }
    if (addr1 >= 0xA0 && addr1 < 0xC0) {
        write_ch_reg(val, 0, addr1);
        return;
    }
}

template void core::write_group2<false>(u8 val);
template void core::write_group2<true>(u8 val);

template<bool do_debug>
void core::write_group2(u8 val) {
    status.busy_until = (*master_cycle_count) + wait_cycles;

    const u8 addr2 = static_cast<u8>(io.address);  // low byte; bit 8 already validated
    if (addr2 < 0x30) {
        // ADPCM-A: $00-$2D
        adpcm_a_write(addr2, val);
        return;
    }

    if (addr2 >= 0x30 && addr2 < 0xA0) {
        write_op_reg(val, 3, addr2);
        return;
    }
    if (addr2 >= 0xA0 && addr2 < 0xC0) {
        write_ch_reg(val, 3, addr2);
        return;
    }
}

void core::adpcm_b_write(u8 reg, u8 val) {
    switch (reg) {
        case 0x00: {
            u8 newctrl = (val | 0x20) & ~0x40;
            bool was_exec = (adpcm_b.ctrl1 & 0x80) != 0;
            bool new_exec = (newctrl & 0x80) != 0;
            adpcm_b.ctrl1 = newctrl;
            if (!was_exec && new_exec) {
                adpcm_b.cur_addr = static_cast<u32>(adpcm_b.start_reg) << 8;
                adpcm_b.cur_nibble = 0;
                adpcm_b.accumulator = 0;
                adpcm_b.prev_accum = 0;
                adpcm_b.adpcm_step = 127;
                adpcm_b.position = 0;
                adpcm_b.playing = true;
                adpcm_b.eos = false;
            }
            if (val & 0x01) {
                adpcm_b.playing = false;
                adpcm_b.eos = false;
                adpcm_b.accumulator = 0;
                adpcm_b.prev_accum = 0;
                adpcm_b.adpcm_step = 127;
                adpcm_b.position = 0;
            }
            return; }
        case 0x01: adpcm_b.ctrl2 = val; return;
        case 0x02: adpcm_b.start_reg = (adpcm_b.start_reg & 0xFF00) | val; return;
        case 0x03: adpcm_b.start_reg = (adpcm_b.start_reg & 0x00FF) | (val << 8); return;
        case 0x04: adpcm_b.end_reg = (adpcm_b.end_reg & 0xFF00) | val; return;
        case 0x05: adpcm_b.end_reg = (adpcm_b.end_reg & 0x00FF) | (val << 8); return;
        case 0x06: adpcm_b.prescale_reg = (adpcm_b.prescale_reg & 0xFF00) | val; return;
        case 0x07: adpcm_b.prescale_reg = (adpcm_b.prescale_reg & 0x00FF) | ((val & 7) << 8); return;
        case 0x08: return;
        case 0x09: adpcm_b.delta_n = (adpcm_b.delta_n & 0xFF00) | val; return;
        case 0x0A: adpcm_b.delta_n = (adpcm_b.delta_n & 0x00FF) | (val << 8); return;
        case 0x0B: adpcm_b.level = val; return;
        default: return;
    }
}

void core::adpcm_b_clock() {
    if (!adpcm_b.playing) return;

    u32 new_pos = adpcm_b.position + adpcm_b.delta_n;
    adpcm_b.position = new_pos & 0xFFFF;
    if (new_pos < 0x10000) return;

    if (adpcm_b.cur_nibble == 0)
        adpcm_b.cur_byte = adpcm_b_read(mem_ptr, adpcm_b.cur_addr);

    u8 data = static_cast<u8>(adpcm_b.cur_byte << (4 * adpcm_b.cur_nibble)) >> 4;
    adpcm_b.cur_nibble ^= 1;

    if (adpcm_b.cur_nibble == 0) {
        u32 end_addr = (static_cast<u32>(adpcm_b.end_reg)   + 1) << 8;
        u32 limit_addr = (static_cast<u32>(adpcm_b.limit_reg) + 1) << 8;

        if (adpcm_b.cur_addr + 1 >= end_addr) {
            if (adpcm_b.ctrl1 & 0x10) {
                adpcm_b.cur_addr = static_cast<u32>(adpcm_b.start_reg) << 8;
                adpcm_b.cur_nibble = 0;
                adpcm_b.accumulator = 0;
                adpcm_b.prev_accum = 0;
                adpcm_b.adpcm_step = 127;
                adpcm_b.position = 0;
            } else {
                adpcm_b.playing = false;
                adpcm_b.eos = true;
                adpcm_b.accumulator = 0;
                adpcm_b.prev_accum = 0;
                eos_status |= 0x80;
            }
            return;
        } else if (adpcm_b.cur_addr + 1 >= limit_addr) {
            adpcm_b.cur_addr = 0;
        } else {
            adpcm_b.cur_addr = (adpcm_b.cur_addr + 1) & 0xFFFFFF;
        }
    }

    static const u8 step_scale[8] = { 57, 57, 57, 57, 77, 102, 128, 153 };

    adpcm_b.prev_accum = adpcm_b.accumulator;

    i32 delta = (2 * (data & 7) + 1) * adpcm_b.adpcm_step / 8;
    if (data & 8) delta = -delta;

    i32 acc = adpcm_b.accumulator + delta;
    if (acc >  32767) acc = 32767;
    if (acc < -32768) acc = -32768;
    adpcm_b.accumulator = acc;

    i32 nstep = (adpcm_b.adpcm_step * step_scale[data & 7]) / 64;
    if (nstep < 127)   nstep = 127;
    if (nstep > 24576) nstep = 24576;
    adpcm_b.adpcm_step = nstep;
}

void core::adpcm_a_write(u8 reg, u8 val) {
    if (reg == 0x00) {
        bool dump = (val & 0x80) != 0;
        u8 mask = val & 0x3F;
        for (u32 i = 0; i < 6; i++) {
            if (!(mask & (1 << i))) continue;
            auto &ach = adpcm_a.ch[i];
            if (dump) {
                ach.playing = false;
                ach.accumulator = 0;
                eos_status |= (1 << i);
            } else {
                ach.playing = true;
                ach.cur_addr = static_cast<u32>(ach.start_reg) << 8;
                ach.cur_nibble = 0;
                ach.accumulator = 0;
                ach.step_idx = 0;
            }
        }
        return;
    }
    if (reg == 0x01) { adpcm_a.master_vol = val & 0x3F; return; }
    if (reg >= 0x08 && reg <= 0x0D) { adpcm_a.ch[reg - 0x08].pan_vol = val; return; }
    if (reg >= 0x10 && reg <= 0x15) {
        u32 i = reg - 0x10;
        adpcm_a.ch[i].start_reg = (adpcm_a.ch[i].start_reg & 0xFF00) | val;
        return;
    }
    if (reg >= 0x18 && reg <= 0x1D) {
        u32 i = reg - 0x18;
        adpcm_a.ch[i].start_reg = (adpcm_a.ch[i].start_reg & 0x00FF) | (val << 8);
        return;
    }
    if (reg >= 0x20 && reg <= 0x25) {
        u32 i = reg - 0x20;
        adpcm_a.ch[i].end_reg = (adpcm_a.ch[i].end_reg & 0xFF00) | val;
        return;
    }
    if (reg >= 0x28 && reg <= 0x2D) {
        u32 i = reg - 0x28;
        adpcm_a.ch[i].end_reg = (adpcm_a.ch[i].end_reg & 0x00FF) | (val << 8);
        return;
    }
}

void core::adpcm_a_clock() {
    static const u16 steps[49] = {
         16,  17,  19,  21,  23,  25,  28,
         31,  34,  37,  41,  45,  50,  55,
         60,  66,  73,  80,  88,  97, 107,
        118, 130, 143, 157, 173, 190, 209,
        230, 253, 279, 307, 337, 371, 408,
        449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552
    };
    static const i8 step_inc[8] = { -1, -1, -1, -1, 2, 5, 7, 9 };

    for (u32 i = 0; i < 6; i++) {
        auto &ach = adpcm_a.ch[i];
        if (!ach.playing) continue;

        u8 data;
        if (ach.cur_nibble == 0) {
            u32 end_addr = (static_cast<u32>(ach.end_reg) + 1) << 8;
            if (((ach.cur_addr ^ end_addr) & 0xFFFFF) == 0) {
                ach.playing = false;
                ach.accumulator = 0;
                eos_status |= (1 << i);
                continue;
            }
            ach.cur_byte = adpcm_a_read(mem_ptr, ach.cur_addr++);
            data = ach.cur_byte >> 4;
            ach.cur_nibble = 1;
        } else {
            data = ach.cur_byte & 0xF;
            ach.cur_nibble = 0;
        }

        i32 delta = (2 * (data & 7) + 1) * steps[ach.step_idx] / 8;
        if (data & 8) delta = -delta;

        ach.accumulator = (ach.accumulator + delta) & 0xFFF;

        i32 new_idx = ach.step_idx + step_inc[data & 7];
        if (new_idx < 0)  new_idx = 0;
        if (new_idx > 48) new_idx = 48;
        ach.step_idx = new_idx;
    }
}

template void core::write<false>(u8 addr, u8 val);
template void core::write<true>(u8 addr, u8 val);

template<bool do_debug>
void core::write(u8 addr, u8 val) {
    switch (addr & 3) {
        case 0: // addr-lo: clears group-2 flag
            io.address = val;
            if (val < 0x10) ssg.select(val);
            return;
        case 1: // data-lo: ignored if group-2 was last addressed
            if (io.address & 0x100) return;
            write_group1<do_debug>(val);
            return;
        case 2: // addr-hi: sets group-2 flag
            io.address = 0x100 | val;
            return;
        case 3: // data-hi: ignored if group-1 was last addressed
            if (!(io.address & 0x100)) return;
            write_group2<do_debug>(val);
            return;
        default: break;
    }
}

}
