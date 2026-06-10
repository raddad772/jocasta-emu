#pragma once

// Thanks to TotalJustice of TotalSMS, who allowed use of their SMS PSG
// implementation. Extended here for the T6W28 in the NeoGeo Pocket.

#include "helpers/debugger/debuggerdefs.h"
#include "helpers/int.h"
#include "helpers/serialize/serialize.h"

template<bool t6w28 = false>
struct SN76489_t {
    void reset();
    void cycle();
    i16 sample_channel(i32 i);
    void sample_channel_stereo(i32 i, i16 &left, i16 &right);
    i16 mix_sample(bool for_debug);
    void mix_stereo(i16 &left, i16 &right, bool for_debug);
    void write_data(u8 val);
    void write_left(u8 val);
    void write_right(u8 val);
    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);

private:
    void cycle_squares();
    void cycle_noise();

public:
    u32 vol[4]{};
    u32 vol_r[4]{};
    i16 polarity[4]{};
    struct SN76489_noise {
        u32 lfsr{};
        u32 shift_rate{};
        u32 mode{};
        i32 counter{};
        u32 countdown{};
        i32 freq{};
        bool ext_enable{true};
    } noise{};

    struct SN76489_SW {
        i32 counter{};
        i32 freq{};
        bool ext_enable{true};
    } sw[3]{};

    bool ext_enable{true};
    u32 io_reg{};
    u32 io_kind{};

    DBG_EVENT_VIEW_ONLY;
};

using SN76489 = SN76489_t<false>;
using T6W28 = SN76489_t<true>;
