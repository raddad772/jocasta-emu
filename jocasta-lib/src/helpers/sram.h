//
// Created by . on 11/26/24.
//

#pragma once
#include <cstdio>

#include "int.h"

enum persistent_store_kind {
    PSK_RAM,
    PSK_MAPPED_FILE,
    PSK_SIMPLE_FILE
};

struct persistent_store {
    // Internal to persisten store
    void *data{};
    u64 actual_size{};

    persistent_store_kind kind{};

    // These values must be set on discovery of RAM/SRAM
    u32 fill_value{}; // Fill value to initialize with
    // Optional seed for a freshly-created file: if init_data is set, the host fills a NEW
    // store by copying init_data (init_data_size bytes, remainder = fill_value) instead of
    // just fill_value. Used by carts whose save IS the flash image (e.g. NeoGeo Pocket), so
    // a fresh save starts as a copy of the ROM and the ROM file itself is never modified.
    const void *init_data{};
    u64 init_data_size{};
    u64 requested_size{};
    bool ready_to_use{}; // Set by host
    bool dirty{}; // Set by guest on dirtying it
    bool persistent{};

    // File-extension hint for the host's save file (without the dot), e.g. "sram", "ram".
    // Empty => the host uses its default ("sram"). Lets a system expose more than one store
    // (e.g. NeoGeo Pocket: cart flash -> <game>.sram, on-board work RAM -> <game>.ram) so each
    // lands in its own file and the cart .sram stays interchangeable with other emulators.
    char ext[16]{};

    // Internal to UI
    char filename[500]{}; // Filename if present
    FILE *fno{};
    u64 old_requested_size{};

    persistent_store() = default;
    ~persistent_store();
};
