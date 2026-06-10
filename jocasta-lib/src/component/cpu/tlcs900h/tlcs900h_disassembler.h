#pragma once

#include "helpers/int.h"
#include "helpers/debug.h"
#include "helpers/debugger/debugger.h"

namespace TLCS900H {
struct core;

void disassemble(core &cpu, u32 &PC, jsm_debug_read_trace &trace, jsm_string &outstr);
void disassemble_entry(core &cpu, disassembly_entry &entry);

}
