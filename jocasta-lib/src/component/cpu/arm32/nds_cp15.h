//
// Created by . on 1/19/25.
//

#pragma once

#include "helpers/int.h"

struct serialized_state;

namespace ARM32 {

static constexpr u32 NDS_ITCM_SIZE = 0x8000;
static constexpr u32 NDS_DTCM_SIZE = 0x4000;
static constexpr u32 NDS_ITCM_MASK = NDS_ITCM_SIZE - 1;
static constexpr u32 NDS_DTCM_MASK = NDS_DTCM_SIZE - 1;

struct NDS_CP15 {
    explicit NDS_CP15(u32 *EBR_in, bool *halt_in) : EBR{EBR_in}, halt{halt_in} {}
    u32 read(u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP);
    bool write(u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP, u32 val);
    void reset();
    void direct_boot();
    void serialize(serialized_state &state);
    void deserialize(serialized_state &state);
    static u32 ptr_read(void *ptr, u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP);
    static bool ptr_write(void *ptr, u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP, u32 val);

    bool *halt{};
    u32 *EBR{};
    struct {
        union {
            struct {
                // Bits 0...7
                u32 mmu_pu_enable : 1;
                u32 alignment_fault_check : 1;
                u32 data_unified_cache: 1;
                u32 write_buffer : 1;
                u32 exception_handling : 1;
                u32 address_faults26 : 1;
                u32 abort_model : 1;
                u32 endian : 1;

                // Bits 8...15
                u32 system_protection : 1;  // 8
                u32 rom_protection : 1;     // 9
                u32 _imp1 : 1;              // 10
                u32 branch_prediction : 1;  // 11
                u32 instruction_cache : 1;  // 12
                u32 exception_vectors : 1;  // 13
                u32 cache_replacement: 1;   // 14
                u32 pre_armv5_mode : 1;     // 15

                // Bits 16-23
                u32 dtcm_enable : 1;            // 16
                u32 dtcm_load_mode : 1;         // 17
                u32 itcm_enable : 1;            // 18
                u32 itcm_load_mode : 1;         // 19
                u32 _res : 2;                // 20,21
                u32 unaligned_access : 1;    // 22
                u32 extended_page_table : 1; // 23


                // Bits 24-31
                u32 _res2: 1;               // 24
                u32 cpsr_e_on_exceptions: 1;// 25
                u32 _res3: 1;               // 26
                u32 fiq_behavior: 1;        // 27
                u32 tex_remap : 1;          // 28
                u32 force_ap : 1;           // 29
                u32 _res4: 2;               // 30{}, 31
            };
            u32 u{};
        }control{};
        u32 pu_data_cacheable{};
        u32 pu_instruction_cacheable{};
        u32 pu_data_cached_write{};
        u32 pu_data_rw{};
        u32 pu_code_rw{};
        u32 pu_region[8]{};
        u32 dtcm_setting{};
        u32 itcm_setting{};
        u32 trace_process_id{};
    } regs{};

    u32 rng_seed{};
    struct {
        u8 data[NDS_DTCM_SIZE]{};
        u32 size{}, base_addr{}, end_addr{}, mask{};
    } dtcm{};

    struct {
        u8 data[NDS_ITCM_SIZE]{};
        u32 size{}, base_addr{}, end_addr{}, mask{};
    } itcm{};
private:
    void update_itcm();
    void update_dtcm();
};

}
