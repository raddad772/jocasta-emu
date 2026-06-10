//
// Created by . on 1/19/25.
//

#include <cstdio>
#include "arm32.h"
namespace ARM32 {
    namespace CP15 {
enum regs {
    unknown,
    main_id,
    cache_type_and_size,
    TCM_physical_size,
    control_register,
    PU_cacheability_data_unified_PR,
    PU_cacheability_instruction_PR,
    PU_cache_write_buffer_ability_data_PR,
    PU_access_permission_data_unified_PR,
    PU_access_permission_instruction_PR,
    PU_extended_access_permission_data_unified_PR,
    PU_extended_access_permission_instruction_PTR,
    PU_data_unified_0_7,
    PU_instruction_0_7,
    cache_commands_and_halt,
    cache_data_lockdown,
    cache_instruction_lockdown,
    TCM_data_TCM_base_and_virtual_size,
    TCM_instruction_TCM_base_and_virtual_size,
    misc_process_ID,
    misc_implementation_defined
};
}
static CP15::regs get_register(u32 opcode, u32 cn, u32 cm, u32 cp)
{
#define CP(name, opce, cne, cme, cpe) if ((opcode == opce) && (cn == cne) && (cm == cme) && (cp == cpe)) return CP15::name
    CP(main_id, 0, 0, 0, 0);
    CP(cache_type_and_size, 0, 0, 0, 1);
    CP(TCM_physical_size, 0, 0, 0, 2);
    CP(control_register, 0, 1, 0, 0);
    CP(PU_cacheability_data_unified_PR, 0, 2, 0, 0);
    CP(PU_cacheability_instruction_PR, 0, 2, 0, 1);
    CP(PU_cache_write_buffer_ability_data_PR, 0, 3, 0, 0);
    CP(PU_access_permission_data_unified_PR, 0, 5, 0, 0);
    CP(PU_access_permission_instruction_PR, 0, 5, 0, 1);
    CP(PU_extended_access_permission_data_unified_PR, 0, 5, 0, 2);
    CP(PU_extended_access_permission_instruction_PTR, 0, 5, 0, 3);
    if ((opcode == 0) && (cn == 6) && (cm < 7) && (cp == 0)) return CP15::PU_data_unified_0_7;
    if ((opcode == 0) && (cn == 6) && (cm < 7) && (cp == 1)) return CP15::PU_instruction_0_7;
    if ((opcode == 0) && (cn == 7)) return CP15::cache_commands_and_halt;
    CP(cache_data_lockdown, 0, 9, 0, 0);
    CP(cache_instruction_lockdown, 0, 9, 0, 1);
    CP(TCM_data_TCM_base_and_virtual_size, 0, 9, 1, 0);
    CP(TCM_instruction_TCM_base_and_virtual_size, 0, 9, 1, 1);
    if ((opcode == 0) && (cn == 13)) return CP15::misc_process_ID;
    if ((opcode == 0) && (cn == 15)) return CP15::misc_implementation_defined;
#undef CP
    return CP15::unknown;
}

u32 NDS_CP15::ptr_read(void *ptr, u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP) {
    auto *th = static_cast<NDS_CP15 *>(ptr);
    return th->read(num, opcode, Cn, Cm, CP);
}

bool NDS_CP15::ptr_write(void *ptr, u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP, u32 val) {
    auto *th = static_cast<NDS_CP15 *>(ptr);
    return th->write(num, opcode, Cn, Cm, CP, val);
}

u32 NDS_CP15::read(u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP) {
    if (num != 15) {
        printf("\n!BAD! CP.r:%d opcode:%d Cn:%d Cm:%d CP:%d", num, opcode, Cn, Cm, CP);
        return 0;
    }

    // Register is selected by <cpopc>,Cn,Cm,<cp>
    CP15::regs cpreg = get_register(opcode, Cn, Cm, CP);

    u32 v = 0;
    switch(cpreg) {
        case CP15::main_id:
            return 0x41059461; // ARM946ES
        case CP15::cache_type_and_size:
            return 0x0F0D2112;
        case CP15::TCM_physical_size:
            return 0x00140180;
        case CP15::control_register:
            return regs.control.u;
        case CP15::PU_cacheability_data_unified_PR:
            return regs.pu_data_cacheable;
        case CP15::PU_cacheability_instruction_PR:
            return regs.pu_instruction_cacheable;
        case CP15::PU_cache_write_buffer_ability_data_PR:
            return regs.pu_data_cached_write;
        case CP15::PU_access_permission_data_unified_PR:
            v = regs.pu_data_rw & 0x00000003;
            v |= (regs.pu_data_rw & 0x00000030) >> 2;
            v |= (regs.pu_data_rw & 0x00000300) >> 4;
            v |= (regs.pu_data_rw & 0x00003000) >> 6;
            v |= (regs.pu_data_rw & 0x00030000) >> 8;
            v |= (regs.pu_data_rw & 0x00300000) >> 10;
            v |= (regs.pu_data_rw & 0x03000000) >> 12;
            v |= (regs.pu_data_rw & 0x30000000) >> 14;
            return v;
        case CP15::PU_access_permission_instruction_PR:
            v = regs.pu_code_rw & 0x00000003;
            v |= (regs.pu_code_rw & 0x00000030) >> 2;
            v |= (regs.pu_code_rw & 0x00000300) >> 4;
            v |= (regs.pu_code_rw & 0x00003000) >> 6;
            v |= (regs.pu_code_rw & 0x00030000) >> 8;
            v |= (regs.pu_code_rw & 0x00300000) >> 10;
            v |= (regs.pu_code_rw & 0x03000000) >> 12;
            v |= (regs.pu_code_rw & 0x30000000) >> 14;
            return v;
        case CP15::PU_extended_access_permission_data_unified_PR:
            return regs.pu_data_rw;
        case CP15::PU_extended_access_permission_instruction_PTR:
            return regs.pu_code_rw;
        case CP15::PU_instruction_0_7:
        case CP15::PU_data_unified_0_7:
            return regs.pu_region[Cm];
        case CP15::TCM_data_TCM_base_and_virtual_size:
            return regs.dtcm_setting;
        case CP15::TCM_instruction_TCM_base_and_virtual_size:
            return regs.itcm_setting;
        case CP15::misc_process_ID:
            return regs.trace_process_id;
        case CP15::misc_implementation_defined:
            return 0;

        default:
            break;
    }

    printf("\nUNHANDLED CP.r:15 opcode:%d Cn:%d Cm:%d CP:%d", opcode, Cn, Cm, CP);
    return 0;
}

void NDS_CP15::update_dtcm()
{
    if (!regs.control.dtcm_enable) {
        dtcm.size = dtcm.mask = 0;
        dtcm.base_addr = 0xFFFFFFFF;
        dtcm.end_addr = 0xFFFFFFFF;
    }
    else {
        dtcm.size = 0x200 << ((regs.dtcm_setting >> 1) & 0x1F);
        if (dtcm.size < 0x1000) dtcm.size = 0x1000;
        const u32 mask = 0xFFFFF000 & ((dtcm.size - 1) ^ 0xFFFFFFFF);
        dtcm.base_addr = regs.dtcm_setting & mask;
        dtcm.end_addr = dtcm.base_addr + dtcm.size;
        dtcm.mask = dtcm.size > NDS_DTCM_SIZE ? NDS_DTCM_SIZE : dtcm.size;
        dtcm.mask--;
    }
#ifdef DBG_TCM
    printf("\nDTCM enable:%d base_addr:%08x end_addr:%08x size:%04x", regs.control.dtcm_enable, dtcm.base_addr, dtcm.end_addr, dtcm.size);
#endif
}

void NDS_CP15::update_itcm()
{
    if (!regs.control.itcm_enable) {
        itcm.size = 0;
    }
    else {
        itcm.size = 0x200 << ((regs.itcm_setting >> 1) & 0x1F);
        itcm.mask = itcm.size > NDS_ITCM_SIZE ? NDS_ITCM_SIZE : itcm.size;
        itcm.mask--;
    }
    itcm.end_addr = itcm.size;
#ifdef DBG_TCN
    printf("\nITCM enable:%d base_addr:%08x end_addr:%08x size:%04x", regs.control.itcm_enable, itcm.base_addr, itcm.end_addr, itcm.size);
#endif
}

bool NDS_CP15::write(u32 num, u32 opcode, u32 Cn, u32 Cm, u32 CP, u32 val)
{
    if (num != 15) {
        printf("\n!BAD! CP.w:%d opcode:%d Cn:%d Cm:%d CP:%d val:%08x", num, opcode, Cn, Cm, CP, val);
        return false;
    }

    CP15::regs cpreg = get_register(opcode, Cn, Cm, CP);

    switch(cpreg) {
        case CP15::control_register: {
            // 42078
            regs.control.u = (regs.control.u & (~0xFF085)) | (val & 0xFF085);
            update_dtcm();
            update_itcm();
            *EBR = 0xFFFF0000 * ((val >> 13) & 1);
            return false; }
        case CP15::PU_cacheability_data_unified_PR:
            regs.pu_data_cacheable = val;
            return false;
        case CP15::PU_cacheability_instruction_PR:
            regs.pu_instruction_cacheable = val;
            return false;
        case CP15::PU_cache_write_buffer_ability_data_PR:
            regs.pu_data_cached_write = val;
            return false;
        case CP15::PU_access_permission_data_unified_PR:
            regs.pu_data_rw = val & 0x0003;
            regs.pu_data_rw |= (val & 0x000C) << 2;
            regs.pu_data_rw |= (val & 0x0030) << 4;
            regs.pu_data_rw |= (val & 0x00C0) << 6;
            regs.pu_data_rw |= (val & 0x0300) << 8;
            regs.pu_data_rw |= (val & 0x0C00) << 10;
            regs.pu_data_rw |= (val & 0x3000) << 12;
            regs.pu_data_rw |= (val & 0xC000) << 14;
            return false;
        case CP15::PU_access_permission_instruction_PR:
            regs.pu_code_rw = val & 0x0003;
            regs.pu_code_rw |= (val & 0x000C) << 2;
            regs.pu_code_rw |= (val & 0x0030) << 4;
            regs.pu_code_rw |= (val & 0x00C0) << 6;
            regs.pu_code_rw |= (val & 0x0300) << 8;
            regs.pu_code_rw |= (val & 0x0C00) << 10;
            regs.pu_code_rw |= (val & 0x3000) << 12;
            regs.pu_code_rw |= (val & 0xC000) << 14;
            return false;
        case CP15::PU_extended_access_permission_data_unified_PR:
            regs.pu_data_rw = val;
            return false;
        case CP15::PU_extended_access_permission_instruction_PTR:
            regs.pu_code_rw = val;
            return false;
        case CP15::PU_data_unified_0_7:
        case CP15::PU_instruction_0_7:
            regs.pu_region[Cm] = val;
            return false;
        case CP15::cache_commands_and_halt: // 04, 82
            if (((Cm == 0) && (CP == 4)) || ((Cm == 8) && (CP == 2))) {
                //printf("\nHALT ARM9");
                *halt = true;
                return true;
            }
            if ((Cm == 5) && ((CP == 0) || (CP == 1) || (CP == 2))) {
                // Invalidate all i cache, invalidate by address, ???
                return false;
            }
            if ((Cm == 6) && ((CP == 0) || (CP == 1))) {
                // Invalidate all d cache, invalidate by address
                return false;
            }
            if ((Cm == 10 ) && ((CP == 1) || (CP == 2))) {
                // Flush d cache by val, all
                return false;
            }
            return false;
        case CP15::TCM_data_TCM_base_and_virtual_size:
            regs.dtcm_setting = val & 0xFFFFF03E;
            update_dtcm();
            return false;
        case CP15::TCM_instruction_TCM_base_and_virtual_size:
            regs.itcm_setting = val & 0x3E;
            update_itcm();
            return false;
        case CP15::misc_process_ID:
            regs.trace_process_id = val;
            return false;
        case CP15::misc_implementation_defined:
            return false;
        default:
            break;
    }
    if ((opcode == 0) && (Cn==6) && (Cm == 7) && (CP==0)) {
        // IGNORE!
        // this'll never bite me in the butt!
        return false;
    }
    printf("\nUNHANDLED CP.w:15 opcode:%d Cn:%d Cm:%d CP:%d val:%08x", opcode, Cn, Cm, CP, val);
    return false;
}

void NDS_CP15::reset()
{
    regs.control.u = 0x2078; // MelonDS says this

    rng_seed = 44203;
    regs.trace_process_id = 0;

    regs.dtcm_setting = 0;
    regs.itcm_setting = 0;

    itcm.size = 0;
    itcm.base_addr = 0;
    itcm.end_addr = 0;
    dtcm.base_addr = 0xFFFFFFFF;
    dtcm.end_addr = 0xFFFFFFFF;
    dtcm.mask = 0;

    regs.pu_instruction_cacheable = 0;
    regs.pu_data_cacheable = 0;
    regs.pu_data_cached_write = 0;

    regs.pu_code_rw = 0;
}

void NDS_CP15::direct_boot()
{
    write(15, 0, 1, 0, 0, 0x0005707D);
    write(15, 0, 9, 1, 0, 0x0300000A);
    write(15, 0, 9, 1, 1, 0x00000020);
}

void NDS_CP15::serialize(serialized_state &state)
{
#define S(x) Sadd(state, &( x), sizeof( x))
    S(regs.control.u);
    S(regs.pu_data_cacheable);
    S(regs.pu_instruction_cacheable);
    S(regs.pu_data_cached_write);
    S(regs.pu_data_rw);
    S(regs.pu_code_rw);
    S(regs.pu_region);
    S(regs.dtcm_setting);
    S(regs.itcm_setting);
    S(regs.trace_process_id);
    S(rng_seed);
    S(dtcm.data);
    S(dtcm.size);
    S(dtcm.base_addr);
    S(dtcm.end_addr);
    S(dtcm.mask);
    S(itcm.data);
    S(itcm.size);
    S(itcm.base_addr);
    S(itcm.end_addr);
    S(itcm.mask);
#undef S
}

void NDS_CP15::deserialize(serialized_state &state)
{
#define L(x) Sload(state, &( x), sizeof( x))
    L(regs.control.u);
    L(regs.pu_data_cacheable);
    L(regs.pu_instruction_cacheable);
    L(regs.pu_data_cached_write);
    L(regs.pu_data_rw);
    L(regs.pu_code_rw);
    L(regs.pu_region);
    L(regs.dtcm_setting);
    L(regs.itcm_setting);
    L(regs.trace_process_id);
    L(rng_seed);
    L(dtcm.data);
    L(dtcm.size);
    L(dtcm.base_addr);
    L(dtcm.end_addr);
    L(dtcm.mask);
    L(itcm.data);
    L(itcm.size);
    L(itcm.base_addr);
    L(itcm.end_addr);
    L(itcm.mask);
#undef L

    update_dtcm();
    update_itcm();
}
}
