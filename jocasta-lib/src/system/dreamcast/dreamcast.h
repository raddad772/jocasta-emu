//
// Created by Dave on 2/11/2024.
//

#pragma once

#include "helpers/sys_interface.h"

jsm_system* DC_new();
void DC_delete(jsm_system* system);

namespace DREAMCAST {
static constexpr u64 CYCLES_PER_SEC = 200000000;
static constexpr float HBLANK_RATIO = 10.9f/63.6f; // NTSC
}


