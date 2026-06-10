#pragma once
#include <string>
#include <vector>
#include "helpers/sys_db.h"
#include "helpers/buf.h"
#include "helpers/sram.h"

class AppSettings;

enum class bios_status {
    unchecked,
    missing,
    bad_hash,
    unknown_hash,
    ok,
};

struct BIOSCheckResult {
    const BIOSFile* file;
    bios_status status;
    std::string sha256_actual;
};

std::string bios_default_dir(const char* slug);
std::string bios_get_dir(jsm::systems sys, const AppSettings& s);
void bios_set_dir(jsm::systems sys, const char* dir, AppSettings& s);

std::vector<BIOSCheckResult> bios_check(jsm::systems sys, const std::string& dir);

bool bios_ok_to_launch(jsm::systems sys, const std::string& dir);

bool bios_load(jsm::systems sys, const std::string& dir, multi_file_set& mfs,
               std::vector<std::string>& working_paths_out);
