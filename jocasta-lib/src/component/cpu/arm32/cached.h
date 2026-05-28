#pragma once

#define TVOID template <armtype cpukind, typename scheduler_kind>void core<cpukind, scheduler_kind>::
#define TVOIDD template <armtype cpukind, typename scheduler_kind> template<bool do_debug> void core<cpukind, scheduler_kind>::
#define PC R[15]

//#define cached_printf(...) printf(__VA_ARGS__)
#define cached_printf(...) (void)0

TVOID set_current_cached_ins_ends_block() {
    current_ins->ends_block = true;
}

TVOID cached_block_destruct(void *ptr) {
    auto *bl = static_cast<cached_block_t *>(ptr);
    bl->~cached_block_t();
}

template<armtype cpukind, typename scheduler_kind>
template<bool do_debug>
void *core<cpukind, scheduler_kind>::get_cached_block_itcm(u32 addr) {
    u32 tcm_addr = (addr - nds_cp15.itcm.base_addr) & nds_cp15.itcm.mask;
    return arm9_block_cache.template get_block<0>(tcm_addr, 0);
}

template<armtype cpukind, typename scheduler_kind>
template<bool do_debug>
void *core<cpukind, scheduler_kind>::fetch_cached_block(u32 addr) {
    if constexpr(cpukind >= AT_ARM946ES) {
        if (addr_in_itcm(addr) && nds_cp15.regs.control.itcm_enable && !nds_cp15.regs.control.itcm_load_mode) {
            return get_cached_block_itcm<do_debug>(addr);
        }
    }
    if constexpr(do_debug) return get_cached_block_debug(cached_block_ptr, addr);
    else return get_cached_block(cached_block_ptr, addr);
}


template <armtype cpukind, typename scheduler_kind> template<bool do_debug>
core<cpukind, scheduler_kind>::cached_block_t *core<cpukind, scheduler_kind>::get_next_block() {
    cached_printf("\n\nEXEC NEXT BLOCK. PC:%08x", regs.PC);
    u32 ins_size = regs.CPSR.T ? 2 : 4;

    u32 fetch_addr = regs.PC - (ins_size * 2);
    auto *bl = static_cast<cached_block_t *>(fetch_cached_block<do_debug>(fetch_addr));

    if (bl->instructions.size() > 0) {
        assert(static_cast<i32>(bl->instructions.size()) > 0);

        cached_printf("\nRETURN BLOCK OF %ld INSTRUCTIONS", bl->instructions.size());
        return bl;
    }
    cached_printf("\nNO INSTRUCTIONS %ld", bl->instructions.size());

    if (regs.CPSR.T) compile_cached_block_into<do_debug, true>(fetch_addr, bl);
    else compile_cached_block_into<do_debug, false>(fetch_addr, bl);
    if constexpr(cpukind == AT_ARM946ES) {
        if (addr_in_itcm(fetch_addr) && nds_cp15.regs.control.itcm_enable && !nds_cp15.regs.control.itcm_load_mode) {
            arm9_block_cache.template commit<0>(bl);
        }
        else {
            register_cached_block(cached_block_ptr, bl);
        }
    }
    else register_cached_block(cached_block_ptr, bl);
    return bl;
}

template <armtype cpukind, typename scheduler_kind>
template<u8 sz, bool do_debug>
u32 core<cpukind, scheduler_kind>::cached_compile_get_ins(u32 addr) {
    if constexpr (cpukind >= AT_ARM946ES) {
        if (addr_in_itcm(addr) && nds_cp15.regs.control.itcm_enable && !nds_cp15.regs.control.itcm_load_mode) {
            return read_itcm<sz, true>(addr);
        }
    }
    if constexpr(do_debug) {
        if constexpr(sz == 2) return fetch_ins_func16_peek_debug(fetch_ptr, addr, 0);
        if constexpr(sz == 4) return fetch_ins_func32_peek_debug(fetch_ptr, addr, 0);
    }
    else {
        if constexpr(sz == 2) return fetch_ins_func16_peek(fetch_ptr, addr, 0);
        if constexpr(sz == 4) return fetch_ins_func32_peek(fetch_ptr, addr, 0);
    }
    NOGOHERE;
}

