#pragma once

#include "helpers/physical_io.h"
#include "helpers/int.h"

namespace DREAMCAST::MAPLE {

    struct PORT;
}

enum CSTATE {
    cstate_wait = 0,
    cstate_recv_cmd = 1,
    cstate_transmit = 2
};

enum COMMANDS : u32 {
    CMD_host_device_info_request = 0x01, // expect 05
    CMD_host_extended_device_info_request = 0x02, // expect 06
    CMD_host_reset = 0x03, // expect 07
    CMD_host_shutdown = 0x04, // expect 07
    CMD_guest_device_info = 0x05,
    CMD_guest_extended_device_info = 0x06,
    CMD_guest_ack = 0x07,
    CMD_guest_data_transfer = 0x08,
    CMD_host_get_condition = 0x09, // expect 0x08
    CMD_host_get_merory_info = 0x0A, // expect 0x08
    CMD_host_block_read = 0x0B, // expect 0x08
    CMD_host_block_write = 0x0C, // expect 0x07
    CMD_host_get_last_error = 0x0D, // expect 0x07
    CMD_host_set_condition = 0x0E, // expect 0x07
    CMD_guest_AR_error = 0xF9,
    CMD_guest_LCD_error = 0xFA,
    CMD_guest_file_error = 0xFB,
    CMD_request_resend = 0xFC,
    CMD_guest_unknown = 0xFD,
    CMD_guest_function_code_not_supported = 0xFE
};

union FRAME_WORD {
    struct {
        COMMANDS cmd : 8;
        u32 receiver_addr : 8;
        u32 sender_addr : 8;
        u32 num_words : 8;
    };
    u32 u{};
};

union LOCATION_WORD {
    struct {
        u32 block : 16;
        u32 phase : 8;
        u32 partition : 8;
    };
    u32 u{};
};


enum FUNCTION_CODES {
    FC_controller = 0x01,
    FC_storage = 0x02,
    FC_screen = 0x04,
    FC_timer = 0x08,
    FC_mic = 0x10,
    FC_AR_gun = 0x20,
    FC_keyboard = 0x40,
    FC_gun = 0x80,
    FC_vibration = 0x100,
    FC_mouse = 0x200
};

namespace DREAMCAST::PERIPHERAL {
struct CONTROLLER {
    physical_io_device *pio{};

    void setup_pio(physical_io_device *d, u32 num, const char*name, u32 connected);
    void connect_to_port(MAPLE::PORT *p);

    void write(u32 val);
    u32 read(bool &more);

    void reset() { waiting_for_device_info = true; }
    void cmd_device_info();
    void cmd_get_condition();
    void cmd_reset();
    void send_buffer(COMMANDS cmd, void *ptr, u32 num_words);

    bool waiting_for_device_info{true};
    FRAME_WORD fw{};
    u32 tx_len{};
    CSTATE state{};
    char buf[2048];
    u8 buf_index{};
    void cmd();

    u8 my_address{0x20};
    u8 recv_addr = 0x00;

    u8 cur_CMD{};
    u8 CRC{};
};

}