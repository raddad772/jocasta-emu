#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <deque>
#include <vector>

#include "int.h"

namespace block_cache_better_detail {

template <typename T>
concept has_block_cache_better_fields = requires(T t) {
    { t.cached_addr } -> std::convertible_to<u32 &>;
    { t.sz } -> std::convertible_to<u32 &>;
    { t.exec_count } -> std::convertible_to<u32 &>;
    { t.page_first } -> std::convertible_to<u32 &>;
    { t.page_span } -> std::convertible_to<u32 &>;
    { t.version_snap[0] } -> std::convertible_to<u32 &>;
    { t.instructions.clear() };
    { t.instructions.empty() } -> std::convertible_to<bool>;
};

constexpr u32 calc_shift(u32 v) {
    u32 s = 0;
    while ((1u << s) < v) ++s;
    return s;
}

constexpr bool is_pow2(u32 v) {
    return v != 0 && (v & (v - 1)) == 0;
}

}

template <
    u32 total_size_bytes,
    u32 dirty_page_size_bytes,
    u32 block_pointer_granularity,
    u32 max_block_bytes,
    typename block_t,
    u32 num_stores = 1,
    typename second_block_t = block_t
>
struct block_cache_better {
    static constexpr u32 MAX_PAGES_SPANNED = 5;

    static_assert(block_cache_better_detail::is_pow2(total_size_bytes),
                  "total_size_bytes must be a power of two");
    static_assert(block_cache_better_detail::is_pow2(dirty_page_size_bytes),
                  "dirty_page_size_bytes must be a power of two");
    static_assert(block_cache_better_detail::is_pow2(block_pointer_granularity),
                  "block_pointer_granularity must be a power of two");
    static_assert(dirty_page_size_bytes <= total_size_bytes,
                  "dirty_page_size_bytes must not exceed total_size_bytes");
    static_assert(block_pointer_granularity <= dirty_page_size_bytes,
                  "block_pointer_granularity must not exceed dirty_page_size_bytes");
    static_assert(max_block_bytes > 0, "max_block_bytes must be > 0");
    static_assert(num_stores >= 1 && num_stores <= 2,
                  "num_stores must be 1 or 2 (extend if you need more)");
    static_assert(block_cache_better_detail::has_block_cache_better_fields<block_t>,
                  "block_t must declare: u32 cached_addr, sz, exec_count, page_first, page_span; "
                  "u32 version_snap[>=5]; and an 'instructions' container with .clear()/.empty().");
    static_assert(block_cache_better_detail::has_block_cache_better_fields<second_block_t>,
                  "second_block_t must declare: u32 cached_addr, sz, exec_count, page_first, page_span; "
                  "u32 version_snap[>=5]; and an 'instructions' container with .clear()/.empty().");

    static constexpr u32 dirty_shift     = block_cache_better_detail::calc_shift(dirty_page_size_bytes);
    static constexpr u32 block_ptr_shift = block_cache_better_detail::calc_shift(block_pointer_granularity);

    static constexpr u32 num_dirty_pages = total_size_bytes / dirty_page_size_bytes;
    static constexpr u32 num_slots       = total_size_bytes / block_pointer_granularity;

    static constexpr u32 max_pages_spanned = ((dirty_page_size_bytes - 1 + max_block_bytes - 1) / dirty_page_size_bytes) + 2;

    static_assert(max_pages_spanned <= MAX_PAGES_SPANNED,
                  "Configuration would let a block span more dirty pages than MAX_PAGES_SPANNED. "
                  "Decrease max_block_bytes, increase dirty_page_size_bytes, or raise both "
                  "MAX_PAGES_SPANNED and version_snap[] in the block struct.");

    static constexpr u32 COUNTER_HIGH_WATERMARK = 0x8000'0000u;

    struct stats_t {
        u64 requests{};
        u64 hits{};
        u64 misses{};
        u64 invalidations{};
        u64 dirty_marks{};
        u64 commits{};
        u64 live_blocks{};
    };

    struct store_t {
        std::vector<block_t *> slots;
        std::deque<block_t> pool;
        std::vector<block_t *> free_list;
        std::vector<std::vector<u32>> live_slots;
        stats_t stats{};
    };

    store_t first_store{};
    store_t second_store{};
    std::vector<u32> page_version;
    u32 exec_counter{1};

    block_cache_better() {
        page_version.assign(num_dirty_pages, 1u);
        first_store.slots.assign(num_slots, nullptr);
        first_store.live_slots.resize(num_dirty_pages);
        if constexpr(num_stores == 2) {
            second_store.slots.assign(num_slots, nullptr);
            second_store.live_slots.resize(num_dirty_pages);
        }
    }