template <armtype cpukind, typename scheduler_kind>
template<bool do_debug, bool is_THUMB>
void core<cpukind, scheduler_kind>::compile_cached_block_into(u32 fetch_addr, cached_block_t *bl) {
    bl->instructions.clear();

    u32 ins_sz = is_THUMB ? 2 : 4;

    for (u32 block_size = 0; block_size < cached_max_block_size; block_size++) {
        auto &ins = bl->instructions.emplace_back();

        if constexpr (is_THUMB) {
            ins.opcode = cached_compile_get_ins<2, do_debug>(fetch_addr);
            cached_printf("\nTHUMB opocde %04x @%08x:", ins.opcode, fetch_addr);
            cached_decode_thumb32<true, true>(ins.opcode, ins);
            if (ins.ends_block) break;
        } else {
            ins.opcode = cached_compile_get_ins<4, do_debug>(fetch_addr);
            cached_printf("\nARM opocde %08x @%08x:", ins.opcode, fetch_addr);
            cached_decode_arm32<true>(ins.opcode, ins);
            if (ins.ends_block && ins.cc == 14) break;
        }

        fetch_addr += ins_sz;
    }
    bl->sz = bl->instructions.size() * ins_sz;
}

template<armtype cpukind, typename scheduler_kind> template<bool do_debug, bool is_THUMB, bool do_IRQ_check, bool IRQ_check_param, bool check_halt_bool>
void core<cpukind, scheduler_kind>::execute_cached_block(cached_block_t *block) {
    u32 addr;
    if constexpr(do_debug) {
        if constexpr(is_THUMB) addr = regs.PC - 4;
        else addr = regs.PC - 8;
    }
    for (u32 i = 0; i < block->instructions.size(); i++) {
        current_ins = &block->instructions[i];
        if constexpr(do_IRQ_check) {
            if (IRQcheck<do_debug, true, IRQ_check_param>())
                break;
        }
        regs.PC &= 0xFFFFFFFE;
        if constexpr(is_THUMB) {
            // Emulate instruction fetch
            (*waitstates) += ins_timing16(ins_timing_ptr, regs.PC, pipeline.access);

            bool branch_taken;
            if constexpr(do_debug) {
                arm_trace_format(current_ins->opcode, addr, true, true);
                addr += 2;
                branch_taken = (this->*(this->current_ins->exec_debug))(*current_ins);
            }
            else branch_taken = (this->*(this->current_ins->exec))(*current_ins);
            if (branch_taken) {
                break;
            }
        }
        else {
            // Emulate instruction fetch
            (*waitstates) += ins_timing32(ins_timing_ptr, regs.PC, pipeline.access);
            if (condition_passes(current_ins->cc)) {
                bool branch_taken;
                if constexpr(do_debug) {
                    cached_printf("\nRUN.ARM INS%d @%08x opcode %08x", i, addr, current_ins->opcode);
                    arm_trace_format(current_ins->opcode, addr, false, true);
                    addr += 4;
                    branch_taken = (this->*(this->current_ins->exec_debug))(*current_ins);
                }
                else branch_taken = (this->*(this->current_ins->exec))(*current_ins);
                if (branch_taken) { // Branch that executed, write to certain memory, etc.
                    break;
                }
            }
            else {
                if constexpr(do_debug) {
                    arm_trace_format(current_ins->opcode, addr, false, false);
                    addr += 4;
                }
                regs.PC += 4;
                pipeline.access = ARM32P_code | ARM32P_sequential;
            }
        }
        if constexpr(check_halt_bool) {
            if (halted && cached_cycles_left > *waitstates) {
                (*waitstates) = cached_cycles_left;
                return;
            }
        }
    }
}

template<armtype cpukind, typename scheduler_kind>
template<bool do_debug, bool do_IRQ_check, bool IRQ_check_param, bool check_halt_bool>
void core<cpukind, scheduler_kind>::cached_run(i32 num_cycles) {
    cached_cycles_left += num_cycles;
    if constexpr(check_halt_bool) {
        if (halted) {
            if (cached_cycles_left > 0) {
                (*master_clock) += (*waitstates) + cached_cycles_left;
                (*waitstates) = 0;
                cached_cycles_left = 0;
            }
            return;
        }
    }

    (*master_clock) += *waitstates;
    (*waitstates) = 0;

    while (cached_cycles_left > 0) {
        cached_block_t *block = get_next_block<do_debug>();
        if (regs.CPSR.T) execute_cached_block<do_debug, true, do_IRQ_check, IRQ_check_param, check_halt_bool>(block);
        else execute_cached_block<do_debug, false, do_IRQ_check, IRQ_check_param, check_halt_bool>(block);
        cached_cycles_left -= (*waitstates);
        (*master_clock) += *waitstates;
        (*waitstates) = 0;
    }
}

#undef TVOID
#undef TVOIDD
#undef PC
#undef cached_printf