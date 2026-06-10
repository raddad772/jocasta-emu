#pragma once
#include <cassert>
#include <vector>
#include "helpers/int.h"

template <auto bit_a, auto... rest>
constexpr void setbits(auto& r, auto set_to)
{
    static_assert(sizeof...(rest) <= 1);

    if constexpr (sizeof...(rest) == 0) {
        constexpr auto mask = 1u << bit_a;
        r = (r & ~mask) | ((set_to << bit_a) & mask);
    } else {
        constexpr auto bit_b = [] {
            auto v = 0;
            ((v = rest), ...);
            return v;
        }();

        constexpr auto bit_lo = bit_a < bit_b ? bit_a : bit_b;
        constexpr auto bit_hi = bit_a < bit_b ? bit_b : bit_a;

        constexpr auto width = bit_hi - bit_lo + 1;
        constexpr auto mask = ((1u << width) - 1) << bit_lo;

        r = (r & ~mask) | ((set_to << bit_lo) & mask);
    }
}

template <auto bit_num>
constexpr void setbit(auto& r, auto set_to)
{
    setbits<bit_num>(r, set_to);
}

template<auto byte_num>
void setbyte(auto &r, auto set_to) {
    constexpr auto bit_lo = byte_num * 8;
    constexpr auto mask = 0xFF << bit_lo;
    r = (r & ~mask) | ((set_to << bit_lo) & mask);
}

template<auto byte_num>
void setbyte(auto &r, auto set_to, auto lastmask) {
    constexpr auto bit_lo = byte_num * 8;
    constexpr auto mask = 0xFF << bit_lo;
    r = ((r & ~mask) | ((set_to << bit_lo) & mask)) & lastmask;
}


void setbyte(auto &r, int byte_num, auto set_to) {
    set_to &= 0xFF;
    switch (byte_num) {
        case 0: r = (r & ~0xFF) | set_to; return;
        case 1: r = (r & ~0xFF00) | (set_to << 8); return;
        case 2: r = (r & ~0xFF0000) | (set_to << 16); return;
        case 3: r = (r & ~0xFF000000) | (set_to << 24); return;
    //}
    //if constexpr (sizeof(r) == 8) switch (byte_num) {
        case 4: r = (r & ~0xFF00000000L) | (static_cast<u64>(set_to) << 32L); return;
        case 5: r = (r & ~0xFF0000000000L) | (static_cast<u64>(set_to) << 40L); return;
        case 6: r = (r & ~0xFF000000000000L) | (static_cast<u64>(set_to) << 48L); return;
        case 7: r = (r & ~0xFF00000000000000L) | (static_cast<u64>(set_to) << 56L); return;
    }
}

void setbyte(auto &r, int byte_num, auto set_to, auto mask) {
    set_to &= 0xFF;
    switch (byte_num) {
        case 0: r = ((r & ~0xFF) | set_to) & mask; return;
        case 1: r = ((r & ~0xFF00) | (set_to << 8)) & mask; return;
        case 2: r = ((r & ~0xFF0000) | (set_to << 16)) & mask; return;
        case 3: r = ((r & ~0xFF000000) | (set_to << 24)) & mask; return;
        case 4: r = ((r & ~0xFF00000000) | (set_to << 32)) & mask; return;
        case 5: r = ((r & ~0xFF0000000000) | (set_to << 40)) & mask; return;
        case 6: r = ((r & ~0xFF000000000000) | (set_to << 48)) & mask; return;
        case 7: r = ((r & ~0xFF00000000000000) | (set_to << 56)) & mask; return;
    }
}

template <auto bit_a, auto... rest>
constexpr auto getbits(auto r)
{
    static_assert(sizeof...(rest) <= 1);

    if constexpr (sizeof...(rest) == 0) {
        return (r >> bit_a) & 1;
    } else {
        constexpr auto bit_b = [] {
            auto v = 0;
            ((v = rest), ...);
            return v;
        }();

        constexpr auto bit_lo = bit_a < bit_b ? bit_a : bit_b;
        constexpr auto bit_hi = bit_a < bit_b ? bit_b : bit_a;

        constexpr auto width = bit_hi - bit_lo + 1;
        constexpr auto mask = (1u << width) - 1;

        return (r >> bit_lo) & mask;
    }
}

template <auto bit_num>
constexpr auto getbit(auto r) {
    return getbits<bit_num>(r);
}

template <auto byte_num>
auto getbyte(auto r) {
    constexpr auto shift = byte_num * 8;
    return (r >> shift) & 0xFF;
}

auto getbyte(auto r, auto bnum) {
    auto shift = bnum * 8;
    return (r >> shift) & 0xFF;
}

template<u32 shift>
static inline bool test_shift_bit(const u8 *array, u32 addr) {
    u8 byte = array[addr >> (shift + 3)];
    return (byte >> ((addr >> shift) & 7)) & 1;
}

template<u32 dirty_shift, u32 total_bytes>
struct dirty_bit_filter {
    static constexpr u32 bit_shift = dirty_shift;
    static constexpr u32 byte_shift = dirty_shift + 3;

    static constexpr u32 bit_stride = 1u << bit_shift;    // bytes per dirty bit
    static constexpr u32 byte_stride = 1u << byte_shift;  // bytes per dirty byte

    static constexpr u32 size_in_bytes =
        (total_bytes + byte_stride - 1) >> byte_shift;

    u8 array[size_in_bytes]{};

    void reset() {
        memset(array, 0, sizeof(array));
    }

    bool get(u32 addr) const {
        assert(addr < total_bytes);

        const u32 b_addr = addr >> byte_shift;
        assert(b_addr < size_in_bytes);

        return (array[b_addr] >> ((addr >> bit_shift) & 7)) & 1u;
    }

    void set(u32 addr) {
        assert(addr < total_bytes);

        const u32 b_addr = addr >> byte_shift;
        assert(b_addr < size_in_bytes);

        array[b_addr] |= 1u << ((addr >> bit_shift) & 7);
    }

    void clear(u32 addr) {
        assert(addr < total_bytes);

        const u32 b_addr = addr >> byte_shift;
        assert(b_addr < size_in_bytes);

        array[b_addr] &= ~(1u << ((addr >> bit_shift) & 7));
    }

    // Tests [start, end_exclusive)
    bool test_range_byte(u32 start, u32 end_exclusive) const {
        assert(start <= end_exclusive);
        assert(end_exclusive <= total_bytes);

        const u32 b_start = start >> byte_shift;
        const u32 b_end =
            (end_exclusive + byte_stride - 1) >> byte_shift;

        assert(b_start <= size_in_bytes);
        assert(b_end <= size_in_bytes);

        for (u32 b = b_start; b < b_end; b++) {
            if (array[b]) return true;
        }

        return false;
    }

    // Tests [start, end_exclusive)
    bool test_range(u32 start, u32 end_exclusive) const {
        assert(start <= end_exclusive);
        assert(end_exclusive <= total_bytes);

        u32 addr = (start >> bit_shift) << bit_shift;

        for (; addr < end_exclusive; addr += bit_stride) {
            if (get(addr)) return true;
        }

        return false;
    }

    // Clears [start, end_exclusive)
    void clear_range(u32 start, u32 end_exclusive) {
        assert(start <= end_exclusive);
        assert(end_exclusive <= total_bytes);

        const u32 clear_start =
            (start >> bit_shift) << bit_shift;

        const u32 clear_end =
            ((end_exclusive + bit_stride - 1) >> bit_shift) << bit_shift;

        for (u32 addr = clear_start; addr < clear_end; addr += bit_stride) {
            clear(addr);
        }
    }
};
