//
// Created by . on 2/20/25.
//

#pragma once

#include "helpers/int.h"
namespace PS1::GPU {

struct RT_POINT2D {
    void color24_from_cmd(u32 cmd) { r = cmd & 0xFF; g = (cmd >> 8) & 0xFF; b = (cmd >> 16) & 0xFF; }
    void uv_from_cmd(u32 cmd) { u = cmd & 0xFF; v = (cmd >> 8) & 0xFF; }
    void copyxy(const RT_POINT2D &other) { x = other.x; y = other.y; }
    void copyxycolor(const RT_POINT2D &other) { x = other.x; y = other.y; r = other.r; g = other.g; b = other.b; }
    i32 x{}, y{};
    u32 u{}, v{};
    u32 r{}, g{}, b{};
};

static inline i64 cpz(RT_POINT2D *v0, RT_POINT2D *v1, RT_POINT2D *v2) {
    return (v1->x - v0->x) * (v2->y - v0->y) - (v1->y - v0->y) * (v2->x - v0->x);
}

static inline bool check3(RT_POINT2D *a, RT_POINT2D *b, RT_POINT2D *c) {
    i64 cp = cpz(a, b, c);
    if (cp < 0) return false;
    if (cp == 0) {
        if (b->y > a->y) return false;
        if (b->y == a->y && b->x < a->x) return false;
    }
    return true;
}

static inline bool is_inside_triangle(RT_POINT2D *p, RT_POINT2D *v0, RT_POINT2D *v1, RT_POINT2D *v2) {
    if (!check3(v0, v1, p)) return false;
    if (!check3(v1, v2, p)) return false;
    if (!check3(v2, v0, p)) return false;
    return true;
}

static inline void compute_barycentric(i64 cp, RT_POINT2D *p, RT_POINT2D *v0, RT_POINT2D *v1, RT_POINT2D *v2, i64 *lambdas) {
    if (cp == 0) {
        lambdas[0] = lambdas[1] = lambdas[2] = (1LL << 32) / 3;
        return;
    }
    i64 r = (1LL << 32) / cp;
    lambdas[0] = cpz(v1, v2, p) * r;
    lambdas[1] = cpz(v2, v0, p) * r;
    lambdas[2] = ((1LL << 32) - lambdas[0]) - lambdas[1];
}

static constexpr i32 dithertable[16] = {
    -4,  +0,  -3,  +1,
    +2,  -2,  +3,  -1,
    -3,  +1,  -4,  +0,
    +3,  -1,  +2,  -2,
};



struct RT_TEXTURE_SAMPLER;
typedef u16 (*RT_texture_sample_func)(RT_TEXTURE_SAMPLER *ts, i32 u, i32 v);
struct core;
struct RT_TEXTURE_SAMPLER {
    u32 page_x{}, page_y{}, base_addr{}, clut_addr{};
    u8 *VRAM{};
    RT_texture_sample_func sample{};
};

}