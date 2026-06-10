#pragma once
#include "helpers/int.h"
#include "helpers/physical_io.h"
#include "helpers/cdrom_formats.h"
#include "helpers/cvec.h"

namespace TG16 { struct core; }

namespace TG16::CDROM {

struct ADPCM {
    u8 ram[65536]{};

    u16 address_port{};
    u16 write_addr{};
    u16 read_addr{};
    u32 length{};

    bool playing{};
    bool play_request{};
    bool nibble{};
    bool half_reached{};
    bool end_reached{};

    u8 control{};
    u8 dma_control{};
    u8 rate{};
    u8 read_buffer{};
    bool read_pending{};
    u64 read_done_clock{};

    i32 current_output{2048};
    u8 magnitude{};

    u32 frac{};
    u32 ring_frac{};
    float last_sample{};
};

struct ACChannel {
    u32 addr{};
    u8 offset{};
    u8 ctrl{};

    [[nodiscard]] u32 eff() const { return (addr + offset) & 0x1FFFFF; }

    void increment() {
        static constexpr u32 units[4] = { 1, 2, 128, 256 };
        addr = (addr + units[(ctrl >> 2) & 3]) & 0x1FFFFF;
    }
};


struct core {
    explicit core(TG16::core *parent);
    TG16::core *sys;

    u8 read(u32 addr, u8 old);
    void write(u32 addr, u8 val);

    void insert_disc(multi_file_set &mfs);
    void remove_disc();
    void open_drive();
    void close_drive();
    void reset();

    void get_audio(i16 &left, i16 &right);
    void get_cdda(i16 &l, i16 &r);

    void tick_adpcm();

    audio_output_ring *adpcm_ring{};

    void sch_exec(u64 key, u64 clock);
    void sch_read(u64 key, u64 clock);
    void sch_ack(u64 key, u64 clock);
    void sch_dma(u64 key, u64 clock);
    void sch_adpcm_read(u64 key, u64 clock);
    void schedule_ack(u64 delay);
    void reschedule_pending_events();

    void *irq_ptr{};
    void (*set_irq2)(void *ptr, bool val){};

    cvec_ptr<physical_io_device> pio_ptr{};
    JSM_DISC_DRIVE *dd{};

    CDROM_DISC disc{};
    struct { bool inserted{}, open{}; } drive{};

    enum PHASE { BUS_FREE, COMMAND, DATA_IN, STATUS, MESSAGE_IN };
    struct {
        PHASE phase{BUS_FREE};
        bool BSY{}, REQ{}, MSG{}, CD{}, IO{};

        u8 cmd[12]{};
        u32 cmd_len{}, cmd_pos{};

        u8 buf[4096]{};
        u32 buf_len{}, buf_pos{};
        u8 status_byte{};
        u32 remaining_sectors{};
        bool buf_peeked{};
    } scsi{};

    struct SLOT { u64 id{}; u32 active{}; };
    SLOT exec_slot{};
    SLOT read_slot{};
    SLOT ack_slot{};
    SLOT dma_slot{};
    SLOT adpcm_read_slot{};

    struct {
        u32 LBA{};
        u32 loop_start_lba{};
        u32 loop_end_lba{0xFFFFFFFF};
        u8 end_behavior{};
        u32 sample_idx{};
        bool playing{}, muted{};
        i16 last_l{}, last_r{};
    } cdda{};

    audio_output_ring *cdda_ring{};

    u32 read_LBA{};

    struct {
        u32 target_LBA{};
        bool needs_seek{};
    } seek{};

    struct {
        u8 active_irqs{};
        u8 enabled_irqs{};
    } irqc{};

    bool bram_unlocked{};
    u8 reset_reg{};

    struct {
        u8 reg{};
        bool enabled{};
        bool adpcm_target{};
        bool fast{};
        u64 start_clock{};
    } fader{};

    ADPCM adpcm{};

    ACChannel ac[4]{};

private:
    void update_irq();
    void set_irq_source(u8 bit);
    void clear_irq_source(u8 bit);
    void update_scsi_irq();

    void exec_scsi_cmd();
    void begin_response(const u8 *data, u32 len);
    void advance_phase();
    void scsi_ack();

    void read_sector();
    void next_sector();

    u32 get_audio_lba_pos(const u8 *cmd) const;
    i64 seek_cycles() const;
    void schedule_read(u64 clock);
    void schedule_exec(u64 delay);
    void schedule_dma(u64 delay);
    void schedule_adpcm_read(u64 clock);

    void adpcm_finish_pending_read();
    void adpcm_complete_read_if_due(u64 clock);
    void adpcm_set_control(u8 value);
    void adpcm_set_half_reached(bool value);
    void adpcm_set_end_reached(bool value);

    u8 scsi_pop();

    u8 ac_read(u32 addr, u8 old);
    void ac_write(u32 addr, u8 val);

    static u8 bcd_enc(u8 v) { return ((v / 10) << 4) | (v % 10); }
    static u8 bcd_dec(u8 v) { return ((v >> 4) * 10) + (v & 0xF); }
};

}
