#pragma once

#include "helpers/int.h"

namespace DREAMCAST {
struct core;
enum G2_DMA_channel_name {
    G2D_AICA = 0,
    G2D_Ext1 = 1,
    G2D_Ext2 = 2,
    G2D_Dev = 3
};

struct G2_DMA_channel {
    core *bus{};
    G2_DMA_channel_name subsystem{};
    u32 num{};
    bool DREQ{}; // Hardware DMA request

    struct {
        long double next_cycle{};
        double cycle_surplus{};
        bool suspended{};

        u32 sch_id{}, still_sch{};
    } transfer_32{};

    u64 read_reg(u32 addr, u8 sz, bool *success);
    u64 read_counter(u32 addr, u8 sz, bool *success);
    void write_reg(u32 addr, u8 sz, u64 val, bool *success);
    u32 read_suspend();
    void write_suspend(u32 val);
    [[nodiscard]] bool running() const;

    void schedule_transfer_32();
    [[nodiscard]] bool ST_allowed() const;
    [[nodiscard]] bool DREQ_allowed() const;
    [[nodiscard]] bool IRQ_allowed() const;
    void try_run();
    void irq_trigger();
    void run();
    void do_transfer_32(); // G2 DMA's are 32 byte blocks

    struct {
        // 800, 820, 840, 860
        u32 STAG{}; // Internal-to-subsystem start address
        u32 STAR{}; // System RAM start address
        union {
            struct {
                u32 _res : 5;
                u32 len : 20; // =25
                u32 _res2 : 6; // =31
                u32 disable_on_end : 1; // =32
            };
            u32 u{};
        } LEN{}; // DMA length

        u32 DIR{}; // Direction. 0=RAM to Device. 1=Device to RAM
        union {
            struct {
                u32 external_pin_enable : 1; // 0=no external pin, 1=external pin
                u32 hardware_trigger : 1; // 0= CPU, 1= Hardware
                u32 enable_suspend : 1;
            };
            u32 u{};
        } TSEL{}; // Trigger select
        u32 EN{}; // Enable
        u32 ST{}; // Start
        u32 SUSP{}; // Suspend

        // ...C0, D0, E0
        u32 STAGD{}; // address counter local
        u32 STARD{}; // address counter on root bus
        u32 LEND{}; // transfer counter
    } io{};
};

struct G2 {
    explicit G2(core *parent);
    core *bus;
    void reset();
    void write_io(u32 addr, u8 sz, u64 val, bool *success);
    u64 read_io(u32 addr, u8 sz, bool *success);
    void dma_irq_trigger();

    G2_DMA_channel channel[4]{};

    u32 SB_G2DSTO{};  // 0x005F7890
    u32 SB_G2TRTO{};  // 0x005F7894
    u32 SB_G2MDMTO{};  // 0x005F7898
    u32 SB_G2MDMW{};  // 0x005F789C
    u32 UKN005F78A0{};  // 0x005F78A0
    u32 UKN005F78A4{};  // 0x005F78A4
    u32 UKN005F78A8{};  // 0x005F78A8
    u32 UKN005F78AC{};  // 0x005F78AC
    u32 UKN005F78B0{};  // 0x005F78B0
    u32 UKN005F78B4{};  // 0x005F78B4
    u32 UKN005F78B8{};  // 0x005F78B8
    union {  // SB_G2APRO
        struct {
            u32 bottom_address : 7;
            u32 : 1;
            u32 top_address : 7;
        };
        u32 u{};
    } SB_G2APRO;  // 0x005F78BC
};
}