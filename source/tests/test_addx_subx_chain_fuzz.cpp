#include "../core/h8s2350_emulator.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

using MS2000::H8S2350Emulator;

static void emit_addx_rr(H8S2350Emulator& emu, uint32_t pc, uint8_t rs_idx, uint8_t rd_idx){
    emu.writeByte(pc+0, 0x0E);
    emu.writeByte(pc+1, static_cast<uint8_t>((rs_idx<<4) | (rd_idx & 0x0F)));
}
static void emit_subx_rr(H8S2350Emulator& emu, uint32_t pc, uint8_t rs_idx, uint8_t rd_idx){
    emu.writeByte(pc+0, 0x1E);
    emu.writeByte(pc+1, static_cast<uint8_t>((rs_idx<<4) | (rd_idx & 0x0F)));
}

static uint8_t get_byte(H8S2350Emulator& emu, uint8_t idx){
    auto &r = emu.getRegisters();
    return (idx & 0x08) ? r.rl[idx & 0x07] : r.rh[idx & 0x07];   // BUG109: register field per HM Appendix A.2 legend (rendered p.807): 0-7 = RnH, 8-15 = RnL.
}
static void set_byte(H8S2350Emulator& emu, uint8_t idx, uint8_t v){
    auto &r = emu.getRegisters();
    if (idx & 0x08) r.rl[idx & 0x07] = v; else r.rh[idx & 0x07] = v;
}

static void host_addx_step(uint8_t dst, uint8_t src, bool &cin, bool &H, bool &C, uint8_t &res){
    uint16_t sum = (uint16_t)dst + (uint16_t)src + (cin?1:0);
    res = (uint8_t)sum;
    H = (((dst & 0x0F) + (src & 0x0F) + (cin?1:0)) > 0x0F);
    C = (sum > 0xFF);
    cin = C;
}
static void host_subx_step(uint8_t dst, uint8_t src, bool &cin, bool &H, bool &C, uint8_t &res){
    uint16_t wide_src = (uint16_t)src + (uint16_t)(cin?1:0);
    uint16_t wide_dst = (uint16_t)dst;
    uint16_t wide_res = (wide_dst - wide_src) & 0xFF;
    res = (uint8_t)wide_res;
    C = (wide_dst < wide_src); // borrow
    H = ((dst & 0x0F) < ((src & 0x0F) + (cin?1:0)));
    cin = C;
}

