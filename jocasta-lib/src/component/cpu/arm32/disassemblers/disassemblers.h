//
// Created by . on 12/21/24.
//

#pragma once
#include "helpers/int.h"
#include "helpers/debug.h"
#include "helpers/debugger/debugger.h"

namespace ARM32 {
struct ARMctxt {
    u32 regs; // bits 0-15: regs 0-15. bit 16: CPSR, etc...
};


void ARMv4_disassemble(u32 opcode, jsm_string *out, i64 ins_addr, ARMctxt *ct);
void THUMBv4_disassemble(u16 opc, jsm_string *out, i64 ins_addr, ARMctxt *ct);
void ARMv5_disassemble(u32 opcode, jsm_string *out, i64 ins_addr, ARMctxt *ct);
void THUMBv5_disassemble(u16 opc, jsm_string *out, i64 ins_addr, ARMctxt *ct);
}

