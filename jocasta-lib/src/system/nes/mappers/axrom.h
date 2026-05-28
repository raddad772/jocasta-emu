//
// Created by . on 9/29/24.
//

#ifndef JOCASTA_EMUS_AXROM_H
#define JOCASTA_EMUS_AXROM_H


struct AXROM : NES_mapper {
    explicit AXROM(NES::core *);

    void writecart(u32 addr, u32 val, u32 &do_write) override;
    u32 readcart(u32 addr, u32 old_val, u32 has_effect, u32 &do_read) override;
    void setcart(NES_cart &cart) override;
    void reset() override;

    void serialize(serialized_state &state) override;
    void deserialize(serialized_state &state) override;

    // Optional ones
    //void a12_watch(u32 addr) override;
    //void cpu_cycle() override;
    //float sample_audio() override;
    //virtual u32 PPU_read_override(u32 addr, u32 has_effect);
    //virtual void PPU_write_override(u32 addr, u32 val);

private:
    void remap();
    struct {
        u32 PRG_bank{};
        u32 nametable{};
    } io{};

};

#endif //JOCASTA_EMUS_AXROM_H
