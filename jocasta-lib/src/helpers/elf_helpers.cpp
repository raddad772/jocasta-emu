//
// Created by . on 5/8/24.
//

#include "elf_helpers.h"
#include <cstdio>

elf_symbol32* elf_symbol_list32::find(u32 addr, u32 mask)
{
    if (mask == 0xFFFFFFFF) {
        auto it = by_offset.find(addr);
        if (it != by_offset.end()) {
            return &symbols[it->second];
        }
        return nullptr;
    }

    for (auto& sym : symbols) {
        if ((sym.offset & mask) == (addr & mask)) {
            return &sym;
        }
    }

    return nullptr;
}

void elf_symbol_list32::add(u32 offset, const char* fname, const char* name, elf_symbol32_kind kind)
{
    if (by_offset.contains(offset)) {
        return;
    }

    elf_symbol32 sym{};
    sym.offset = offset;
    sym.kind = kind;

    snprintf(sym.name, sizeof(sym.name), "%s", name ? name : "");
    snprintf(sym.fname, sizeof(sym.fname), "%s", fname ? fname : "");

    symbols.push_back(sym);
    by_offset[offset] = symbols.size() - 1;
}