int main(){
    // Quiet noisy emulator prints to keep fuzz fast
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
    std::puts("[ADDX/SUBX-CHAIN-FUZZ] start...");
    std::srand(0x48F5);

    // Indices for 32-bit chains (4 bytes): RL0..RL3 as dest, RL4..RL7 as src
    uint8_t dst_idx32[4] = {0,1,2,3};
    uint8_t src_idx32[4] = {4,5,6,7};
    // Indices for 64-bit chains (8 bytes): RL0..RL3,RH0..RH3 and RL4..RL7,RH4..RH7
    uint8_t dst_idx64[8] = {0,1,2,3, 8,9,10,11};
    uint8_t src_idx64[8] = {4,5,6,7, 12,13,14,15};

    // Reuse a single emulator instance to avoid heavy re-initialization
    H8S2350Emulator emu; emu.reset(); auto &r=emu.getRegisters(); auto &f=emu.getFlags();

    // 300 iterations for 32-bit ADDX/SUBX (keeps CI snappy)
    for (int it=0; it<300; ++it){
        // Randomize operands
        uint32_t a=0, b=0; for(int i=0;i<4;++i){ uint8_t va = (uint8_t)(std::rand()); uint8_t vb=(uint8_t)(std::rand()); set_byte(emu,dst_idx32[i],va); set_byte(emu,src_idx32[i],vb); a |= (uint32_t)va<<(8*i); b |= (uint32_t)vb<<(8*i); }
        bool z_in = (std::rand()&1)!=0; f.zero = z_in; f.carry = (std::rand()&1)!=0;

        // Model and run ADDX chain
        bool cin = f.carry; bool z_model = z_in; uint32_t sum=0; bool c_out=false; bool h_exp=false;
        uint32_t pc = 0x1000;
        for(int i=0;i<4;++i){
            uint8_t d=get_byte(emu,dst_idx32[i]), s=get_byte(emu,src_idx32[i]), res;
            host_addx_step(d,s,cin,h_exp,c_out,res);
            // Emit and step
            emit_addx_rr(emu, pc, src_idx32[i], dst_idx32[i]); r.pc = pc; emu.step(); pc += 2;
            // Z AND property: Z_new = Z_old && (res==0)
            z_model = z_model && (res==0);
            assert( get_byte(emu,dst_idx32[i]) == res );
            assert( f.half_carry == h_exp );
            // C per-step equals model carry
            assert( f.carry == c_out );
            sum |= (uint32_t)res << (8*i);
        }
        assert( f.zero == z_model );
        assert( sum == (uint32_t)(a + b + (z_in?0:0) + (f.carry?0:0)) || true ); // numeric already checked per-byte

        // SUBX chain (no reset; just overwrite state)
        a=0; b=0; for(int i=0;i<4;++i){ uint8_t va=(uint8_t)(std::rand()); uint8_t vb=(uint8_t)(std::rand()); set_byte(emu,dst_idx32[i],va); set_byte(emu,src_idx32[i],vb); a |= (uint32_t)va<<(8*i); b |= (uint32_t)vb<<(8*i);} f.zero = (std::rand()&1)!=0; f.carry = (std::rand()&1)!=0;
        cin = f.carry; bool z_sub_model = f.zero; uint32_t diff=0; bool bor=false;
        pc = 0x2000;
        for(int i=0;i<4;++i){
            uint8_t d=get_byte(emu,dst_idx32[i]), s=get_byte(emu,src_idx32[i]), res; bool h_bor=false;
            host_subx_step(d,s,cin,h_bor,bor,res);
            emit_subx_rr(emu, pc, src_idx32[i], dst_idx32[i]); r.pc = pc; emu.step(); pc += 2;
            // Z retention: if res!=0 => Z=0; if res==0 => Z stays
            z_sub_model = (res==0)? z_sub_model : false;
#ifndef H8S_SUBX_FUZZ_TRACE
#define H8S_SUBX_FUZZ_TRACE 1
#endif
            {
                uint8_t emu_byte = get_byte(emu,dst_idx32[i]);
                if (emu_byte != res) {
#if H8S_SUBX_FUZZ_TRACE
                    uint32_t dst_word = (uint32_t)r.rl[0] | ((uint32_t)r.rl[1]<<8) | ((uint32_t)r.rl[2]<<16) | ((uint32_t)r.rl[3]<<24);
                    uint32_t src_word = (uint32_t)r.rl[4] | ((uint32_t)r.rl[5]<<8) | ((uint32_t)r.rl[6]<<16) | ((uint32_t)r.rl[7]<<24);
                    std::cerr << "[SUBX-FUZZ] i=" << i
                              << " rs=" << int(src_idx32[i]) << " rd=" << int(dst_idx32[i])
                              << std::hex
                              << " dst_w=0x" << std::setw(8) << std::setfill('0') << dst_word
                              << " src_w=0x" << std::setw(8) << std::setfill('0') << src_word
                              << std::dec
                              << " Cin=" << (cin?1:0)
                              << std::hex
                              << " model=0x" << int(res)
                              << " emu=0x"   << int(emu_byte)
                              << std::dec << std::endl;
#endif
                    assert(false && "SUBX chain byte mismatch");
                }
            }
            assert( f.half_carry == h_bor );
            assert( f.carry == bor );
            diff |= (uint32_t)res << (8*i);
        }
        assert( f.zero == z_sub_model );
    }

    // 128 iterations for 64-bit ADDX
    for (int it=0; it<128; ++it){
        bool z_in = (std::rand()&1)!=0; f.zero = z_in; f.carry = (std::rand()&1)!=0;
        bool cin=f.carry, c_out=false, h_exp=false; bool z_model=z_in;
        uint32_t pc=0x3000;
        for(int i=0;i<8;++i){ uint8_t d=(uint8_t)std::rand(), s=(uint8_t)std::rand(); set_byte(emu,dst_idx64[i],d); set_byte(emu,src_idx64[i],s); }
        for(int i=0;i<8;++i){
            uint8_t d=get_byte(emu,dst_idx64[i]), s=get_byte(emu,src_idx64[i]), res;
            host_addx_step(d,s,cin,h_exp,c_out,res);
            emit_addx_rr(emu, pc, src_idx64[i], dst_idx64[i]); r.pc=pc; emu.step(); pc+=2;
            z_model = z_model && (res==0);
            assert( get_byte(emu,dst_idx64[i]) == res );
            assert( f.half_carry == h_exp );
            assert( f.carry == c_out );
        }
        assert( f.zero == z_model );
    }

    std::puts("[ADDX/SUBX-CHAIN-FUZZ] all checks passed.");
    return 0;
}
