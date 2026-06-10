//
// Created by . on 4/13/26.
//

#include <cstring>
#include "helpers/multisize_memaccess.cpp"

#include "dc_controller.h"
#include "dc_bus.h"

namespace DREAMCAST::PERIPHERAL {
void CONTROLLER::setup_pio(physical_io_device *d, u32 num, const char*name, u32 connected)
{
    d->init(HID_CONTROLLER, 0, 0, 1, 1);

    snprintf(d->controller.name, sizeof(d->controller.name), "%s", name);
    d->id = num;
    d->kind = HID_CONTROLLER;
    d->connected = connected;
    d->enabled = connected;

    JSM_CONTROLLER* cnt = &d->controller;

    // up down left right a b start select. in that order
    pio_new_button(cnt, "up", DBCID_co_up);
    pio_new_button(cnt, "down", DBCID_co_down);
    pio_new_button(cnt, "left", DBCID_co_left);
    pio_new_button(cnt, "right", DBCID_co_right);
    pio_new_button(cnt, "a", DBCID_co_fire1);
    pio_new_button(cnt, "b", DBCID_co_fire2);
    pio_new_button(cnt, "x", DBCID_co_fire4);
    pio_new_button(cnt, "y", DBCID_co_fire5);
    pio_new_button(cnt, "start", DBCID_co_start);
    pio = d;
}
    /*
    u32 (*read_device)(void*, u32*){};
    void (*write_device)(void *,u32){};*/

static u32 rdme(void *ptr, bool &more) {
    auto *th = static_cast<CONTROLLER *>(ptr);
    return th->read(more);
}

static void wrme(void *ptr, u32 val) {
    auto *th = static_cast<CONTROLLER *>(ptr);
    return th->write(val);
}


void CONTROLLER::write(u32 val) {
    //printf("\n\nCONTROLLER WRITE %08x", val);
    switch (state) {
        case cstate_wait:
            fw.u = val;
            //printf("\nDC controller WRITE size:%d words src:%02x dest:%02x val%02x", fw.num_words, fw.sender_addr, fw.receiver_addr, fw.cmd);
            if (fw.receiver_addr != my_address) {
                printf("\nCONTROLLER: BAD ADDRES %02x", fw.receiver_addr);
                return;
            }
            tx_len = fw.num_words;
            cW32(buf, 0, val);
            buf_index = 4;
            if (tx_len == 0)
                cmd();
            else
                state = cstate_recv_cmd;
            return;
        case cstate_recv_cmd:
            if (tx_len < 1) {
                printf("\nBAD CMD TX LEN == 0!");
                return;
            }
            cW32(buf, buf_index, val);
            buf_index += 4;
            tx_len--;
            if (tx_len == 0)
                cmd();
            return;
        case cstate_transmit:
            printf("\nUH OH WRITE DURING TRANSMIT!?");
            break;
    }
    printf("\nMAPLE: BAD STATE %d", state);
}

void CONTROLLER::cmd() {

    CRC = 0xFF;
    state = cstate_transmit;
    //printf("\nFW.u:%08x CMD:%02x", fw.u, fw.cmd);
    if (waiting_for_device_info) {
        if (fw.cmd == CMD_host_device_info_request) cmd_device_info();
        else printf("\nCONTROLLER: BAD CMD WHILE WAITING FOR DEVICE INFO %02x", fw.cmd);
        return;
    }
    switch (fw.cmd) {
        case CMD_host_device_info_request:
            cmd_device_info();
            return;
        case CMD_host_get_condition:
            cmd_get_condition();
            return;
        case CMD_host_reset:
            cmd_reset();
            return;
        default: break;
    }
    printf("\nUNKNOWN CMD %02x", fw.cmd);
}

void CONTROLLER::send_buffer(COMMANDS cmd, void *ptr, u32 num_words) {
    u32 *b32 = reinterpret_cast<u32 *>(ptr);
    memset(buf, 0, sizeof(buf));
    buf_index = 0;
    tx_len = num_words + 2;
    FRAME_WORD f{};
    f.receiver_addr = 0;
    f.sender_addr = my_address;
    f.num_words = num_words;
    f.cmd = cmd;
    CRC = 0;
    cW32(buf, 0, f.u);
    if (num_words > 0) {
        for (u32 i = 0; i < num_words; i++) {
            cW32(buf, 4 + (i * 4), b32[i]);
            //printf("\nCOPY %08x", b32[i]);
        }
    }
    for (u32 i = 0; i < (num_words * 4) + 4; i++) {
        CRC ^= (buf[i] >> (i * 8)) & 0xFF;
    }
    cW8(buf, 7 + (num_words * 4), CRC);
    state = cstate_transmit;
}

void CONTROLLER::cmd_reset() {
    reset();
    send_buffer(CMD_guest_ack, nullptr, 0);
}

void CONTROLLER::cmd_get_condition() {
    u32 b32[4];
    b32[0] = 0b1111111111111; // All the buttons on the controller
    b32[1] = 0; // No pressed buttons!
    b32[2] = 0x80808080; // 4 axiis center
    b32[3] = 0x8080; // 2 more axiis
    send_buffer(CMD_guest_data_transfer, b32, 4);
}

void CONTROLLER::cmd_device_info() {
    // 28 words 112 bytes
    //auto *b32 = reinterpret_cast<u32 *>(buf);
    waiting_for_device_info = false;
    u32 b32[28] = {};
    b32[0] = bswap_32(FC_controller);

    b32[1] = 0b1111111111111; // All the buttons on the controller
    b32[2] = 0;
    b32[3] = 0;
    b32[4] = (0 << 8) | 0xFF;
    strcpy(reinterpret_cast<char *>(b32) + 18, "Dreamcast Controller         ");
    strcpy(reinterpret_cast<char *>(b32) + 48, "Produced By or Under License From SEGA ENTERPRISES,LTD.    ");
    b32[27] = 0x01AE | (0x01F4 << 16);
    send_buffer(CMD_guest_device_info, b32, 28);
}

u32 CONTROLLER::read(bool &more) {
    if (state != cstate_transmit) {
        printf("\nWARN BAD TRANSMIT!?!?!");
    }
    u32 v = 0xFFFFFFFF;
    if (tx_len) {
        v = cR32(buf, buf_index);
        //printf("\nREAD FROM %d: %08x", buf_index, v);
        buf_index += 4;
        tx_len--;
        if (tx_len == 0) {
            more = false;
            state = cstate_wait;
        }
        else
            more = true;
    }
    else
        more = false;
    for (u32 i = 0; i < 4; i++) {
        CRC ^= (v >> (i * 8)) & 0xFF;
    }
    //printf("\nCONTROLLER READ %08x TXL:%d", v, tx_len);
    return v;
}

void CONTROLLER::connect_to_port(MAPLE::PORT *p) {
    p->device_ptr = this;
    p->device_kind = MAPLE::DK_CONTROLLER;
    p->read_device = &rdme;
    p->write_device = &wrme;
}

}