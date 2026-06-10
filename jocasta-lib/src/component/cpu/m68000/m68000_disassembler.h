//
// Created by . on 6/5/24.
//
#pragma once


#include "helpers/int.h"
#include "helpers/jsm_string.h"
#include "helpers/debug.h"

namespace M68k {
struct core;
void disassemble(u32 PC, u16 IR, jsm_debug_read_trace &rt, jsm_string &out, jsm_string *ctxt_out, core *ctxt_core);

}