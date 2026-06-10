//
// Created by . on 8/29/24.
//

#pragma once

#include "helpers/int.h"
#include "helpers/sys_interface.h"

jsm_system *apple2_new(const system_config& cfg = {});
void apple2_delete(jsm_system* system);

