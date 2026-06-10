#pragma once
#include "int.h"

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#define CLAMP(x, low, high) ((x) < (low) ? (low) : ((x) > (high) ? (high) : (x)))

static inline i32 MIN3(const i32 a, const i32 b, const i32 c)
{
    const i32 mab = MIN(a,b);
    return MIN(mab, c);
}

static inline i32 MAX3(const i32 a, const i32 b, const i32 c)
{
    const i32 mab = MAX(a,b);
    return MAX(mab, c);
}
