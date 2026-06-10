//
// Created by . on 9/30/24.
//

#pragma once

namespace NES {

// Implements iNES mapper 003 (aka CGNROM_JF11_JF14_color_dreams and "similar")
// 8KB switchable CHR banks. That's pretty much all


struct GNROM_JF11_JF14_color_dreams : MAPPER {
    explicit GNROM_JF11_JF14_color_dreams(core *, mappers in_kind);

    void writecart(u32 addr, u32 val, u32 &do_write) override;
    u32 readcart(u32 addr, u32 old_val, u32 has_effect, u32 &do_read) override;
    void setcart(CART &cart) override;
    void reset() override;

    void serialize(serialized_state &state) override;
    void deserialize(serialized_state &state) override;

    // Optional ones
    //void a12_watch(u32 addr) override;
    //void cpu_cycle() override;
    //float sample_audio() override;
    //u32 PPU_read_override(u32 addr, u32 has_effect);
    //void PPU_write_override(u32 addr, u32 val);

private:
    void remap();
    mappers kind{};
    struct {
        u32 CHR_bank_num{};
        u32 PRG_bank_num{};
    } io{};

};

} // namespace NES
