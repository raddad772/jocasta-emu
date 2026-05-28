//
// Created by . on 5/10/24.
//

#ifndef JOCASTA_EMUS_MYRANDOM_H
#define JOCASTA_EMUS_MYRANDOM_H

#include "helpers/int.h"

struct sfc32_state {
    u64 a, b, c, d;
};

void sfc32_seed(const char *seed, sfc32_state *state);
u32 sfc32(struct sfc32_state *state);

#endif //JOCASTA_EMUS_MYRANDOM_H
