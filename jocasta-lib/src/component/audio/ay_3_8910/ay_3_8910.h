#pragma once

#include "helpers/int.h"
#include "helpers/serialize/serialize.h"

namespace AY_3_8910 {

enum variants {
    V_8910,
    V_8914
};

enum ENVS {
    ENV_none
};

struct NOISE {
    void tick();
    u8 period{};
    u16 counter{};
    i16 polarity{};
    u32 lfsr{};
    u8 flip{};
};

struct SW {
    void tick();

    u16 freq{};
    u8 amplitude{};
    u8 amplitude_mode{};

    u32 enable{};
    u32 enable_noise{};
    i16 polarity{};

    u16 counter{};
    bool ext_enable{true};
};

struct ENV {
    void tick();
    ENVS kind{};
    u16 period{};
    u8 counter{};
    u8 count_up{};
    u8 mirror{};

    u8 e_out{};
    u16 period_counter{};

    u8 hold{}, alternate{}, attack{}, econtinue{};
};


struct CHIP {
    CHIP(variants variant_in);
    void reset();
    void select(u8 addr);
    void write_data(u8 val);
    u8 read_data();
    void write(u8 addr, u8 val);
    u8 read(u8 addr);
    i16 sample_channel(int i);
    i16 mix_sample(bool for_debug);
    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);

    variants variant{};

    u32 divider_16{};
    u32 divider_256{};

    SW sw[3]{};
    NOISE noise{};
    ENV env{};

    void cycle();
    void cycle_div_16();

    struct {
        u32 enable_a{}, enable_b{};
        u8 output_latch_a{}, output_latch_b{};
        u8 input_a{}, input_b{};
        void *write_a_ptr{};
        void *write_b_ptr{};
        void (*write_a)(void *ptr, u8 val){};
        void (*write_b)(void *ptr, u8 val){};
    } io_ports{};

    bool ext_enable{true};
    u8 io_reg{};

private:
    void init_env_table();
    void reset_envelope();
    u8 channel_level(u32 i) const;
    bool channel_output(u32 i) const;
};

}
