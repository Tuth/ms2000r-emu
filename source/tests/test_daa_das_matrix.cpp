#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>

using MS2000::H8S2350Emulator;

static void emit2(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1){
    emu.writeByte(pc+0,b0); emu.writeByte(pc+1,b1);
}

static uint64_t run(H8S2350Emulator& emu, uint32_t pc, uint8_t b0, uint8_t b1){
    auto &r=emu.getRegisters(); r.pc=pc; auto c0=emu.getCycles(); emit2(emu,pc,b0,b1); emu.step(); return emu.getCycles()-c0;
}

int main(){
    std::puts("[DAA/DAS-MATRIX] start");
    // Case set 1: lower nibble boundary 0x09 -> 0x0A with H=0, C=0
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=0; r.r[rd]=(r.r[rd]&0xFF00)|0x09; f.carry=false; f.half_carry=false; f.overflow=false;
        uint64_t cyc = run(emu,0x1000,0x0F,rd); // DAA.B R0L
        uint8_t res = (uint8_t)(r.r[rd]&0xFF);
        assert(res == 0x15); // 0x09 + 0x06 correction
        assert(f.zero == false && f.negative == false);
        // C preserved (no high nibble carry), H/V preserved
        assert(f.carry == false); assert(f.half_carry==false); assert(f.overflow==false);
        assert(cyc == 1);
    }
    // Case set 2: upper nibble boundary 0x90 -> adjust +0x60 when C_in=1
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=1; r.r[rd]=(r.r[rd]&0xFF00)|0x90; f.carry=true; f.half_carry=false; f.overflow=true;
        run(emu,0x1000,0x0F,rd);
        uint8_t res=(uint8_t)(r.r[rd]&0xFF);
        assert(res == (uint8_t)(0x90+0x60)); // 0xF0
        // C remains 1 (carry occurred), H/V preserved per manual
        assert(f.carry == true && f.half_carry==false && f.overflow==true);
    }
    // Case set 3: DAS should preserve incoming C and H/V
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=2; r.r[rd]=(r.r[rd]&0xFF00)|0x9A; f.carry=false; f.half_carry=true; f.overflow=true;
        run(emu,0x2000,0x1F,rd); // DAS.B R2L
        uint8_t res=(uint8_t)(r.r[rd]&0xFF);
        // 0x9A - 0x60 = 0x3A (borrow_in true since C_in=0)
        assert(res == 0x3A);
        assert(f.carry == false && f.half_carry==true && f.overflow==true);
        assert(f.zero == false);
    }
    // Case set 4: DAA no correction when 0x12, flags preserved for C/H/V
    {
        H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        uint8_t rd=3; r.r[rd]=(r.r[rd]&0xFF00)|0x12; f.carry=false; f.half_carry=false; f.overflow=true;
        run(emu,0x3000,0x0F,rd);
        uint8_t res=(uint8_t)(r.r[rd]&0xFF);
        assert(res==0x12);
        assert(f.carry==false && f.half_carry==false && f.overflow==true);
        assert(f.zero==false && f.negative==false);
    }
    std::puts("[DAA/DAS-MATRIX] all ok");
    return 0;
}

