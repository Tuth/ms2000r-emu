#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

using MS2000::H8S2350Emulator;

static void emit_rotx(H8S2350Emulator& emu, uint32_t pc, bool left, int width, bool two, uint8_t regIdx){
    // Primaries: ROTXL=0x12, ROTXR=0x13; sub-nibbles per manual
    uint8_t p = left ? 0x12 : 0x13;
    uint8_t b1=0, b2=0; bool has_b2=false;
    if (width==1){ // .B
        uint8_t sub = two ? 0x4 : 0x0;
        b1 = static_cast<uint8_t>((sub<<4) | (regIdx & 0x07));
    } else if (width==2){ // .W
        uint8_t sub = two ? 0x5 : 0x1;
        b1 = static_cast<uint8_t>((sub<<4) | (regIdx & 0x07));
    } else { // .L
        uint8_t sub = two ? 0x7 : 0x3;
        b1 = static_cast<uint8_t>((sub<<4) | 0x0);
        b2 = regIdx & 0x07; has_b2=true;
    }
    emu.writeByte(pc+0, p);
    emu.writeByte(pc+1, b1);
    if (has_b2) emu.writeByte(pc+2, b2);
}

template<typename T>
static T model_rotxl(T v, bool &carry, bool two){
    if (!two){ T out = (T)((v<<1) | (carry?1:0)); bool c = (v >> ((sizeof(T)*8)-1)) & 1; carry = c; return out; }
    // Two steps
    T a1 = (T)((v<<1) | (carry?1:0)); bool c1 = (v >> ((sizeof(T)*8)-1)) & 1;
    T a2 = (T)((a1<<1) | (c1?1:0)); bool c2 = (v >> ((sizeof(T)*8)-2)) & 1; carry = c2; return a2;
}
template<typename T>
static T model_rotxr(T v, bool &carry, bool two){
    if (!two){ T out = (T)((v>>1) | (carry? (T)1 << ((sizeof(T)*8)-1) : 0)); bool c = v & 1; carry = c; return out; }
    T a1 = (T)((v>>1) | (carry? (T)1 << ((sizeof(T)*8)-1) : 0)); bool c1 = v & 1;
    T a2 = (T)((a1>>1) | (c1? (T)1 << ((sizeof(T)*8)-1) : 0)); bool c2 = (v >> 1) & 1; carry = c2; return a2;
}

int main(){
    // Quiet emulator prints to keep fuzz snappy
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
    std::puts("[ROTX-PROPERTY-FUZZ] start...");
    std::srand(0x126D);
    H8S2350Emulator emu; emu.reset();
    for (int it=0; it<300; ++it){
        bool left = (std::rand()&1)!=0;
        int widthSel = std::rand()%3; // 0:.B 1:.W 2:.L
        int width = (widthSel==0)?1: (widthSel==1?2:4);
        bool two = (std::rand()&1)!=0;
        uint32_t pc = 0x4000;

        auto &r=emu.getRegisters(); auto &f=emu.getFlags();
        f.carry = (std::rand()&1)!=0;
        uint8_t reg = std::rand() % 8;

        if (width==1){
            uint8_t val = (uint8_t)std::rand(); r.r[reg] = (r.r[reg] & 0xFF00) | val;
            bool c = f.carry; uint8_t exp = left? model_rotxl<uint8_t>(val,c,two) : model_rotxr<uint8_t>(val,c,two);
            emit_rotx(emu, pc, left, 1, two, reg); r.pc = pc; emu.step();
            uint8_t got = (uint8_t)(r.r[reg] & 0xFF);
            assert(got == exp);
            assert(f.carry == c);
            assert(f.overflow == false);
            assert(f.zero == (got==0));
            assert(f.negative == ((got & 0x80)!=0));
        } else if (width==2){
            uint16_t val = (uint16_t)std::rand(); r.r[reg] = val;
            bool c = f.carry; uint16_t exp = left? model_rotxl<uint16_t>(val,c,two) : model_rotxr<uint16_t>(val,c,two);
            emit_rotx(emu, pc, left, 2, two, reg); r.pc = pc; emu.step();
            uint16_t got = (uint16_t)(r.r[reg] & 0xFFFF);
            assert(got == exp);
            assert(f.carry == c);
            assert(f.overflow == false);
            assert(f.zero == (got==0));
            assert(f.negative == ((got & 0x8000)!=0));
        } else {
            uint32_t val = (uint32_t)std::rand(); r.er[reg] = val;
            bool c = f.carry; uint32_t exp = left? model_rotxl<uint32_t>(val,c,two) : model_rotxr<uint32_t>(val,c,two);
            emit_rotx(emu, pc, left, 4, two, reg); r.pc = pc; emu.step();
            uint32_t got = r.er[reg];
            assert(got == exp);
            assert(f.carry == c);
            assert(f.overflow == false);
            assert(f.zero == (got==0));
            assert(f.negative == ((got & 0x80000000u)!=0));
        }
    }
    std::puts("[ROTX-PROPERTY-FUZZ] all checks passed.");
    return 0;
}
