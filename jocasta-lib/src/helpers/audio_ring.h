#pragma once

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "int.h"

// ─────────────────────────────────────────────────────────────────────────────
// audio_output_ring
//
// Lock-free, single-producer / single-consumer (SPSC) ring buffer of stereo
// float sample pairs.  Designed to be pushed by the emulator thread and
// popped by the audio callback thread without any mutex.
//
// Rules:
//   • capacity MUST be a power of two (enforced by assert in init()).
//   • push() is called by the emulator thread only.
//   • pop() / available() are called by the audio callback thread only.
//   • queued() / drain() / keep_latest() are called by the emulator thread only.
//   • Never call init() or destroy() while the audio backend is running.
//
// Overrun behaviour: push() drops the sample silently when the ring is full.
// This is the correct behaviour for fast-forward: the backend drains slowly,
// the emulator fills quickly, the ring saturates and extra samples are dropped.
// ─────────────────────────────────────────────────────────────────────────────

struct audio_output_ring {
    float* data     = nullptr;  // interleaved L, R, L, R … (capacity*2 floats)
    u32    capacity = 0;        // stereo pairs, MUST be a power of two
    u32    mask     = 0;        // capacity - 1  (fast modulo)

    // Emulator writes write_pos; audio callback reads it (and vice-versa for read_pos).
    // Placed on separate cache lines to eliminate false sharing.
    alignas(64) std::atomic<u32> write_pos{0};
    alignas(64) std::atomic<u32> read_pos{0};

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    void init(u32 capacity_pairs)
    {
        destroy();
        // capacity_pairs must be a power of two
        capacity = capacity_pairs;
        mask     = capacity_pairs - 1;
        data     = static_cast<float*>(malloc(capacity_pairs * 2 * sizeof(float)));
        memset(data, 0, capacity_pairs * 2 * sizeof(float));
        write_pos.store(0, std::memory_order_relaxed);
        read_pos.store(0,  std::memory_order_relaxed);
    }

    void destroy()
    {
        if (data) { free(data); data = nullptr; }
        capacity = 0;
        mask     = 0;
    }

    // ── Emulator-thread API ───────────────────────────────────────────────────

    // Push one stereo pair.  Drops silently when the ring is full.
    inline void push(float l, float r) noexcept
    {
        u32 w = write_pos.load(std::memory_order_relaxed);
        if (w - read_pos.load(std::memory_order_acquire) >= capacity)
            return; // full — drop (natural fast-forward protection)
        u32 i = (w & mask) * 2;
        data[i]     = l;
        data[i + 1] = r;
        write_pos.store(w + 1, std::memory_order_release);
    }

    // Stereo pairs currently buffered (emulator timing loop).
    inline u32 queued() const noexcept
    {
        u32 w = write_pos.load(std::memory_order_relaxed);
        u32 r = read_pos.load(std::memory_order_acquire);
        return w - r; // unsigned subtraction wraps correctly on overflow
    }

    // Discard every buffered sample (game load, hard fast-forward flush).
    inline void drain() noexcept
    {
        read_pos.store(write_pos.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
    }

    // After fast-forward: keep only the most-recent `keep` pairs so
    // playback resumes immediately without a stale-audio burst.
    inline void keep_latest(u32 keep) noexcept
    {
        u32 q = queued();
        if (q > keep) {
            u32 w = write_pos.load(std::memory_order_relaxed);
            read_pos.store(w - keep, std::memory_order_relaxed);
        }
    }

    // Prepend a linear ramp of `pad` stereo pairs in front of the current
    // read position.  The ramp goes from (0,0) at index 0 to (target_l,
    // target_r) at index pad-1, so the first real sample follows naturally.
    //
    // Only safe to call from the emulator thread while the stream is locked
    // (audio callback not consuming).  Assumes the ring has at least `pad`
    // slots behind the current read head (guaranteed when the ring is sized
    // at >= 4× frames-per-frame and called immediately after one frame's
    // worth of samples have been pushed).
    inline void prepend_ramp(float target_l, float target_r, u32 pad) noexcept
    {
        if (pad == 0) return;
        u32 r = read_pos.load(std::memory_order_relaxed);
        float inv = 1.0f / (float)pad;
        for (u32 i = 0; i < pad; i++) {
            float t = (float)(i + 1) * inv; // 1/pad … 1.0 (ramps up to target)
            u32 pos = (r - pad + i) & mask;
            data[pos * 2]     = target_l * t;
            data[pos * 2 + 1] = target_r * t;
        }
        read_pos.store(r - pad, std::memory_order_release);
    }

    // ── Audio-callback-thread API ─────────────────────────────────────────────

    // Stereo pairs available to read (audio callback).
    inline u32 available() const noexcept
    {
        u32 w = write_pos.load(std::memory_order_acquire);
        u32 r = read_pos.load(std::memory_order_relaxed);
        return w - r;
    }

    // Pop up to n stereo pairs into out[0 .. n*2-1] (interleaved L, R).
    // Returns the number of pairs actually popped (may be < n if ring is empty).
    inline u32 pop(float* out, u32 n) noexcept
    {
        u32 r     = read_pos.load(std::memory_order_relaxed);
        u32 w     = write_pos.load(std::memory_order_acquire);
        u32 avail = w - r;
        u32 count = (n < avail) ? n : avail;
        for (u32 i = 0; i < count; i++) {
            u32 idx    = ((r + i) & mask) * 2;
            out[i * 2]     = data[idx];
            out[i * 2 + 1] = data[idx + 1];
        }
        read_pos.store(r + count, std::memory_order_release);
        return count;
    }
};

// Return the smallest power of two >= n (minimum 1).
inline u32 audio_ring_next_pow2(u32 n)
{
    if (n <= 1) return 1;
    n--;
    n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16;
    return n + 1;
}
