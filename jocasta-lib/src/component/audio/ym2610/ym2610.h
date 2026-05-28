#pragma once
#include "helpers/audio_ring.h"
#include "helpers/int.h"
#include "helpers/scheduler.h"
#include "component/audio/ay_3_8910/ay_3_8910.h"

namespace YM2610 {

enum envelope_state {
    EP_attack = 0,
    EP_decay = 1,
    EP_sustain = 2,
    EP_release = 3,
};

struct CHANNEL_FM;

struct LFO {
    bool enabled{};
    u8 counter{}, divider{}, period{};

    void run();
    void freq(u8 val);
    void enable(bool enabled_in);

    static u16 fm(u8 lfo_counter, u8 pms, u16 f_num);
    static u16 am(u8 lfo_counter, u8 ams);
};

struct OPERATOR {
    explicit OPERATOR(CHANNEL_FM *parent) : ch(parent) {}
    void run_phase();
    void run_env_ssg();
    void run_env();
    void key_on(u32 val);
    i16 run_output(i32 mod_input);
    [[nodiscard]] u16 attenuation() const;
    void update_env_key_scale_rate(u16 f_num, u16 block);
    void update_freq(u16 f_num, u16 block);
    void update_key_scale(u8 val);

    CHANNEL_FM *ch;

    u32 num{};
    u32 am_enable{};

    u32 lfo_counter{}, ams{};

    i16 output{};
    struct { u32 value{}, latch{}; } f_num{};
    struct { u32 value{}, latch{}; } block{};

    struct {
        u32 counter{};
        u16 output{};
        u16 f_num{}, block{};
        u32 input{};
        u32 multiple{}, detune{};
    } phase{};

    struct {
        envelope_state state;
        i32 rate{};
        u32 steps{};
        u32 level{};
        i32 attenuation{};

        u32 key_scale{};
        u32 key_scale_rate{};
        u32 attack_rate{};
        u32 decay_rate{};
        u32 sustain_rate{};
        u32 sustain_level{};
        u32 prev_sustain_level{};
        u32 release_rate{};

        struct {
            u32 invert_output{}, attack{}, enabled{}, hold{}, alternate{};
        } ssg{};
        u32 total_level{};
    } envelope{};

    u32 keyon{};
};

struct core;

struct CHANNEL_FM {
    explicit CHANNEL_FM(core *parent) : ym2610(parent), op{OPERATOR(this), OPERATOR(this), OPERATOR(this), OPERATOR(this)} {}
    void run();
    void update_phase_generators();

    core *ym2610;
    enum FREQ_MODE { FM_single = 0, FM_multiple } mode{};
    i32 op0_prior[2]{};
    u32 num{};
    u32 left_enable{}, right_enable{};
    bool ext_enable{true};
    i16 output{};

    u32 algorithm{}, feedback{}, pms{};
    u32 ams{};
    struct { u16 value{}, latch{}; } f_num{};
    struct { u16 value{}, latch{}; } block{};
    OPERATOR op[4];
};

struct core {
    static constexpr u32 NUM_FM_CHANNELS = 4;

    explicit core(u64 *master_cycle_count_in, u32 wait_cycles_in);

    AY_3_8910::CHIP ssg{AY_3_8910::V_8910};
    void cycle();
    void reset();
    void push_samples();
    void eval_IRQs();

    audio_output_ring* output_ring{nullptr};
    bool ext_enable{true};

    template<bool do_debug> void write_group1(u8 val);
    template<bool do_debug> void write_group2(u8 val);

    u64 scheduler_divider{};
    u64 *master_cycle_count;
    u32 wait_cycles;

    void *irq_ptr;
    void (*update_IRQs)(void *, bool);

    scheduler_t *scheduler;

    template<bool do_debug, bool peek> u8 read(u8 addr);
    template<bool do_debug> void write(u8 addr, u8 val);

    bool cycle_timers();

    void *mem_ptr{};
    u8 (*adpcm_a_read)(void *ptr, u32 addr){};
    u8 (*adpcm_a_read_debug)(void *ptr, u32 addr){};
    u8 (*adpcm_b_read)(void *ptr, u32 addr){};
    u8 (*adpcm_b_read_debug)(void *ptr, u32 addr){};

    // FM engine
    CHANNEL_FM channel[NUM_FM_CHANNELS];
    LFO lfo{};

    struct {
        u16 timer{};
        u8  timer_low{};
        u8  shift_lock{};
        u8  quotient{};
        bool tick{};
    } eg_global{};

    struct {
        i32 left_output{}, right_output{}, mono_output{};
        i32 fm_left{}, fm_right{};
        struct {
            struct { i32 left{}, right{}; } sample{};
            struct { i32 left{}, right{}; } output{};
        } filter{};
    } mix{};

    [[nodiscard]] i32 phase_compute_increment(const OPERATOR &op) const;
    void write_ch_reg(u8 val, u32 bch, u8 addr);
    void write_op_reg(u8 val, u32 bch, u8 addr);
    void mix_sample();
    [[nodiscard]] i16 sample_channel(u32 ch) const;

    struct {
        u32 enable{}, irq{}, line{}, period{}, counter{};
    } timer_a{};

    struct {
        u32 enable{}, irq{}, line{}, period{}, counter{}, divider{};
    } timer_b{};

    struct {
        u16 address{}; // bit 8 = group-2 pending; replaces separate addr1/addr2
        bool csm_enabled{};
    } io{};

    struct {
        u64 busy_until{};
        u64 env_cycle_counter{};
    } status{};

    struct CHANNEL_B {
        // Registers ($10-$1B in port A)
        u8 ctrl1{};            // $10: execute, external(forced on), repeat, speaker, reset
        u8 ctrl2{};            // $11: pan L/R
        u16 start_reg{};       // $12/$13, byte addr = start_reg << 8
        u16 end_reg{};         // $14/$15, end byte addr = (end_reg+1)<<8 - 1
        u16 prescale_reg{};    // $16/$17
        u16 delta_n{0x7FFF};   // $19/$1A, pitch (65536 = native rate)
        u8 level{0xFF};        // $1B, volume
        u16 limit_reg{0xFFFF}; // ($1C/$1D not in YM2610, but stored for compat)

        // Playback state
        bool playing{};
        bool ext_enable{true};
        bool eos{};
        u32 cur_addr{};
        u8 cur_byte{};
        u32 cur_nibble{};
        i32 accumulator{}, prev_accum{};
        i32 adpcm_step{127};
        u32 position{}; // 16-bit fractional position counter for delta_n
    } adpcm_b{};

    struct CHANNEL_A {
        u8 pan_vol{}; // [7]=pan_l, [6]=pan_r, [4:0]=instr_level
        u16 start_reg{}; // byte addr = start_reg << 8
        u16 end_reg{}; // end byte addr = (end_reg+1)<<8 - 1
        bool playing{};
        bool ext_enable{true};
        u32 cur_addr{};
        u8 cur_byte{};
        u32 cur_nibble{};
        i32 accumulator{}; // 12-bit, wraps on overflow
        i32 step_idx{};
    };
    struct {
        u8 master_vol{0x3F};
        CHANNEL_A ch[6]{};
        u32 env_counter{}; // increments each FM cycle; ADPCM-A clocked every 4th
    } adpcm_a{};

    // EOS / status flags for port-A register $02 / $1C
    u8 eos_status{};
    u8 eos_flag_mask{0xFF};

    void adpcm_b_write(u8 reg, u8 val);
    void adpcm_b_clock();
    void adpcm_a_write(u8 reg, u8 val);
    void adpcm_a_clock();
};

}