    static constexpr u32 slot_index_of_addr(u32 cached_addr) {
        return cached_addr >> block_ptr_shift;
    }

    static constexpr u32 dirty_page_of_addr(u32 cached_addr) {
        return cached_addr >> dirty_shift;
    }

    template<u32 store_num>
    auto &get_store() {
        static_assert(store_num < num_stores);

        if constexpr (store_num == 0) {
            return first_store;
        } else {
            return second_store;
        }
    }

    template <u32 store_num>
    block_t *alloc_or_reuse(u32 cached_addr) {
        static_assert(store_num < num_stores);
        auto &s = get_store<store_num>();
        block_t *bl;
        if (!s.free_list.empty()) {
            bl = s.free_list.back();
            s.free_list.pop_back();
        } else {
            bl = &s.pool.emplace_back();
        }
        bl->instructions.clear();
        bl->cached_addr = cached_addr;
        bl->sz = 0;
        bl->exec_count = 0;
        bl->page_first = dirty_page_of_addr(cached_addr);
        bl->page_span = 0;
        const u32 idx = slot_index_of_addr(cached_addr);
        s.slots[idx] = bl;
        s.live_slots[bl->page_first].push_back(idx);
        return bl;
    }

    template <u32 store_num = 0, bool do_debug = false>
    block_t *peek_block(u32 cached_addr, u8 mem_region) {
        static_assert(store_num < num_stores);
        auto &s = get_store<store_num>();
        const u32 idx = slot_index_of_addr(cached_addr);
        block_t *bl = s.slots[idx];
        if constexpr (do_debug) s.stats.requests++;
        if (!bl) {
            if constexpr (do_debug) s.stats.misses++;
            bl = alloc_or_reuse<store_num>(cached_addr);
        } else {
            if constexpr (do_debug) s.stats.hits++;
        }
        bl->exec_count = exec_counter++;
        bl->mem_region = mem_region;
        return bl;
    }

    template <u32 store_num = 0, bool do_debug = false>
    block_t *get_block(u32 cached_addr, u8 mem_region) {
        static_assert(store_num < num_stores);
        auto &s = get_store<store_num>();
        const u32 idx = slot_index_of_addr(cached_addr);
        block_t *bl = s.slots[idx];

        if constexpr (do_debug) s.stats.requests++;

        if (!bl) {
            if constexpr (do_debug) s.stats.misses++;
            bl = alloc_or_reuse<store_num>(cached_addr);
            bl->exec_count = exec_counter++;
            bl->mem_region = mem_region;
            return bl;
        }

        bool stale = page_version[bl->page_first] != bl->version_snap[0];
        if constexpr (max_pages_spanned > 1) {
            if (!stale && bl->page_span > 1) {
                for (u32 i = 1; i < bl->page_span; ++i) {
                    if (page_version[bl->page_first + i] != bl->version_snap[i]) {
                        stale = true;
                        break;
                    }
                }
            }
        }

        if (stale) {
            if constexpr (do_debug) s.stats.invalidations++;
            bl->instructions.clear();
            bl->sz = 0;
            bl->page_span = 0;
        } else {
            if constexpr (do_debug) s.stats.hits++;
        }

        bl->exec_count = exec_counter++;
        bl->mem_region = mem_region;
        return bl;
    }

    template <bool do_debug = false>
    void mark_dirty(u32 cached_addr) {
        const u32 page = dirty_page_of_addr(cached_addr);
        page_version[page]++;
        if constexpr (do_debug) {
            for (auto &s : first_store) s.stats.dirty_marks++;
            if constexpr(num_stores == 2) for (auto &s : second_store) s.stats.dirty_marks++;
        }
    }

    template<u32 store_num>
    using store_block_t = std::conditional_t<store_num == 0, block_t, second_block_t>;

    template<u32 store_num>
    void commit(void *block_ptr) {
        static_assert(store_num < num_stores);

        auto *bl = static_cast<store_block_t<store_num> *>(block_ptr);

        assert(bl != nullptr);
        assert(bl->sz > 0);
        assert((bl->cached_addr & (block_pointer_granularity - 1)) == 0);
        assert(bl->cached_addr + bl->sz <= total_size_bytes);

        const u32 page_first = bl->cached_addr >> dirty_shift;
        const u32 page_last = (bl->cached_addr + bl->sz - 1) >> dirty_shift;
        const u32 page_span = (page_last - page_first) + 1;

        assert(page_span <= max_pages_spanned);

        bl->page_first = page_first;
        bl->page_span = page_span;

        for (u32 i = 0; i < page_span; ++i) {
            bl->version_snap[i] = page_version[page_first + i];
        }
    }

