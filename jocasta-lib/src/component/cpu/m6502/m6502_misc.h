//
// Created by Dave on 2/4/2024.
//

#pragma once
namespace M6502 {
struct REGS;
struct PINS;

typedef void (*ins_func)(REGS&, PINS&);
}
