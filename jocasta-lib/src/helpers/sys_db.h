#pragma once
#include "helpers/enums.h"


struct BIOSFile {
    const char* filename;
    const char* sha256;
    const char* label;
    bool optional;
    bool writable;
};

struct BIOSEntry {
    jsm::systems sys;
    const char* dir_slug;
    const char* menu_label;
    bool allow_unknown;
    int num_files;
    BIOSFile files[4];
};

extern const BIOSEntry SYS_DB[];
extern const int SYS_DB_COUNT;

const BIOSEntry* sysdb_find_bios(jsm::systems sys);

bool sysdb_bios_required(jsm::systems sys);