    void reset_dirty() {
        for (auto &v : page_version) v = 1;
    }

    template <u32 store_num>
    void clear_all_blocks_in() {
        static_assert(store_num < num_stores);
        auto &s = get_store<store_num>();
        for (auto &bl : s.pool) bl.instructions.clear();
        for (auto *&p : s.slots) p = nullptr;
        for (auto &list : s.live_slots) list.clear();
        s.free_list.clear();
        for (auto &bl : s.pool) s.free_list.push_back(&bl);
    }

    void clear_all_blocks() {
        if constexpr (num_stores >= 1) clear_all_blocks_in<0>();
        if constexpr (num_stores >= 2) clear_all_blocks_in<1>();
    }

    void reset() {
        reset_dirty();
        clear_all_blocks();
        exec_counter = 1;
        first_store.stats = {};
        if constexpr (num_stores == 2) second_store.stats = {};
    }

    template<u32 store_num>
    void gc_stale_blocks(u32 age_threshold) {
        gc_stale_blocks_do<0>(age_threshold);
        if constexpr(num_stores == 2) gc_stale_blocks_do<1>(age_threshold);
    }

    template<u32 store_num>
    void gc_stale_blocks_do(u32 age_threshold) {
        const u32 cutoff = exec_counter > age_threshold ? exec_counter - age_threshold : 0;
        auto &s = get_store<store_num>();
        u64 alive = 0;
        for (u32 page = 0; page < num_dirty_pages; ++page) {
            auto &list = s.live_slots[page];
            u32 dst = 0;
            for (u32 idx : list) {
                block_t *bl = s.slots[idx];
                if (!bl) continue;
                if (bl->page_first != page) continue;
                if (bl->exec_count >= cutoff) {
                    list[dst++] = idx;
                    ++alive;
                } else {
                    bl->instructions.clear();
                    s.slots[idx] = nullptr;
                    s.free_list.push_back(bl);
                }
            }
            list.resize(dst);
        }
        s.stats.live_blocks = alive;
    }

    void gc_counter_rollover() {
        gc_counter_rollover_do_step1<0>();
        if constexpr(num_stores == 2) gc_counter_rollover_do_step1<1>();

        for (auto &v : page_version) v = 1;
        exec_counter = 1;

        gc_counter_rollover_do_step2<0>();
        if constexpr(num_stores == 2) gc_counter_rollover_do_step2<1>();
    }

    template<u32 store_num>
    void gc_counter_rollover_do_step1() {
        auto &s = get_store<store_num>();
        u64 alive = 0;
        for (u32 page = 0; page < num_dirty_pages; ++page) {
            auto &list = s.live_slots[page];
            u32 dst = 0;
            const u32 page_v = page_version[page];
            for (u32 idx : list) {
                block_t *bl = s.slots[idx];
                if (!bl) continue;
                if (bl->page_first != page) continue;
                bool current = (bl->version_snap[0] == page_v);
                if (current && bl->page_span > 1) {
                    for (u32 i = 1; i < bl->page_span; ++i) {
                        if (bl->version_snap[i] != page_version[bl->page_first + i]) {
                            current = false;
                            break;
                        }
                    }
                }
                if (current) {
                    for (u32 i = 0; i < bl->page_span; ++i) bl->version_snap[i] = 1;
                    list[dst++] = idx;
                    ++alive;
                } else {
                    bl->instructions.clear();
                    s.slots[idx] = nullptr;
                    s.free_list.push_back(bl);
                }
            }
            list.resize(dst);
        }
        s.stats.live_blocks = alive;
    }

    template<u32 store_num>
    void gc_counter_rollover_do_step2() {
        auto &s = get_store<store_num>();
        for (auto &bl : s.pool) bl.exec_count = 0;
    }

    [[nodiscard]] bool counter_high() const {
        if (exec_counter >= COUNTER_HIGH_WATERMARK) return true;
        for (u32 v : page_version) if (v >= COUNTER_HIGH_WATERMARK) return true;
        return false;
    }

    template <u32 store_num = 0>
    const stats_t &get_stats() const {
        static_assert(store_num < num_stores);
        return get_store<store_num>().stats;
    }

    void reset_stats() {
        first_store.stats = {};
        if constexpr (num_stores == 2) second_store.stats = {};
    }
};
