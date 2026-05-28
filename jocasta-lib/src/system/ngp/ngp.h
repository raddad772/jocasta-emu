#pragma once

#include "helpers/int.h"
#include "helpers/sys_interface.h"

jsm_system *ngp_new(jsm::systems variant);
void ngp_delete(jsm_system *sys);