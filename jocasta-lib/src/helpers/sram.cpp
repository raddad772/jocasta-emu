//
// Created by . on 11/26/24.
//

#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "sram.h"

persistent_store::~persistent_store()
{
    if (ready_to_use && dirty && requested_size > 0){
        printf("\nWARNING: DELETING DIRTY PERSISTENT STORE!");
    }
    ready_to_use = false;

}
